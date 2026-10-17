/**
  ******************************************************************************
  * @file    mq5.c
  * @brief   MQ-5 气体传感器驱动（PB0 = ADC1_IN8）
  ******************************************************************************
  */
#include "mq5.h"
#include "bsp_adc.h"

#define MQ5_ADC_CHANNEL    ADC_CHANNEL_8
#define MQ5_SAMPLE_TIMES   8

uint16_t MQ5_ReadRaw(void)
{
    return BSP_ADC_ReadChannel(MQ5_ADC_CHANNEL, MQ5_SAMPLE_TIMES);
}

uint16_t MQ5_ReadLevel(void)
{
    /* ADC 12bit 0~4095 → 0~100 */
    return (uint16_t)((uint32_t)MQ5_ReadRaw() * 100UL / 4095UL);
}
