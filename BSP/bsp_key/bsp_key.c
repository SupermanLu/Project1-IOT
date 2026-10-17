/**
  ******************************************************************************
  * @file    bsp_key.c
  * @brief   按键轮询扫描实现
  * @note    引脚宏复用 CubeMX 生成的 main.h（KEY1_Pin 等），不重复定义
  ******************************************************************************
  */
#include "bsp_key.h"
#include "main.h"

/**
  * @brief  扫描一次按键，输出短按/长按事件
  * @retval KEY_EVT_x（短按）或 KEY_EVT_xL（长按），无事件返回 KEY_EVT_NONE
  *
  * 三态状态机：
  *   按下沿（s_last==无 && cur==按下） → 只记键号+时间戳，不发事件
  *   按住且距按下 >= 1s               → 发一次长按事件（s_long 保证只发一次）
  *   松手且长按从未触发               → 发短按事件（"点击"要等松手才能定性）
  *   松手                             → 无条件清状态
  * 消抖：调用周期 >= 20ms，抖动毛刺（<5ms）在一次采样里已被滤掉
  */
uint8_t KEY_Scan(void)
{
    static uint8_t s_last = KEY_EVT_NONE;   /* 上次采样到的按键状态 */
    static uint32_t s_t0 = 0;  /*按下时间戳*/
    static uint8_t s_long = 0;  /* 长按事件时间标志位*/
    static uint32_t s_repT0 = 0;  /* 重复按时间戳*/
    uint32_t now = HAL_GetTick();   /* 当前时间戳 */
    uint8_t cur = KEY_EVT_NONE;
    uint8_t evt = KEY_EVT_NONE;

    /* 低电平 = 按下（上拉输入，按下接地）。else-if 互斥：只认优先级最高的那个 */
    if (HAL_GPIO_ReadPin(KEY1_GPIO_Port, KEY1_Pin) == GPIO_PIN_RESET)
    {
        cur = KEY_EVT_1;
    }
    else if (HAL_GPIO_ReadPin(KEY2_GPIO_Port, KEY2_Pin) == GPIO_PIN_RESET)
    {
        cur = KEY_EVT_2;
    }
    else if (HAL_GPIO_ReadPin(KEY3_GPIO_Port, KEY3_Pin) == GPIO_PIN_RESET)
    {
        cur = KEY_EVT_3;
    }

    /* 长按检测: 按下 + 时间 > 1s*/
    if(cur != KEY_EVT_NONE) /*检测按键是否按下*/
    { 
       if(s_last == KEY_EVT_NONE) /*上次没有按下*/
       {
          s_last = cur; /*记录当前按下按键*/
          s_t0 = now; /*记录按下时间戳*/
          s_long = 0; /*重置长按事件时间标志位*/
       }
       else if(s_long == 0 && (now - s_t0) >= KEY_LONG_MS) /*当前按下时间超过1s*/
       {
          s_long = 1; /*长按事件触发*/
          s_repT0 = now; /*连发节拍从长按触发时刻算起*/
          evt = (uint8_t)(cur + 3); /* 长按事件码 = 普通事件码 + 3 */
       }
       else if(s_long == 1 &&          /*新分支: 长按后持续连发*/
                cur != KEY_EVT_3 &&   /* 3 不连发 (保存语义)*/
              (now - s_repT0) >= KEY_REPEAT_MS)
      {
        s_repT0 = now;
        evt = cur;                    /*连发短按码 , 页面按次数处理*/
      }
    }
    else /*当前松手*/
    {
      if(s_last != KEY_EVT_NONE && s_long == 0)
      {
        /*发送短按事件*/
        evt = s_last;
       
      }
       s_last = KEY_EVT_NONE; /*重置上次采样到的按键状态*/
    }

    return evt;
}
