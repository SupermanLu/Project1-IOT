/**
  ******************************************************************************
  * @file    led.h
  * @brief   LED 驱动：LED1=PB4, LED2=PC13
  ******************************************************************************
  */
#ifndef __LED_H
#define __LED_H

#include "main.h"

typedef enum
{
    LED1 = 0,
    LED2
} LedId_t;

void LED_Init(void);          /* 初始全部熄灭 */
void LED_On(LedId_t id);
void LED_Off(LedId_t id);
void LED_Toggle(LedId_t id);

#endif /* __LED_H */
