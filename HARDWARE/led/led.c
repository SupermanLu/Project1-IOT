/**
  ******************************************************************************
  * @file    led.c
  * @brief   LED 驱动
  * @note    LED_On = 引脚置高；若你的 LED 为低电平点亮，请把 SET/RESET 对调
  ******************************************************************************
  */
#include "led.h"

void LED_Init(void)
{
    HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_RESET);
}

void LED_On(LedId_t id)
{
    if (id == LED1)
    {
        HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_SET);
    }
    else
    {
        HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_SET);
    }
}

void LED_Off(LedId_t id)
{
    if (id == LED1)
    {
        HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET);
    }
    else
    {
        HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_RESET);
    }
}

void LED_Toggle(LedId_t id)
{
    if (id == LED1)
    {
        HAL_GPIO_TogglePin(LED1_GPIO_Port, LED1_Pin);
    }
    else
    {
        HAL_GPIO_TogglePin(LED2_GPIO_Port, LED2_Pin);
    }
}
