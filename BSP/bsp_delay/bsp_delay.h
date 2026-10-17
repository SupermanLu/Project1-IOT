/**
  ******************************************************************************
  * @file    bsp_delay.h
  * @brief   基于 DWT 精确微秒/毫秒延时（不依赖 SysTick/FreeRTOS，可移植）
  ******************************************************************************
  */
#ifndef __BSP_DELAY_H
#define __BSP_DELAY_H

#include <stdint.h>

void BSP_Delay_Init(void);        /* 使能 DWT->CYCCNT，需在 SystemClock_Config 后调用一次 */
void BSP_Delay_us(uint32_t us);   /* 微秒级忙等延时 */
void BSP_Delay_ms(uint32_t ms);   /* 毫秒级忙等延时 */
uint32_t BSP_DWT_GetUs(void);     /* 读自由运行微秒计数（测脉宽用：两次相减算间隔） */

#endif /* __BSP_DELAY_H */
