/* ============================================================
 * w25q64_port_app.c - 接缝的 HAL 实现 (仅 app 工程编译)
 * 硬件事实: SPI2(PB13/14/15) + PA8 软件片选, 由 CubeMX 配置
 * 行为保证: 与 M9.3b 之前的原 w25q64.c 逐位一致 (回归风险≈0)
 * ============================================================ */
#include "w25q64_port.h"
#include "stm32f1xx_hal.h"
#include "main.h"             /* W_CS_Pin / W_CS_GPIO_Port (CubeMX) */

extern SPI_HandleTypeDef hspi2;   /* Core/Src/spi.c */

void W25Q64_Port_Init(void)
{
    /* 总线已由 MX_SPI2_Init() 建好, 有意空实现:
     * 接缝契约 = "boot 必须自带总线启动, app 复用 CubeMX" */
}

void W25Q64_Port_DeInit(void)
{
    /* app 外设由 CubeMX 生命周期管理, 空 */
}

void W25Q64_Port_CsLow(void)
{
    HAL_GPIO_WritePin(W_CS_GPIO_Port, W_CS_Pin, GPIO_PIN_RESET);
}

void W25Q64_Port_CsHigh(void)
{
    HAL_GPIO_WritePin(W_CS_GPIO_Port, W_CS_Pin, GPIO_PIN_SET);
}

uint8_t W25Q64_Port_Xfer(uint8_t tx)
{
    uint8_t rx = 0;

    if (HAL_SPI_TransmitReceive(&hspi2, &tx, &rx, 1, 100) != HAL_OK)
    {
        return 0xFF;   /* 失败语义: 返回全 1 (总线悬空默认电平) */
    }
    return rx;
}

void W25Q64_Port_DelayMs(uint32_t ms)
{
    HAL_Delay(ms);
}
