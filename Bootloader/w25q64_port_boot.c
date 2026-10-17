/* ============================================================
 * w25q64_port_boot.c - 接缝的寄存器级实现 (仅 boot 工程编译)
 *
 * 硬件事实 (与 app 的 CubeMX 配置严格一致):
 *   SPI2: SCK=PB13, MISO=PB14, MOSI=PB15, APB1 36MHz / 2 = 18MHz
 *   模式: CPOL=0 CPHA=0 (第一沿采样), 8bit, MSB 先行, 软件 NSS
 *   CS  : PA8 推挽输出, 空闲高
 *
 * 位定义全部本地化 (flash_param.c 同款纪律): RTE 的 stm32f10x.h
 * 保证寄存器结构体可用, 但 SPL 风格宏(GPIO_Pin_x/SPI_CR1_xxx)
 * 需要 conf 头, 不依赖 —— 自己定义, 两套头文件都免疫
 * ============================================================ */
#include "w25q64_port.h"
#include "stm32f10x.h"

/* --- 本地位定义 --- */
#define BP_SPI_CR1_SPE    (1UL << 6)   /* SPI 使能 */
#define BP_SPI_CR1_MSTR   (1UL << 2)   /* 主机模式 */
#define BP_SPI_CR1_SSM    (1UL << 9)   /* 软件从选管理 */
#define BP_SPI_CR1_SSI    (1UL << 8)   /* 内部从选电平(主机必须=1) */
#define BP_SPI_SR_TXE     (1UL << 1)   /* 发送寄存器空 */
#define BP_SPI_SR_RXNE    (1UL << 0)   /* 接收寄存器非空 */

#define BP_RCC_IOPAEN     (1UL << 2)   /* APB2: GPIOA 时钟 */
#define BP_RCC_IOPBEN     (1UL << 3)   /* APB2: GPIOB 时钟 */
#define BP_RCC_SPI2EN     (1UL << 14)  /* APB1: SPI2 时钟 */

void W25Q64_Port_Init(void)
{
    RCC->APB2ENR |= BP_RCC_IOPAEN | BP_RCC_IOPBEN;
    RCC->APB1ENR |= BP_RCC_SPI2EN;

    /* PA8 = CS: 推挽输出 50MHz (CRH nibble0: MODE=11, CNF=00), 空闲高 */
    GPIOA->CRH = (GPIOA->CRH & ~0xFUL) | 0x3UL;
    GPIOA->BSRR = (1UL << 8);          /* BSRR 写 1 置位, 原子操作 */

    /* PB13=SCK AF推挽 / PB14=MISO 浮空输入 / PB15=MOSI AF推挽
     * CRH nibble5/6/7: 0xB=MODE11+CNF10(AF推挽), 0x4=MODE00+CNF01(浮空输入)
     * 掩码必须是 0xFFF00000(bits20-31=PB13,14,15)!
     * 曾错写 0xFFF0000(bits16-27) → PB15 没被清, 复位0x4|0xB=0xF 开漏
     * → MOSI 无上拉电平漂浮 → W25Q64 收到垃圾指令不应答 */
    GPIOB->CRH = (GPIOB->CRH & ~0xFFF00000UL)
               | (0xBUL << 20)         /* PB13 SCK  */
               | (0x4UL << 24)         /* PB14 MISO */
               | (0xBUL << 28);        /* PB15 MOSI */

    /* CR1: MSTR + SSM + SSI + BR=000(/2=18MHz) + SPE
     * CPOL=0/CPHA=0/MSB/8bit 恰好都是复位值 0, 无需再写 */
    SPI2->CR1 = BP_SPI_CR1_SSM | BP_SPI_CR1_SSI | BP_SPI_CR1_MSTR
              | BP_SPI_CR1_SPE;
}

void W25Q64_Port_DeInit(void)
{
    SPI2->CR1 &= ~BP_SPI_CR1_SPE;      /* 谁开的谁关, 跳转前交还干净硬件 */
    RCC->APB1ENR &= ~BP_RCC_SPI2EN;
}

void W25Q64_Port_CsLow(void)
{
    GPIOA->BSRR = (1UL << 8) << 16;    /* BSRR 高 16 位写 1 = 复位 */
}

void W25Q64_Port_CsHigh(void)
{
    GPIOA->BSRR = (1UL << 8);
}

uint8_t W25Q64_Port_Xfer(uint8_t tx)
{
    while ((SPI2->SR & BP_SPI_SR_TXE) == 0U) { }   /* 等发送空 */
    SPI2->DR = tx;
    while ((SPI2->SR & BP_SPI_SR_RXNE) == 0U) { }  /* 等接收满 */
    return (uint8_t)SPI2->DR;
}

void W25Q64_Port_DelayMs(uint32_t ms)
{
    /* 粗延时: 每圈约 5 周期 @72MHz, 14400 圈 ≈ 1ms, 仅用于 tVSL */
    while (ms--)
    {
        volatile uint32_t i = 14400U;
        while (i--)
        {
            __asm volatile ("nop");
        }
    }
}
