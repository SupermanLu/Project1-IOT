/**
  ******************************************************************************
  * @file    page_setting.c
  * @brief   设置页：光标选择阈值项，KEY1 减值 / KEY2 加值，KEY3 长按保存
  * @note    交互约定（3 键方案）：
  *            KEY1 = 当前项 -1（0 减到底则回卷到上限）
  *            KEY2 = 当前项 +1（到上限回 0）
  *            KEY3 短按 = 光标下移（循环）
  *            KEY3 长按 = 保存到 Flash 并返回仪表盘
  *          本页 on_update 是空的：设置时不被 2s 定时刷新打扰，
  *          只有按键才触发重画——这就是"操作页暂停自动刷新"
  ******************************************************************************
  */
#include <stdio.h>
#include "lcd.h"
#include "gui.h"
#include "gui_page.h"
#include "bsp_key.h"
#include "app_data.h"

/* ---- 布局宏 ---- */
#define SET_BG          GRAY               /* 设置页底色 */
#define SET_SEL_BG      YELLOW             /* 选中项底色（高亮） */
#define SET_SEL_FC      BLACK              /* 选中项字色 */
#define SET_ROW_Y0      60                 /* 第一行 y */
#define SET_ROW_H       30                 /* 行距（16 号字 + 空隙） */
#define SET_ITEM_X      40                 /* 文字 x 起点 */
#define SET_ITEM_W      400                /* 行宽（擦除/高亮条宽） */
#define SET_VAL_X       (SET_ITEM_X + 130)/* 数值区起点：跳过最长的 "Light Level : " */

/* 阈值项定义表：名字 + 对应 g_set 字段 + 可调下限/上限 + 步进 */
typedef struct
{
    const char *name;
    uint16_t   *p_val;                 /* 指向 g_set 的成员（用指针统一读写） */
    uint16_t    min;                   /* KEY1 减过此值 → 回卷到 max */
    uint16_t    max;                   /* KEY2 加过此值 → 回卷到 min */
    uint16_t    step;                  /* 每次按键的步进 */
} SetItem_t;

static const SetItem_t s_items[] =
{
    { "Temp Max",   &g_set.temp_max,   0,                 SET_TEMP_MAX_HI,  1 },
    { "Humi Max",   &g_set.humi_max,   0,                 SET_LEVEL_MAX_HI, 1 },
    { "Gas Level",  &g_set.gas_max,    0,                 SET_LEVEL_MAX_HI, 1 },
    { "Light Level",&g_set.light_max,  0,                 SET_LEVEL_MAX_HI, 1 },
    { "Sample ms",  &g_set.sample_ms,  SET_SAMPLE_MS_LO,  SET_SAMPLE_MS_HI, SET_SAMPLE_MS_STEP },
};
#define SET_ITEM_COUNT  (sizeof(s_items) / sizeof(s_items[0]))

static uint8_t s_cursor = 0;            /* 当前选中行 */

/* ============ 内部工具 ============ */


/* 取一行当前配色（选中=黑字黄底，普通=白字灰底）——两个绘制函数共用 */
static void Setting_RowColor(uint8_t i, u16 *fc, u16 *bc)
{
    *fc = (i == s_cursor) ? SET_SEL_FC : WHITE;
    *bc = (i == s_cursor) ? SET_SEL_BG : SET_BG;
}

/* 名字区：整行铺底色（含高亮）+ 名字和冒号。只在 Enter / 光标移动时调用 */
static void Setting_DrawRowName(uint8_t i)
{
    char buf[24];
    u16 fc, bc;

    Setting_RowColor(i, &fc, &bc);

    /* 整行铺底 = 擦除 + 高亮（数值区被顺带擦掉，随后 DrawRowValue 补上） */
    LCD_Fill(SET_ITEM_X, SET_ROW_Y0 + i * SET_ROW_H,
             SET_ITEM_X + SET_ITEM_W, SET_ROW_Y0 + i * SET_ROW_H + 23, bc);

    snprintf(buf, sizeof(buf), "%s :", s_items[i].name);   /* 冒号归名字区 */
    Show_Str(SET_ITEM_X + 8, SET_ROW_Y0 + i * SET_ROW_H + 4,
             fc, bc, (u8*)buf, 16, 0);
}

