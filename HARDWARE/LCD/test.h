/**
 * @file    test.h
 * @brief   ILI9488 LCD 综合测试 Demo 函数声明 (STM32F407 HAL 移植版)
 *          仅包含显示相关测试(不含触摸屏)。
 *
 * @note    测试改为非阻塞状态机: 主循环调用 run_lcd_tests() 即可,
 *          画面间隔使用 DelayTimer 计时, 不阻塞 CPU。
 */
#ifndef __TEST_H
#define __TEST_H

#include "sys.h"

void DrawTestPage(u8 *str);
void run_lcd_tests(void);

#endif /* __TEST_H */
