/* ============================================================
 * flash_ops.c - F103 内部 Flash 通用擦写实现
 * 位定义本地化(同 flash_param 旧纪律), 双头文件免疫
 * ============================================================ */
#include "flash_ops.h"

#if defined(USE_HAL_DRIVER)
  #include "stm32f1xx.h"      /* app 工程: CubeMX 世代 CMSIS 头 */
#else
  #include "stm32f10x.h"      /* boot 工程: RTE 世代 CMSIS 头(寄存器布局相同) */
#endif

/* --- 位定义本地化 --- */
#define FO_CR_PG        (1UL << 0)   /* 编程使能 */
#define FO_CR_PER       (1UL << 1)   /* 页擦除使能 */
#define FO_CR_STRT      (1UL << 6)   /* 启动操作 */
#define FO_CR_LOCK      (1UL << 7)   /* 锁定控制寄存器 */
#define FO_SR_BSY       (1UL << 0)   /* 操作进行中 */
#define FO_SR_PGERR     (1UL << 2)   /* 编程错误(目标非 0xFFFF) */
#define FO_SR_WRPRTERR  (1UL << 4)   /* 写保护错误 */
#define FO_KEY1         0x45670123UL
#define FO_KEY2         0xCDEF89ABUL
#define FO_TIMEOUT      2000000UL    /* BSY 轮询上限(不依赖任何定时器) */

static uint8_t fo_wait_bsy(void)
{
    uint32_t t = FO_TIMEOUT;

    while ((FLASH->SR & FO_SR_BSY) != 0U)
    {
        if (--t == 0U)
        {
            return 1U;
        }
    }
    return 0U;
}

static void fo_unlock(void)
{
    FLASH->KEYR = FO_KEY1;
    FLASH->KEYR = FO_KEY2;
}

static void fo_lock(void)
{
    FLASH->CR |= FO_CR_LOCK;
}

/* PGERR/WRPRTERR 检查 + 写 1 清零。返回 0=无错; 1=有错 */
static uint8_t fo_check_clear_err(void)
{
    if ((FLASH->SR & (FO_SR_PGERR | FO_SR_WRPRTERR)) != 0U)
    {
        FLASH->SR = FO_SR_PGERR | FO_SR_WRPRTERR;
        return 1U;
    }
    return 0U;
}

uint8_t FlashOps_ErasePage(uint32_t pageAddr)
{
    uint8_t err;

    if ((pageAddr & 0x3FFUL) != 0U)          /* 1KB 对齐守卫 */
    {
        return 1U;
    }

    fo_unlock();
    FLASH->CR |= FO_CR_PER;
    FLASH->AR  = pageAddr;
    FLASH->CR |= FO_CR_STRT;
    err = fo_wait_bsy();                     /* 期间同 bank 取指被暂停, 自然等 */
    FLASH->CR &= ~FO_CR_PER;
    if (fo_check_clear_err() != 0U)
    {
        err = 1U;
    }
    fo_lock();
    return err;
}

uint8_t FlashOps_Program(uint32_t addr, const uint8_t *data, uint32_t len)
{
    uint8_t err = 0U;
    uint32_t i;

    if ((len == 0U) || ((len & 1UL) != 0U))  /* 半字粒度: 非零偶数 */
    {
        return 1U;
    }

    fo_unlock();
    FLASH->CR |= FO_CR_PG;
    for (i = 0U; (i < len) && (err == 0U); i += 2U)
    {
        uint16_t hw = (uint16_t)data[i] | ((uint16_t)data[i + 1U] << 8);

        *(volatile uint16_t *)(addr + i) = hw;
        err = fo_wait_bsy();
        if (fo_check_clear_err() != 0U)
        {
            err = 1U;
        }
    }
    FLASH->CR &= ~FO_CR_PG;
    fo_lock();

    if (err != 0U)
    {
        return 1U;
    }

    /* 读回逐字节校验: 写进去了 != 写对了 */
    {
        const volatile uint8_t *src = (const volatile uint8_t *)addr;

        for (i = 0U; i < len; i++)
        {
            if (src[i] != data[i])
            {
                return 1U;
            }
        }
    }
    return 0U;
}
