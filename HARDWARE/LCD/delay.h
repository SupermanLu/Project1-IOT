/**
 * @file    delay.h
 * @brief   延时 / 非阻塞计时 (STM32F407 HAL)
 *
 * @note
 *  - delay_ms(ms)       : 阻塞延时, 基于 HAL_Delay() (SysTick 1ms)。
 *                         仅用于 LCD 初始化等"硬时序"场合, 不可在
 *                         主循环里大量使用, 否则会阻塞 CPU。
 *  - delay_plain_ms(ms) : 同上, 语义化别名, 明确表示"这里是必要硬延时"。
 *
 *  - DelayTimer / delay_start() / delay_expired() :
 *                         非阻塞计时。用于主循环里"停留 N 毫秒再翻页"
 *                         等场景, 期间 CPU 可去做别的事, 不阻塞。
 */
#ifndef __DELAY_H
#define __DELAY_H

#include "stm32f1xx_hal.h"

/* 阻塞延时 (仅 LCD 初始化等必要硬时序使用) */
#define delay_ms(ms)         HAL_Delay((ms))
#define delay_plain_ms(ms)   HAL_Delay((ms))

/* 非阻塞计时器 (基于 HAL 的 1ms SysTick 计数) */
typedef struct
{
    uint32_t start;   /* 起始 tick */
    uint32_t period;  /* 目标时长(ms) */
} DelayTimer;

/* 启动一个非阻塞计时器 */
static inline void delay_start(DelayTimer *t, uint32_t period_ms)
{
    t->start  = HAL_GetTick();
    t->period = period_ms;
}

/* 计时是否到期 (到期返回 1)。不阻塞, 可反复调用 */
static inline uint8_t delay_expired(DelayTimer *t)
{
    return ((HAL_GetTick() - t->start) >= t->period) ? 1 : 0;
}

#endif /* __DELAY_H */
