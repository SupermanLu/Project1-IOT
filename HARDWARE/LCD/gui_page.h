/**
  ******************************************************************************
  * @file    gui_page.h
  * @brief   多页面管理器（页面表 + 切换 + 事件分发）
  * @note    设计要点：
  *          - 每个页面实现三个回调：on_enter（整屏重画）/ on_update（周期局部
  *            刷新）/ on_key（按键事件）
  *          - 页面切换 = 改下标 + 调新页面的 on_enter
  *          - 页面不互相调用，全部通过管理器跳转——避免页面间耦合
  *          - 输入设备无关：on_key 只认事件码（bsp_key.h），以后红外遥控
  *            解码成同样的事件码即可无缝接入
  ******************************************************************************
  */
#ifndef __GUI_PAGE_H
#define __GUI_PAGE_H

#include <stdint.h>

/* 页面描述符：每个页面提供一个全局实例（见 page_main.c / page_setting.c） */
typedef struct
{
    const char *name;                  /* 页面名（调试用） */
    void (*on_enter)(void);            /* 进入页面：整屏重画一次 */
    void (*on_update)(void);           /* 周期调用：只刷新变化的数据 */
    void (*on_key)(uint8_t evt);       /* 按键事件（KEY_EVT_x，见 bsp_key.h） */
} GuiPage_t;

/* 页面索引（页面表顺序，见 gui_page.c） */
#define PAGE_IDX_MAIN     0
#define PAGE_IDX_SETTING  1

void GUI_Page_Init(void);              /* 启动：进入初始页面 */
void GUI_Page_Goto(uint8_t idx);       /* 切换页面 */
void GUI_Page_OnKey(uint8_t evt);      /* 分发按键事件给当前页面 */
void GUI_Page_Update(void);            /* 周期刷新当前页面 */

#endif /* __GUI_PAGE_H */