/* 数值区：只擦 SET_VAL_X 到行尾这一小段 + 只写数字。KEY1/2（含连发）只调我 */
static void Setting_DrawRowValue(uint8_t i)
{
    char buf[12];
    u16 fc, bc;

    Setting_RowColor(i, &fc, &bc);

    /* 只擦数值段——名字区的像素一个都不碰，这就是不闪的原因 */
    LCD_Fill(SET_VAL_X, SET_ROW_Y0 + i * SET_ROW_H,
             SET_ITEM_X + SET_ITEM_W, SET_ROW_Y0 + i * SET_ROW_H + 23, bc);

    snprintf(buf, sizeof(buf), "%d", (int)(*s_items[i].p_val));
    Show_Str(SET_VAL_X, SET_ROW_Y0 + i * SET_ROW_H + 4,
             fc, bc, (u8*)buf, 16, 0);
}

/* 组合版：整行重画。原函数改名升级，老调用点（Enter / KEY_EVT_3）不用动 */
static void Setting_DrawRow(uint8_t i)
{
    Setting_DrawRowName(i);
    Setting_DrawRowValue(i);
}

/* ============ 页面回调 ============ */

static void PageSetting_Enter(void)
{
    uint8_t i;

    /* 标题栏 */
    LCD_Fill(0, 0, 479, 39, BLUE);
    Gui_StrCenter(240, 12, WHITE, BLUE, (u8*)"Settings", 16, 0);

    /* 底色 */
    LCD_Fill(0, 40, 479, 319, SET_BG);

    /* ⑤行阈值项 */
    for (i = 0; i < SET_ITEM_COUNT; i++)
    {
        Setting_DrawRow(i);
    }

    /* 底部操作提示 */
    Show_Str(40,  285, WHITE, SET_BG, (u8*)"KEY1:-1", 16, 0);
    Show_Str(150, 285, WHITE, SET_BG, (u8*)"KEY2:+1",  16, 0);
    Show_Str(260, 285, WHITE, SET_BG, (u8*)"KEY3:Next", 16, 0);
    Show_Str(360, 285, GREEN, SET_BG, (u8*)"LONG:Save", 16, 0);
}

static void PageSetting_Update(void)
{
    /* 故意留空：设置页不自动刷新（见文件头注释） */
}

static void PageSetting_Key(uint8_t evt)
{
    uint8_t i;

    switch (evt)
    {
        case KEY_EVT_1:     /* 当前项 -step；减过下限 → 回卷到上限 */
        {
            int32_t v = (int32_t)(*s_items[s_cursor].p_val) - (int32_t)s_items[s_cursor].step;
            if (v < (int32_t)s_items[s_cursor].min)
            {
                v = (int32_t)s_items[s_cursor].max;
            }
            *s_items[s_cursor].p_val = (uint16_t)v;
            Setting_DrawRowValue(s_cursor);
            break;
        }

        case KEY_EVT_2:     /* 当前项 +step；加过上限 → 回卷到下限 */
        {
            int32_t v = (int32_t)(*s_items[s_cursor].p_val) + (int32_t)s_items[s_cursor].step;
            if (v > (int32_t)s_items[s_cursor].max)
            {
                v = (int32_t)s_items[s_cursor].min;
            }
            *s_items[s_cursor].p_val = (uint16_t)v;
            Setting_DrawRowValue(s_cursor);
            break;
        }

        case KEY_EVT_3:     /* 短按：光标下移（循环）：旧行恢复普通色，新行高亮 */
            i = s_cursor;
            s_cursor = (uint8_t)((s_cursor + 1) % SET_ITEM_COUNT);
            Setting_DrawRow(i);
            Setting_DrawRow(s_cursor);
            break;

        case KEY_EVT_3L:    /* 长按：保存 → 返回仪表盘 */
            if (Setting_Save() != 0)
            {
                Show_Str(SET_ITEM_X + 8, 260, RED, SET_BG,
                        (u8*)"Save FAIL!", 16, 0);
            }
            else
            {
                GUI_Page_Goto(PAGE_IDX_MAIN);
            }
            break;

            default:
                break;
    }
}

/* 页面实例 */
const GuiPage_t g_page_setting = { "setting", PageSetting_Enter, PageSetting_Update, PageSetting_Key };
