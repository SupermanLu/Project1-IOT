/**
  ******************************************************************************
  * @file    buzzer.h
  * @brief   蜂鸣器驱动：无源，TIM4_CH1(PB6) PWM 2.5kHz 方波发声
  * @note    API 不变（On/Off/Toggle），内部引擎由 GPIO 电平改为 TIM4 PWM
  *          ——无源蜂鸣器需要交变电压，静态电平只咔一声不发声（实测确认）
  ******************************************************************************
  */
#ifndef __BUZZER_H
#define __BUZZER_H

#include "main.h"

void BUZZ_Init(void);   /* 初始静音 */
void BUZZ_On(void);     /* 发声（PB6 置低） */
void BUZZ_Off(void);    /* 静音（PB6 置高） */
void BUZZ_Toggle(void);

#endif /* __BUZZER_H */
