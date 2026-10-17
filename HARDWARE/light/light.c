/**
  ******************************************************************************
  * @file    light.c
  * @brief   光敏电阻驱动（PA1 = ADC1_IN1）
  * @note    光照越强，光敏电阻阻值越小，分压点电压越低 → ADC 值越小。
  *          这里把"光照等级"取反：光照强(ADC 小)时输出等级高。
  ******************************************************************************
  */
#include "light.h"
#include "stm32f1xx_hal.h"                  // Device header

// #include "bsp_adc.h"

// #define LIGHT_ADC_CHANNEL   ADC_CHANNEL_1
// #define LIGHT_SAMPLE_TIMES  8

// uint16_t LIGHT_ReadRaw(void)
// {
//     return BSP_ADC_ReadChannel(LIGHT_ADC_CHANNEL, LIGHT_SAMPLE_TIMES);
// }

// uint16_t LIGHT_ReadLevel(void)
// {
//     uint16_t raw = LIGHT_ReadRaw();

//     /* 光敏分压：光照强 → 电压低 → raw 小 → 等级高（0~100） */
//     return (uint16_t)(100UL - ((uint32_t)raw * 100UL / 4095UL));
// }

void LIGHT_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	
	__HAL_RCC_GPIOA_CLK_ENABLE();	/*开启GPIOA时钟*/
	
	GPIO_InitStruct.Pin = GPIO_PIN_1;
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	
	HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

uint8_t LIGHT_IsBright(void)
{
	if(HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_1) == GPIO_PIN_RESET)
  {
    return 1;
  }
  else
  {
    return 0;
  }
}

uint16_t LIGHT_ReadLevel(void)
{
  /*亮= 100 ,暗 = 0*/
  return LIGHT_IsBright() ? 100 : 0;
}