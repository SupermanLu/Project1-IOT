/**
  ******************************************************************************
  * @file    light.h
  * @brief   光敏电阻驱动（分压采样，PA1 = ADC1_IN1）
  * @note    输出 0~100 的光照等级：数值越大表示越亮
  ******************************************************************************
  */
#ifndef __LIGHT_H
#define __LIGHT_H

#include <stdint.h>

// /**
//   * @brief  读取光照等级（0~100，越大越亮）
//   */
// uint16_t LIGHT_ReadLevel(void);

// /**
//   * @brief  读取光敏原始 ADC 值（0~4095）
//   */
// uint16_t LIGHT_ReadRaw(void);

void LIGHT_Init(void);   /*PA1 从模拟输入模式改为数字输入*/
uint8_t LIGHT_IsBright(void); /*原始二值: 1 = 亮 , 0 = 灭*/


#endif /* __LIGHT_H */
