/**
  ******************************************************************************
  * @file    bsp_debug.h
  * @brief   调试打印：输出到 USART1（115200, PA9/PA10）
  * @note    - DBG_Printf：不依赖 MicroLIB/半主机，直接用 USART1 发送
  *          - fputc 重定向：若想直接用标准 printf，需在 Keil 勾选 "Use MicroLIB"
  ******************************************************************************
  */
#ifndef __BSP_DEBUG_H
#define __BSP_DEBUG_H

#include <stdio.h>

/* printf 重定向到 USART1（需在 Keil 勾选 "Use MicroLIB"） */
int fputc(int ch, FILE *f);

/* 通用格式化打印：任意 C 库环境下可用（内部 vsnprintf + USART1 发送） */
void DBG_Printf(const char *fmt, ...);

#endif /* __BSP_DEBUG_H */
