/**
  ******************************************************************************
  * @file    bsp_key.h
  * @brief   按键轮询扫描驱动（KEY1=PA11 / KEY2=PA12 / KEY3=PA15）
  * @note    按键低电平有效（内部上拉，按下接地）
  *          - 轮询方式：主循环周期性调用 KEY_Scan()，扫描间隔 >= 20ms
  *            天然完成消抖（两次采样间隔就是消抖时间）
  *          - 事件：短按 = 松手时发一次（KEY_EVT_x）；
 *                  长按 = 按住超过 KEY_LONG_MS 发一次（KEY_EVT_xL），松手不补发
  *          - 主循环读取引脚电平即可，不依赖 EXTI 中断（CubeMX 已配成
  *            IT_RISING 模式，输入电平照常可读，NVIC 没开也不影响）
  ******************************************************************************
  */
#ifndef __BSP_KEY_H
#define __BSP_KEY_H

#include <stdint.h>
#include "stm32f1xx_hal.h"

/* 按键事件码（evt == 0 表示无事件） */
#define KEY_EVT_NONE    0
#define KEY_EVT_1       1   /* KEY1 按下 */
#define KEY_EVT_2       2   /* KEY2 按下 */
#define KEY_EVT_3       3   /* KEY3 按下 */
#define KEY_EVT_1L      4   /* KEY1 长按 (可以按住超过1s)*/
#define KEY_EVT_2L      5   /* KEY2 长按 (可以按住超过1s)*/
#define KEY_EVT_3L      6   /* KEY3 长按 (可以按住超过1s)*/
#define KEY_LONG_MS     1000  /* 长按时间阈值 (ms) */
#define KEY_REPEAT_MS   150   /* 重复按时间间隔值 (ms) */


/**
  * @brief  扫描一次按键（主循环每 20ms 左右调一次）
  * @retval KEY_EVT_x 事件码；无按下沿返回 KEY_EVT_NONE
  */
uint8_t KEY_Scan(void);

#endif /* __BSP_KEY_H */
