/**
  ******************************************************************************
  * @file    relay.c
  * @brief   relay 驱动
  * @note    上电默认关闭，输出低电平,光耦不导通,继电器关闭,处于安全态
  ***********高电平吸合、光耦+9014 驱动、R3 下拉保证复位期断开
  ******************************************************************************
  */


#include "relay.h"

#define RELAY_PORT GPIOB
#define RELAY_PIN GPIO_PIN_5



void RELAY_Init(void)
{
    /*初始化引脚*/
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitStruct.Pin = RELAY_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(RELAY_PORT, &GPIO_InitStruct);
    HAL_GPIO_WritePin(RELAY_PORT, RELAY_PIN, GPIO_PIN_RESET);   /*默认输出低电平,光耦不导通,继电器关闭*/

}

void RELAY_ON(void)
{
    HAL_GPIO_WritePin(RELAY_PORT, RELAY_PIN, GPIO_PIN_SET);   /*输出高电平,光耦导通,继电器打开*/
}

void RELAY_OFF(void)
{
    HAL_GPIO_WritePin(RELAY_PORT, RELAY_PIN, GPIO_PIN_RESET);   /*输出低电平,光耦不导通,继电器关闭*/
}



