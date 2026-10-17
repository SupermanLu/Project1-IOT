/**
  ******************************************************************************
  * @file    infrared.c
  * @brief   红外接收硬件适配层：PB9 EXTI 下降沿 → DWT 时间戳 → NEC 解码器
  * @note    移植对照（02-IR-control 原工程 → 本工程）：
  *            | 原工程                        | 本工程                        |
  *            |------------------------------|-------------------------------|
  *            | IR_IN_Pin（原工程引脚）       | PB9（IR_Pin，main.h）         |
  *            | TIM2 自由计数（16位，1us）    | DWT 自由计数（32位，1us）     |
  *            | __HAL_TIM_GET_COUNTER(&htim2)| BSP_DWT_GetUs()               |
  *            | EXTI15_10_IRQHandler         | EXTI9_5_IRQHandler            |
  *          触发沿保持下降沿不变（接收头空闲输出高，载波段开头拉低），
  *          ir_decode.c 的时序判定就是按下降沿间隔标定的（引导码 13.5ms）。
  *          为什么用 DWT 不配 TIM2：DWT 已被 BSP_Delay/DHT11 共用，且 32 位
  *          免回绕补偿；一个 MCU 的 DWT 只此一份，但解码器逻辑零改动。
  ******************************************************************************
  */
#include "infrared.h"
#include "main.h"        /* IR_Pin / IR_GPIO_Port（自带 HAL 头） */
#include "bsp_delay.h"   /* BSP_DWT_GetUs */
#include "bsp_key.h"     /* KEY_EVT_* 事件码（红外键 → 页面框架事件） */
#include "bsp_debug.h"   /* DBG_Printf（未映射键码打印） */

/**
  * @brief  红外接收初始化（自包含：引脚/EXTI/NVIC/解码器全在此配好）
  * @note   必须在 BSP_Delay_Init() 之后调用（依赖 DWT 计数器已启动）
  */
void IR_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();   /* F1 的 EXTI 引脚映射寄存器在 AFIO，必须开时钟 */

    /* PB9：EXTI 下降沿（与原工程 GPIO_MODE_IT_FALLING 一致）
     * gpio.c 已配过一次 NOPULL，这里重配成上拉增强抗干扰（重复配置无害） */
    GPIO_InitStruct.Pin  = IR_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(IR_GPIO_Port, &GPIO_InitStruct);

    /* 优先级 0x0F = 最低：ISR 里只记录时间戳，别跟 RTOS 抢时间 */
    HAL_NVIC_SetPriority(EXTI9_5_IRQn, 0x0F, 0);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

    IR_Decoder_Init();   /* 复位解码器状态（ir_decode.c） */
}

/**
  * @brief  EXTI 线 9 中断服务（下降沿 = 一段载波的起点）
  * @note   EXTI9_5 是 5~9 号线共用的中断向量；本工程 PB9 独占，
  *         直接定义向量（stm32f1xx_it.c 无同名函数，无冲突）。
  *         对应原工程 README 的用法：取时间戳 → 喂解码器 → 清标志。
  */
void EXTI9_5_IRQHandler(void)
{
    if (__HAL_GPIO_EXTI_GET_IT(IR_Pin) != RESET)
    {
        __HAL_GPIO_EXTI_CLEAR_IT(IR_Pin);   /* 先清挂起，否则退出立刻重进 */
        IR_Decoder_Process(&ir_decoder, BSP_DWT_GetUs());
    }
}

/**
  * @brief  取红外键并翻译成按键事件码（主循环每 20ms 轮询一次）
  * @retval KEY_EVT_x（bsp_key.h）；无事件返回 KEY_EVT_NONE
  * @note   "输入设备无关"设计的落点：遥控键翻译成与板载按键相同的
  *         事件码，页面框架（GUI_Page_OnKey）零改动。
  *         连发帧（解码器 repeat=1）攒够 IR_LONG_REPEATS 个 ≈ 按住 1s
  *         → 发一次 KEY_EVT_xL（单发锁：一次按住只发一次，与 bsp_key
  *         长按语义一致）。依赖解码器先补完连发识别（ir_decode.c TODO）。
  */
uint8_t IR_GetKeyEvent(void)
{
    static uint8_t s_hold_cnt = 0;    /* 连发帧计数（重建"按住了多久"） */
    static uint8_t s_hold_fired = 0;  /* 本次按住期间长按已发过（单发锁） */
    IR_Data_t data;

    if (!IR_Decoder_GetKey(&ir_decoder, &data))
    {
        return KEY_EVT_NONE;
    }

    if (data.repeat)
    {
        /* 连发帧：不携带数据，键值沿用上一帧 command */
        if (s_hold_fired)
        {
            return KEY_EVT_NONE;   /* 长按已发过，剩余连发全部吞掉 */
        }
        s_hold_cnt++;
        if (s_hold_cnt < IR_LONG_REPEATS)
        {
            return KEY_EVT_NONE;   /* 还没攒够 ≈1s，继续按住 */
        }
        s_hold_fired = 1;
        switch (data.command)
        {
        case IR_KEY_UP:   return KEY_EVT_2L;   /* 长按上 = +1 → 等价 KEY2L */
        case IR_KEY_DOWN: return KEY_EVT_1L;   /* 长按下 = -1 → 等价 KEY1L */
        case IR_KEY_OK:   return KEY_EVT_3L;   /* 长按OK → 等价 KEY3L（设置页=保存） */
        default:          return KEY_EVT_NONE; /* 未映射键的连发不打印，防刷屏 */
        }
    }

    /* 走到这里 = 新数据帧（repeat==0）= 新的一次按键，重置按住状态 */
    s_hold_cnt = 0;
    s_hold_fired = 0;
    switch (data.command)
    {
    case IR_KEY_UP:     return KEY_EVT_2;   /* 上 = +1 → 等价 KEY2（设置页 KEY2:+1） */
    case IR_KEY_DOWN:   return KEY_EVT_1;   /* 下 = -1 → 等价 KEY1（设置页 KEY1:-1） */
    case IR_KEY_OK:     return KEY_EVT_3;   /* OK → 等价 KEY3 */
    default:
        /* 未映射的键（0-9 等）先打印留痕，做数字输入页时直接从这里抄键码 */
        DBG_Printf("IR: unmapped cmd=0x%02X\r\n", data.command);
        return KEY_EVT_NONE;
    }
}
