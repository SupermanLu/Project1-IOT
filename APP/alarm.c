/**
  ******************************************************************************
  * @file    alarm.c
  * @brief   报警判定与输出仲裁（CtrlTask 100ms 节拍调用）
  * @note    设计：判定器 + 输出仲裁。本阶段只做 gas + 红绿LED + 蜂鸣器，
  *          温度判定器 + 继电器放第二步（加入后红灯仲裁改为"任一报警即闪"）。
  *
  *  数据流：SensorTask(生产者, 每次提交 g_app.seq++) → 本模块(消费者)
  *          → 判定结果发布到 g_alarm_gas（UITask 用它拦截 OK 键做消音）
  *
  *  gas 状态机（口诀：超一格、清到底、中间躺平）：
  *    s_cnt   连续超标计数（N-of-M 去抖：连续 3 个样本超标才报警）
  *    s_state 0=NORMAL 1=ALARM
  *    触发线  mq5_level > gas_max          → cnt 爬一步，到 3 报警
  *    解除线  mq5_level < gas_max - 10     → cnt 归零，报警中才解除+清消音
  *            （回差 10：防止读数在阈值线上抖动导致报警反复横跳）
  *    中间地带 什么都不做（不奖励也不惩罚——迟滞的核心）
  *
  *  潜在坑位（已防护）：
  *    - gas_max <= 10 时解除线会无符号下陷 → 加 >10 守卫，此设置下报警
  *      只能靠调高阈值解除（阈值设到 10 以下本就是无效配置）
  *    - 计数器到顶封顶不再加，防 uint8_t 长期报警后回绕
  ******************************************************************************
  */
#include "alarm.h"
#include "app_data.h"
#include "led.h"
#include "buzzer.h"
#include "relay.h"
#include "cmsis_os.h"

extern osMutexId xAppMutex;   /* 互斥锁句柄：定义在 freertos.c，本模块借用 */

/* ---- 状态机变量（跨调用存活，必须 static） ---- */
static uint8_t s_lastSeq = 0;   /* 上次评估过的提交序号（新样本凭证） */
static uint8_t s_cnt     = 0;   /* 连续超标计数 */
static uint8_t s_state   = 0;   /* 0=NORMAL 1=ALARM */

/* ---- 输出节拍分频（100ms tick 的整数倍，整数分频不抖动） ---- */
static uint8_t s_blinkDiv = 0;  /* 红灯：÷3 → 300ms 翻转 = 600ms 闪烁周期 */
static uint8_t s_beepDiv  = 0;  /* 蜂鸣：0~9 循环 → 前 2 拍响(200ms) 后 8 拍停(800ms) */

/*-----------温度判定器 + 继电器（M6.5 第二步）-----------*/
static uint8_t s_tcnt = 0;          /*温度连续超标计数*/
static uint8_t s_tstate = 0;        /*温度报警状态 0 = normal 1 = alarm*/
static uint32_t s_relayOntick = 0; /*继电器吸合时刻(最短保持计时)*/
static uint8_t s_relayOn = 0;       /*继电器当前状态 0 = 断 1 = 合*/


/**
  * @brief  报警模块初始化：上电默认"正常相"
  * @note   必须在 BUZZ_Init 之后调用（本模块输出会操作 PWM 句柄）。
  *         本函数由 CtrlTask 在自检序列结束后调用，顺序天然保证。
  */
void Alarm_Init(void)
{
    LED_Off(LED1);
    LED_On(LED2);
    BUZZ_Off();
}

/**
  * @brief  100ms 节拍：gas 判定 + 状态发布 + 输出仲裁
  * @note   四段式结构，各司其职：
  *           ① 门(卫语句)   seq 没变 = 没新样本，跳过评估
  *           ② 评估(锁内)   三条路搬运 s_cnt/s_state
  *           ③ 发布(锁内)   g_alarm_gas = s_state（UITask 的 OK 拦截读它）
  *           ④ 仲裁(锁外)   红闪/绿亮/蜂鸣，纯硬件操作不占锁
  */
