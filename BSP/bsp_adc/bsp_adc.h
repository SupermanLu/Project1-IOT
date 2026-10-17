/**
  ******************************************************************************
  * @file    bsp_adc.h
  * @brief   ADC1 单通道读取（软件切换通道，多采样取平均）
  * @note    适用于低频采集（MQ-5 气体、光敏等），不依赖 DMA/中断
  ******************************************************************************
  */
#ifndef __BSP_ADC_H
#define __BSP_ADC_H

#include <stdint.h>
#include "stm32f1xx_hal.h"

/**
  * @brief  读取 ADC1 指定通道，多次采样取平均
  * @param  channel   : 通道号（如 ADC_CHANNEL_8 / ADC_CHANNEL_1）
  * @param  samples   : 采样次数（建议 4~8 次做均值滤波）
  * @retval 平均原始值（12bit：0~4095）；失败返回 0
  */
uint16_t BSP_ADC_ReadChannel(uint32_t channel, uint8_t samples);

#endif /* __BSP_ADC_H */
