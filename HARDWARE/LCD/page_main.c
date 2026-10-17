/**
  ******************************************************************************
  * @file    page_main.c
  * @brief   仪表盘页：四卡片显示传感器实时数据
  * @note    从 freertos.c 迁入，改成 GuiPage_t 三回调形式。
  *          数据来源改为直接读 g_app（数据与视图分离），
  *          所以 UI_Update 不再需要一长串参数
  ******************************************************************************
  */
#include "lcd.h"
#include "gui.h"
#include "gui_page.h"
#include "bsp_key.h"
#include "app_data.h"

/* ---- UI 布局宏（改布局只改这里） ---- */
#define UI_CARD_BG      LGRAYBLUE          /* 卡片底色 */

/* ============ 内部工具：数值行显示（三步舞：擦 → 变 → 写） ============ */

/**
  * @brief  在 (x,y) 显示一行数值（局部刷新）
  * @param  x, y : 数值左上角坐标（要和标签的 y 对齐）
  * @param  ok   : 传感器本次读取是否成功；0 = 强制显示 "--"
  * @param  text : 已经 snprintf 格式化好的字符串
  * @note   为什么每次都"擦"？LCD 不会自动清除旧字，新字比旧字短时
  *         会留残影（如 "100119 Pa" → "--"），所以先涂底色盖掉旧内容
  */
static void PageMain_ShowValue(u16 x, u16 y, uint8_t ok, const char *text)
{
    /* ① 擦：宽 165 比任何可能的数值都宽；高 15 = 16 号字实际高度 */
    LCD_Fill(x, y, x + 165, y + 15, UI_CARD_BG);

    /* ② 变：失败兜底 —— 不显示旧值假装正常 */
    if (!ok)
    {
        text = "--";
    }

    /* ③ 写：白字 + 卡片底色，16 号字，mode=0 带背景矩形 */
    Show_Str(x, y, WHITE, UI_CARD_BG, (u8*)text, 16, 0);
}

/* ============ 页面回调 ============ */

/**
  * @brief  进入页面：整屏重画（标题栏 + 四卡片 + 固定标签）
  */
static void PageMain_Enter(void)
{
    /* 1. 标题栏：铺 40 高蓝底，居中白字（x=240 是 480 宽屏的中线） */
    LCD_Fill(0, 0, 479, 39, BLUE);
    Gui_StrCenter(240, 12, WHITE, BLUE, (u8*)"Home IoT Node", 16, 0);

    /* 2. DHT11 卡（左上）(0,40)-(239,179)：铺底 + 标题 + 固定标签 */
    LCD_Fill(10, 40, 230, 170, UI_CARD_BG);
    Show_Str(18,  44, WHITE, UI_CARD_BG, (u8*)"DHT11", 16, 0);
    Show_Str(18,  70, WHITE, UI_CARD_BG, (u8*)"T:",    16, 0);
    Show_Str(18,  96, WHITE, UI_CARD_BG, (u8*)"H:",    16, 0);

    /* 3. BMP180 卡（右上）(240,40)-(479,179) */
    LCD_Fill(240, 40, 470, 170, UI_CARD_BG);
    Show_Str(248,  44, WHITE, UI_CARD_BG, (u8*)"BMP180", 16, 0);
    Show_Str(248,  70, WHITE, UI_CARD_BG, (u8*)"P:",     16, 0);

    /* 4. MQ5 卡（左下）(0,180)-(239,319)：等级 + 原始值（诚实显示，不编气体名） */
    LCD_Fill(10, 180, 230, 310, UI_CARD_BG);
    Show_Str(18, 184, WHITE, UI_CARD_BG, (u8*)"MQ5",  16, 0);
    Show_Str(18, 210, WHITE, UI_CARD_BG, (u8*)"Gas:", 16, 0);
    Show_Str(18, 232, WHITE, UI_CARD_BG, (u8*)"RAW:", 16, 0);

    /* 5. LIGHT 卡（右下）(240,180)-(479,319) */
    LCD_Fill(240, 180, 470, 310, UI_CARD_BG);
    Show_Str(248, 184, WHITE, UI_CARD_BG, (u8*)"LIGHT", 16, 0);
    Show_Str(248, 210, WHITE, UI_CARD_BG, (u8*)"L:",    16, 0);

    /* 6. 进入提示：按 KEY3 去设置页 */
    Show_Str(320, 285, YELLOW, UI_CARD_BG, (u8*)"KEY3>Set", 16, 0);
}

/**
  * @brief  周期刷新：只更新数值区（主循环每 20ms 调一次，很轻量）
  */
static void PageMain_Update(void)
{
    char buf[24];
    static uint8_t s_last_seq = 0;   /* 上次画过的数据版本号 */

    /* 节流（坑 44）：本函数每 20ms 被调一次，但数据每 2s 才变一版（seq++）。
     * 无条件重画 = 每秒 50 次"擦+写" → 屏闪。
     * 原则：刷新频率必须等于变化频率 —— seq 没变就整页跳过。
     * seq 每 2s 自增、uint8 每 ~8.5 分钟回绕，回绕只会导致多画一次，无害。 */
    if (g_app_view.seq == s_last_seq)
    {
        return;
    }
    s_last_seq = g_app_view.seq;

    /* --- DHT11 卡（左上）：标签 x=18，数值从 x=60 起 --- */
    if (g_app_view.dht_ok)
    {
        snprintf(buf, sizeof(buf), "%d.%d C", g_app_view.temp_int, g_app_view.temp_dec);
        PageMain_ShowValue(60, 70, 1, buf);   /* y=70 对齐 "T:" 标签 */
        snprintf(buf, sizeof(buf), "%d.%d%%", g_app_view.humi_int, g_app_view.humi_dec);
        PageMain_ShowValue(60, 96, 1, buf);   /* y=96 对齐 "H:" 标签 */
    }
    else
    {
        PageMain_ShowValue(60, 70, 0, "");
        PageMain_ShowValue(60, 96, 0, "");
    }

    /* --- BMP180 卡（右上）：标签 x=248，数值 x=300 --- */
    if (g_app_view.bmp_ok)
    {
        snprintf(buf, sizeof(buf), "%ld Pa", (long)g_app_view.bmp_p);
        PageMain_ShowValue(300, 70, 1, buf);
    }
    else
    {
        PageMain_ShowValue(300, 70, 0, "");
    }

    /* --- MQ5 卡（左下） --- */
    snprintf(buf, sizeof(buf), "%d", g_app_view.mq5_level);
    PageMain_ShowValue(60, 210, 1, buf);
    snprintf(buf, sizeof(buf), "%d", g_app_view.mq5_raw);
    PageMain_ShowValue(60, 232, 1, buf);

    /* --- LIGHT 卡（右下） --- */
    snprintf(buf, sizeof(buf), "%d", g_app_view.light_level);
    PageMain_ShowValue(300, 210, 1, buf);
}

/**
  * @brief  按键：KEY3 进设置页（其余键本页不用，留给报警开关等扩展）
  */
static void PageMain_Key(uint8_t evt)
{
    switch(evt)
    {
      case KEY_EVT_3:
      case KEY_EVT_3L:
        GUI_Page_Goto(PAGE_IDX_SETTING);
        break ;

      default:
        break ;
    }
}

/* 页面实例：登记进 gui_page.c 的页面表 */
const GuiPage_t g_page_main = { "main", PageMain_Enter, PageMain_Update, PageMain_Key };
