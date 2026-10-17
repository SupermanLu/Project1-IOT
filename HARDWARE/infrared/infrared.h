/**
  ******************************************************************************
  * @file    infrared.h
  * @brief   红外接收硬件适配层（PB9 EXTI 下降沿 + DWT 微秒时间源）
  * @note    分层设计：
  *            infrared.c  —— 硬件适配层（本项目专属）：引脚/NVIC/中断，把
  *                           下降沿时刻（us）喂给解码器
  *            ir_decode.c —— NEC 纯解码器（通用，从 02-IR-control 移植）：
  *                           只吃时间戳不碰硬件，与时间源/引脚完全解耦
  *          使用方法：
  *            1. IR_Init()（依赖 BSP_Delay_Init 已启动 DWT）
  *            2. 主循环轮询 IR_Decoder_GetKey(&ir_decoder, &data) 取键值
  ******************************************************************************
  */
#ifndef __INFRARED_H
#define __INFRARED_H

#include "ir_decode.h"   /* IR_Decoder_t / ir_decoder / IR_Decoder_GetKey */

/* ---- 实测键码（本遥控器 addr=0x00，2026-09-07 逐键实测） ---- */
#define IR_KEY_UP     0x18   /* 上 */
#define IR_KEY_DOWN   0x52   /* 下 */
#define IR_KEY_OK     0x1C   /* OK/确认 */
/* 其余键码（0-9 等）后续做数字输入页时再补 */

/* 连发→长按：攒够 N 个连发帧才发 KEY_EVT_xL。NEC 连发约 110ms 一个，
 * 8 个 ≈ 按住 1s，与板载按键 KEY_LONG_MS=1000ms 手感对齐（防轻按误触长按） */
#define IR_LONG_REPEATS  8

void IR_Init(void);      /* 配置 PB9/EXTI/NVIC 并复位解码器 */
uint8_t IR_GetKeyEvent(void);  /* 取红外键并翻译成 KEY_EVT_*（bsp_key.h 事件码）；无事件返回 KEY_EVT_NONE */

#endif /* __INFRARED_H */
