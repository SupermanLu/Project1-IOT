////////////////////////////////////////////////////////////////////////////////
//本程序只供学习使用，未经作者许可，不得用于其它任何用途
//ILI9488 3.5" 320x480 SPI LCD 综合测试 Demo (STM32F407 HAL 移植版)
//仅包含显示相关测试(不含触摸屏)
//
//@note 本文件不依赖 pic.c(图片数据)。所有"停留 N 毫秒"均使用非阻塞
//      DelayTimer, 主循环调用 run_lcd_tests() 时不阻塞 CPU。
////////////////////////////////////////////////////////////////////////////////
#include "LCD/lcd.h"
#include "delay.h"
#include "GUI.h"
#include "test.h"

//========================variable==========================//
u16 ColorTab[5] = {RED, GREEN, BLUE, YELLOW, BRED};  // 颜色表
//=====================end of variable======================//

/*****************************************************************************
 * @name       :void DrawTestPage(u8 *str)
 * @function   :Drawing test interface
 * @parameters :str: 标题字符串
 * @retvalue   :None
******************************************************************************/
void DrawTestPage(u8 *str)
{
    LCD_Clear(WHITE);
    LCD_Fill(0, 0, lcddev.width, 20, BLUE);
    LCD_Fill(0, lcddev.height - 20, lcddev.width, lcddev.height, BLUE);
    POINT_COLOR = WHITE;
    Gui_StrCenter(0, 2, WHITE, BLUE, str, 16, 1);                     // 居中显示
    Gui_StrCenter(0, lcddev.height - 18, WHITE, BLUE, "http://www.lcdwiki.com", 16, 1);
}

/*****************************************************************************
 * 以下为各个测试画面绘制函数 (仅绘制, 不含延时)
******************************************************************************/
static void page_main(void)
{
    DrawTestPage("QDtech Test Program");
    Gui_StrCenter(0, 30, RED, BLUE, "QDtech", 16, 1);
    Gui_StrCenter(0, 60, RED, BLUE, "Test Program", 16, 1);
    Gui_StrCenter(0, 90, GREEN, BLUE, "3.5\" ILI9488 320X480", 16, 1);
    Gui_StrCenter(0, 120, BLUE, BLUE, "xiaoFeng@QDtech 2018-08-06", 16, 1);
}

static void page_color(void)
{
    LCD_Fill(0, 0, lcddev.width, lcddev.height, RED);
    Show_Str(20, 30, BLUE, YELLOW, "RED ", 16, 1);
}

static void page_rect(void)
{
    u8 i = 0;
    DrawTestPage("Test2: GUI Rectangle");
    LCD_Fill(0, 20, lcddev.width, lcddev.height - 20, WHITE);
    for (i = 0; i < 5; i++)
    {
        POINT_COLOR = ColorTab[i];
        LCD_DrawRectangle(lcddev.width/2 - 80 + (i*15), lcddev.height/2 - 80 + (i*15),
                          lcddev.width/2 - 80 + (i*15) + 60, lcddev.height/2 - 80 + (i*15) + 60);
    }
}

static void page_circle(void)
{
    u8 i = 0;
    DrawTestPage("Test3: GUI Circle");
    LCD_Fill(0, 20, lcddev.width, lcddev.height - 20, WHITE);
    for (i = 0; i < 5; i++)
        gui_circle(lcddev.width/2 - 80 + (i*25), lcddev.height/2 - 50 + (i*25), ColorTab[i], 30, 1);
}

static void page_english(void)
{
    DrawTestPage("Test5: ASCII Font");
    Show_Str(10, 30, BLUE, YELLOW, "8X16:abcdefghijklmnopqrstuvwxyz0123456789", 16, 0);
    Show_Str(10, 45, BLUE, YELLOW, "8X16:ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", 16, 1);
    Show_Str(10, 80, BLUE, YELLOW, "8X16:~!@#$%^&*()_+{}:<>?/|-+.", 16, 0);
}

static void page_triangle(void)
{
    u8 i = 0;
    DrawTestPage("Test4: GUI Triangle");
    LCD_Fill(0, 20, lcddev.width, lcddev.height - 20, WHITE);
    for (i = 0; i < 5; i++)
    {
        POINT_COLOR = ColorTab[i];
        Fill_Triangel(lcddev.width/2 - 80 + (i*20), lcddev.height/2 - 20 + (i*15),
                      lcddev.width/2 - 50 - 1 + (i*20), lcddev.height/2 - 20 - 52 - 1 + (i*15),
                      lcddev.width/2 - 20 - 1 + (i*20), lcddev.height/2 - 20 + (i*15));
    }
}

static void page_english2(void)
{
    DrawTestPage("Test6: ASCII string test");
    Show_Str(10, 30, BLUE, YELLOW, "ASCII 16X16: hello world 1234567890", 16, 0);
    Show_Str(10, 50, BLUE, YELLOW, "ABCxyz +-*/=<>? welcome to LCDWIKI", 16, 0);
}

static void page_shape(void)
{
    /* 用几何图形代替原来的图片测试, 不依赖 pic.c */
    DrawTestPage("Test7: Shapes");
    gui_circle(80,  120, RED,   40, 1);
    gui_circle(160, 120, GREEN, 40, 1);
    gui_circle(240, 120, BLUE,  40, 1);
    LCD_DrawRectangle(40, 200, 280, 260);
}

/*****************************************************************************
 * @name       :void run_lcd_tests(void)
 * @function   : 非阻塞测试主状态机, 由 main() 主循环反复调用。
 *              每个画面停留约 1.5s 后自动翻页, 期间不阻塞 CPU。
 * @retvalue   :None
******************************************************************************/
void run_lcd_tests(void)
{
    static enum {
        ST_MAIN, ST_COLOR, ST_RECT, ST_CIRCLE,
        ST_ENGLISH, ST_TRIANGLE, ST_CHINESE, ST_SHAPE, ST_DONE
    } state = ST_MAIN;
    static DelayTimer wt = {0, 0};
    static uint8_t drawn = 0;

    if (!drawn)
    {
        switch (state)
        {
            case ST_MAIN:     page_main();      break;
            case ST_COLOR:    page_color();     break;
            case ST_RECT:     page_rect();      break;
            case ST_CIRCLE:   page_circle();    break;
            case ST_ENGLISH:  page_english();   break;
            case ST_TRIANGLE: page_triangle();  break;
            case ST_CHINESE:  page_english2();   break;
            case ST_SHAPE:    page_shape();     break;
            default: break;
        }
        delay_start(&wt, 1500);   // 非阻塞: 记录开始时刻, 目标停留 1500ms
        drawn = 1;
    }

    if (delay_expired(&wt))       // 时间到, 翻到下一页
    {
        drawn = 0;
        if (state < ST_DONE) state++;
        else state = ST_MAIN;     // 循环展示
    }
}
