/**
 * @file    SPI.h
 * @brief   STM32F407 HAL SPI driver for ILI9488 LCD (SPI1)
 *
 * @note    依赖 CubeMX 生成的 SPI1 句柄 hspi1 (在 main.c / spi.c 中定义,
 *          此处用 extern 引用)。
 *          引脚连接 (STM32F407VG):
 *          MOSI (SDI) = PA7  (SPI1_MOSI)
 *          SCK         = PA5  (SPI1_SCK)
 *          LED (BL)    = PA1  (GPIO_Output)
 *          RST         = PA2  (GPIO_Output)
 *          DC/RS       = PA3  (GPIO_Output)
 *          CS          = PA4  (GPIO_Output, 软件控制)
 *          MISO        = PA6  (SPI1_MISO, 屏一般不回数据, 可不接)
 */

#ifndef _SPI_H_
#define _SPI_H_

#include "stm32f1xx_hal.h"
#include <stdint.h>

/*===========================================================================
 * SPI 句柄 — 由 CubeMX 生成 (在 main.c 中定义), 此处引用
 * 如果你的工程里句柄名字不是 hspi1, 请修改这里
 *=========================================================================== */
extern SPI_HandleTypeDef hspi1;

/*===========================================================================
 * 函数声明
 *===========================================================================*/
void SPI_Init(void);
void SPIv_WriteData(uint8_t data);
void SPI_WriteBytes(uint8_t *data, uint16_t len);

#endif /* _SPI_H_ */
