/**
  ******************************************************************************
  * @file    bsp_debug.c
  * @brief   调试打印：DBG_Printf / fputc 重定向到 USART2
  * @note    临时方案：CH340 缺货，借调试器串口（接 PA2/PA3=USART2）看打印。
  *          注意：USART2 原规划给 ESP-01S，M7 联调时要切回 USART1！
  ******************************************************************************
  */
#include "bsp_debug.h"
#include "stm32f1xx_hal.h"
#include <stdarg.h>

/* huart2 在 Core/Src/usart.c 中定义 */
extern UART_HandleTypeDef huart2;

int fputc(int ch, FILE *f)
{
    (void)f;
    HAL_UART_Transmit(&huart2, (uint8_t *)&ch, 1, 100);
    return ch;
}

void DBG_Printf(const char *fmt, ...)
{
    char buf[128];
    va_list args;
    int len;

    va_start(args, fmt);
    len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (len > 0)
    {
        if (len >= (int)sizeof(buf))
        {
            len = (int)sizeof(buf) - 1;
        }
        HAL_UART_Transmit(&huart2, (uint8_t *)buf, (uint16_t)len, 1000);
    }
}
