/**
 * @file    SPI.c
 * @brief   STM32F407 HAL SPI1 驱动 (主机模式, 用于 ILI9488 LCD)
 *
 * @note    SPI1 的硬件初始化 (时钟、引脚复用、参数) 由 CubeMX 生成的
 *          MX_SPI1_Init() 完成, 本文件只提供发送封装函数。
 *          参数要求: 主机 / 全双工 / 8bit / MSB / CPOL=0 / CPHA=0 (模式0)
 *                   波特率预分频使 SCK ≈ 40MHz (APB2=84MHz 时 PRESCALER=2)
 */

#include "SPI.h"

/*===========================================================================
 * SPI_Init() - 初始化 SPI1
 *
 * 注: 实际 SPI 外设初始化由 CubeMX 的 MX_SPI1_Init() 完成,
 *     调用 HAL_SPI_Init(&hspi1) 即可。如果你的 main() 里已经调用过,
 *     这个函数可以为空; 保留它以保持与原架构一致的调用接口。
 *=========================================================================== */
void SPI_Init(void)
{
    /* 如果 CubeMX 生成的 MX_SPI1_Init() 已经在 main 里调用, 这里无需操作。
     * 若想在此统一初始化, 取消下一行注释并确保 MX_SPI1_Init 已实现:
     *   MX_SPI1_Init();
     */
}

/*===========================================================================
 * SPIv_WriteData() - 阻塞发送一个字节
 *=========================================================================== */
void SPIv_WriteData(uint8_t data)
{
    HAL_SPI_Transmit(&hspi1, &data, 1, HAL_MAX_DELAY);
}

/*===========================================================================
 * SPI_WriteBytes() - 阻塞发送多个字节
 *=========================================================================== */
void SPI_WriteBytes(uint8_t *data, uint16_t len)
{
    if (len == 0)
        return;
    HAL_SPI_Transmit(&hspi1, data, len, HAL_MAX_DELAY);
}
