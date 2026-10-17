/**
  ******************************************************************************
  * @file    bsp_delay.c
  * @brief   基于 DWT->CYCCNT 的微秒/毫秒精确延时
  * @note    用于 DHT11 单总线、BMP180 转换等待等对时序有要求的场景；
  *          精度取决于系统时钟（默认 72MHz，1 tick = 1/72 us）
  ******************************************************************************
  */
#include "bsp_delay.h"
#include "stm32f1xx_hal.h"

/* SystemCoreClock 由 system_stm32f1xx.c 定义 */
extern uint32_t SystemCoreClock;

static uint32_t g_us_ticks = 72;   /* 1us 对应的 CYCCNT 数（72MHz 时 = 72） */

void BSP_Delay_Init(void)
{
    /* 使能 DWT 追踪时钟并启动 CYCCNT 计数器 */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    g_us_ticks = SystemCoreClock / 1000000UL;
    if (g_us_ticks == 0)
    {
        g_us_ticks = 72;
    }
}

void BSP_Delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * g_us_ticks;
    while ((DWT->CYCCNT - start) < ticks)
    {
        /* 忙等 */
    }
}

void BSP_Delay_ms(uint32_t ms)
{
    while (ms-- > 0)
    {
        BSP_Delay_us(1000);
    }
}

/**
  * @brief  读 DWT 自由运行计数（单位 us）
  * @note   测脉宽的标准姿势：不清零计数器，而是"记下上次读数，两次相减"。
  *         32 位无符号减法天然处理回绕（72MHz 下约 59.6s 回绕一次，
  *         只要两次测量间隔远小于回绕周期，now - last 结果总是正确）。
  *         严禁在 ISR 里清零 CYCCNT——BSP_Delay_us/DHT11 都依赖它自由运行。
  */
uint32_t BSP_DWT_GetUs(void)
{
    return DWT->CYCCNT / g_us_ticks;
}
