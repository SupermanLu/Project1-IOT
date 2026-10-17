/**
  ******************************************************************************
  * @file    bsp_adc.c
  * @brief   ADC1 单通道读取（软件切换通道 + 多采样平均）
  ******************************************************************************
  */
#include "bsp_adc.h"
#include "stm32f1xx_hal.h"

extern ADC_HandleTypeDef hadc1;

uint16_t BSP_ADC_ReadChannel(uint32_t channel, uint8_t samples)
{
    ADC_ChannelConfTypeDef sConfig = {0};
    uint32_t sum = 0;
    uint8_t i;

    if (samples == 0)
    {
        samples = 1;
    }

    /* 切换到目标通道 */
    sConfig.Channel      = channel;
    sConfig.Rank         = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5; /* 长采样，高内阻源稳定 */
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
    {
        return 0;
    }

    /* 多次采样求平均 */
    for (i = 0; i < samples; i++)
    {
        if (HAL_ADC_Start(&hadc1) == HAL_OK)
        {
            if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK)
            {
                sum += HAL_ADC_GetValue(&hadc1);
            }
            HAL_ADC_Stop(&hadc1);
        }
    }

    return (uint16_t)(sum / samples);
}