void Alarm_Tick(void)
{
    int32_t t10;    /*温度合成值: 0.1℃ 单位*/
    int32_t tClear; /* 温度解除线*/
    /* ========== 锁内：读 + 判 + 发布（全是微秒级纯逻辑，无 I/O） ========== */
    osMutexWait(xAppMutex, osWaitForever);

    if (g_app.seq != s_lastSeq)               /* ① 门：新样本才评估 */
    {
        s_lastSeq = g_app.seq;                /* 盖章：本样本已评估 */

        if (g_app.mq5_level > g_set.gas_max)  /* ② 路① 超标：爬一格 */
        {
            if (s_cnt < 3U)
            {
                s_cnt++;
            }
            if (3U == s_cnt)                  /* 常量在左：坑17防身 */
            {
                s_state = 1U;                 /* 恰好到顶这一次置位 */
            }
        }
        else if ((g_set.gas_max > 10U) &&     /* 守卫：防解除线下陷 */
                 (g_app.mq5_level < (g_set.gas_max - 10U)))  /* ② 路② 回差线下 */
        {
            s_cnt = 0U;                       /* 楼梯归零 */
            if (s_state == 1U)                /* 报警中才谈解除 */
            {
                s_state = 0U;
                g_buzzer_mute = 0U;           /* 解除顺手清消音 */
            }
        }
        /* ② 路③ 中间地带：什么都不做 */

        /* ==== 温度判定器（和 gas 同款三条路，变量换名） ==== */
        t10    = (int32_t)g_app.temp_int * 10 + g_app.temp_dec;  /* 29.6°C → 296 */
        tClear = (int32_t)g_set.temp_max * 10 - 20;              /* 解除线 = 触发线 - 2°C */

        if (t10 > (int32_t)g_set.temp_max * 10)              /* 路① 超标：爬一格 */
        {
            if (s_tcnt < 3U)
            {
                s_tcnt++;
            }
            if (3U == s_tcnt)                                /* 常量在左 */
            {
                s_tstate = 1U;
            }
        }
        else if (t10 < tClear)                               /* 路② 回差线下 */
        {
            s_tcnt = 0U;
            if (s_tstate == 1U)
            {
                s_tstate = 0U;                               /* 解除——注意：不清 mute！ */
            }
        }
        /* 路③ 中间地带：什么都不写 */

        g_alarm_gas = s_state;                /* ③ 发布给 UITask */
    }

    osMutexRelease(xAppMutex);                /* ★ 与 Wait 成对，漏了就死锁 */

    /* ========== 锁外：④ 输出仲裁（纯硬件操作） ========== */
    if ((s_state == 1U) || (s_tstate == 1U))   /* 任一报警 → 红闪 */
    {
        /* 红灯：300ms 翻转 */
        s_blinkDiv++;
        if (s_blinkDiv >= 3U)
        {
            s_blinkDiv = 0U;
            LED_Toggle(LED1);
        }
        LED_Off(LED2);

        /* 蜂鸣：仅 gas 报警响（温度=红闪+继电器，不响铃——规格定案）；
           beepDiv 0~9 循环；消音 = 全程停，但红灯照闪（消音≠解除） */
        s_beepDiv++;
        if (s_beepDiv >= 10U)
        {
            s_beepDiv = 0U;
        }
        if ((s_state == 1U) && (g_buzzer_mute == 0U) && (s_beepDiv < 2U))
        {
            BUZZ_On();
        }
        else
        {
            BUZZ_Off();
        }
    }
    else
    {
        /* 正常相：红灯灭、绿灯常亮、静音、分频归零 */
        LED_Off(LED1);
        LED_On(LED2);
        BUZZ_Off();
        s_blinkDiv = 0U;
        s_beepDiv  = 0U;
    }

    /* 继电器：温控 bang-bang + 最短保持 30s（防回差线上反复开合烧触点） */
    if ((s_tstate == 1U) && (s_relayOn == 0U))
    {
        RELAY_ON();
        s_relayOn     = 1U;
        s_relayOntick = HAL_GetTick();         /* 吸合时刻戳 */
    }
    else if ((s_tstate == 0U) && (s_relayOn == 1U))
    {
        /* 断开双条件：温度已回落 且 吸合满 30s——只满足一个就继续开着 */
        if ((HAL_GetTick() - s_relayOntick) >= 30000U)
        {
            RELAY_OFF();
            s_relayOn = 0U;
        }
    }
}
