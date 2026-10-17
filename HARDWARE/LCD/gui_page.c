/**
  ******************************************************************************
  * @file    gui_page.c
  * @brief   多页面管理器实现
  * @note    新增页面的步骤：
  *          1. 写 page_xxx.c，提供 const GuiPage_t g_page_xxx
  *          2. 本文件 extern 声明 + 加进 s_pages[] 表
  *          3. gui_page.h 加页面索引宏
  *          管理器代码一行都不用改——这就是"对扩展开放"的页面表设计
  ******************************************************************************
  */
#include "gui_page.h"
#include "bsp_key.h"
#include "lcd.h"

/* 外部页面实例 */
extern const GuiPage_t g_page_main;
extern const GuiPage_t g_page_setting;

/* 页面表：新增页面在这里登记 */
static const GuiPage_t *const s_pages[] =
{
    &g_page_main,       /* PAGE_IDX_MAIN    仪表盘 */
    &g_page_setting,    /* PAGE_IDX_SETTING 设置页 */
};
#define PAGE_COUNT  (sizeof(s_pages) / sizeof(s_pages[0]))

static uint8_t s_cur = PAGE_IDX_MAIN;   /* 当前页面下标 */

void GUI_Page_Init(void)
{
    s_cur = PAGE_IDX_MAIN;
    s_pages[s_cur]->on_enter();         /* 画第一屏 */
}

void GUI_Page_Goto(uint8_t idx)
{
    if (idx == s_cur)                   /* 已在目标页，不重画 */
    {
        return;
    }
    if (idx >= PAGE_COUNT)              /* 越界保护 */
    {
        return;
    }
    s_cur = idx;
    /* 框架负责整屏清底：页面只画自己的内容，绝不背上一个页面的残留。
     * 这条保证写在框架里，新页面"忘了涂满全屏"也不会出残留 bug */
    LCD_Fill(0, 0, 479, 319, BLACK);
    s_pages[s_cur]->on_enter();
}

void GUI_Page_OnKey(uint8_t evt)
{
    if (evt == KEY_EVT_NONE)            /* 无事件直接返回，别打扰页面 */
    {
        return;
    }
    s_pages[s_cur]->on_key(evt);
}

void GUI_Page_Update(void)
{
    s_pages[s_cur]->on_update();
}
