/**
 * @file    lcd.c
 * @brief   ILI9488 LCD 驱动实现 (STM32F407 HAL 移植版)
 *
 * @note    本驱动基于 QDtech ILI9488 3.5寸 TFT LCD 模块
 *          通信方式: 4 线 SPI (硬件 SPI1)
 *          延时使用 HAL_Delay() (基于 SysTick, 1ms 精度)
 *          引脚 (CS/RST/DC/LED) 的 GPIO 方向已由 CubeMX 初始化为 Output。
 */

#include "lcd.h"
#include "../SPI/SPI.h"
#include "../sys.h"

/*===========================================================================
 * 毫秒延时 — 使用 HAL 库提供的 HAL_Delay()
 *===========================================================================*/
#define delay_ms(ms)    HAL_Delay((ms))

/*===========================================================================
 * LCD_GPIOInit() - 设置 LCD 控制引脚初始状态
 *
 * 注意: GPIO 输出模式已由 CubeMX 的 MX_GPIO_Init() 完成 (PA1~PA4 = Output),
 *       此处只需设置初始电平.
 *===========================================================================*/
void LCD_GPIOInit(void)
{
    LCD_CS_CLR();   // 片选默认拉低 (选中)
    LCD_RST_CLR();  // 复位默认拉低
    LCD_DC_CLR();   // 数据/命令默认拉低 (命令)
    LCD_LED_ON();   // 背光默认打开
}

/*===========================================================================
 * 向 LCD 写 8 位命令
 *===========================================================================*/
void LCD_WR_REG(u8 data)
{
    LCD_DC_CLR();           // 命令 (CS 全程已由 LCD_GPIOInit 拉低, 此处不再翻转)
    SPIv_WriteData(data);
}

/*===========================================================================
 * 向 LCD 写 8 位数据
 *===========================================================================*/
void LCD_WR_DATA(u8 data)
{
    LCD_DC_SET();           // 数据
    SPIv_WriteData(data);
}

/*===========================================================================
 * 写像素数据 (ILI9488 SPI  ics: 18bit/RGB666, 每个像素发 3 字节)
 *   输入 data 为 RGB565。ILI9488 18bit 模式: 每个分量占传输字节的高 6 位。
 *===========================================================================*/
void LCD_WriteData_16Bit(u16 data)
{
    u8 r = (data >> 11) & 0x1F;   // 红 5bit
    u8 g = (data >> 5)  & 0x3F;   // 绿 6bit
    u8 b = (data)       & 0x1F;   // 蓝 5bit
    // RGB565 的 5bit 分量扩展到 6bit 满量程 (5->6)
    r = (r << 1) | (r >> 4);
    b = (b << 1) | (b >> 4);
    // ILI9488 18bit 模式: 每个分量占传输字节的高 6 位, 低 2 位补 0
    r <<= 2;
    g <<= 2;
    b <<= 2;
    LCD_DC_SET();
    SPIv_WriteData(b);   // 屏幕期望 BGR 物理顺序: 先 B
    SPIv_WriteData(g);   // 再 G
    SPIv_WriteData(r);   // 后 R
}

/*===========================================================================
 * 写寄存器 (ILI9488)
 *===========================================================================*/
void LCD_WriteReg(u8 LCD_Reg, u16 LCD_RegValue)
{
    LCD_WR_REG(LCD_Reg);
    LCD_WR_DATA(LCD_RegValue);
}

/*===========================================================================
 * 准备写 GRAM
 *===========================================================================*/
void LCD_WriteRAM_Prepare(void)
{
    LCD_WR_REG(lcddev.wramcmd);
}

/*===========================================================================
 * 写 GRAM (16bit RGB565 -> ILI9488 需要 18bit: R/G/B 各 6bit 或 8bit)
 * 这里和原驱动一致: 用 Lcd_WriteData_16Bit() 发出 3 字节 (每个分量 8bit)
 *===========================================================================*/
void LCD_WriteRAM(u16 RGB_Code)
{
    LCD_WriteData_16Bit(RGB_Code);
}

/*===========================================================================
 * 读取数据 (屏一般不发送, 留作兼容)
 *===========================================================================*/
u16 LCD_RD_DATA(void)
{
    u16 ram;
    ram = LCD_ReadRAM();
    return ram;
}

/*===========================================================================
 * 读寄存器
 *===========================================================================*/
u16 LCD_ReadReg(u8 LCD_Reg)
{
    u16 t;
    LCD_WR_REG(LCD_Reg);
    t = LCD_ReadRAM();
    return t;
}

/*===========================================================================
 * 读 GRAM (留作兼容, 实际 ILI9488 读需额外处理, 一般不读)
 *===========================================================================*/
u16 LCD_ReadRAM(void)
{
    return 0;
}

/*===========================================================================
 * 设置显示窗口
 *===========================================================================*/
void LCD_SetWindows(u16 xStar, u16 yStar, u16 xEnd, u16 yEnd)
{
    LCD_WR_REG(lcddev.setxcmd);
    LCD_WR_DATA(xStar >> 8);
    LCD_WR_DATA(0x00FF & xStar);
    LCD_WR_DATA(xEnd >> 8);
    LCD_WR_DATA(0x00FF & xEnd);

    LCD_WR_REG(lcddev.setycmd);
    LCD_WR_DATA(yStar >> 8);
    LCD_WR_DATA(0x00FF & yStar);
    LCD_WR_DATA(yEnd >> 8);
    LCD_WR_DATA(0x00FF & yEnd);

    LCD_WriteRAM_Prepare();
}

/*===========================================================================
 * 设置光标
 *===========================================================================*/
void LCD_SetCursor(u16 Xpos, u16 Ypos)
{
    LCD_SetWindows(Xpos, Ypos, Xpos, Ypos);
}

/*===========================================================================
 * 清屏
 *===========================================================================*/
void LCD_Clear(u16 Color)
{
    u32 i;
    u32 total = (u32)lcddev.width * lcddev.height;

    LCD_SetWindows(0, 0, lcddev.width - 1, lcddev.height - 1);
    LCD_DC_SET();
    for (i = 0; i < total; i++)
    {
        LCD_WriteData_16Bit(Color);   // 18bit RGB666, 3 字节/像素
    }
}

/*===========================================================================
 * 画点
 *===========================================================================*/
void LCD_DrawPoint(u16 x, u16 y)
{
    LCD_SetCursor(x, y);
    LCD_WriteRAM_Prepare();
    LCD_WriteRAM(POINT_COLOR);
}

/*===========================================================================
 * 读点 (ILI9488 读需额外协议, 此处返回当前 POINT_COLOR 作为兼容占位)
 *=========================================================================== */
u16 LCD_ReadPoint(u16 x, u16 y)
{
    return POINT_COLOR;
}

/*===========================================================================
 * 画横线 / 竖线 / 任意线
 *===========================================================================*/
void LCD_DrawLine(u16 x1, u16 y1, u16 x2, u16 y2)
{
    u16 t;
    int xerr = 0, yerr = 0, delta_x, delta_y, distance;
    int incx, incy, uRow, uCol;

    delta_x = x2 - x1;
    delta_y = y2 - y1;
    uRow = x1;
    uCol = y1;

    if (delta_x > 0) incx = 1;
    else if (delta_x == 0) incx = 0;
    else { incx = -1; delta_x = -delta_x; }

    if (delta_y > 0) incy = 1;
    else if (delta_y == 0) incy = 0;
    else { incy = -1; delta_y = -delta_y; }

    if (delta_x > delta_y) distance = delta_x;
    else distance = delta_y;

    for (t = 0; t <= distance + 1; t++)
    {
        LCD_DrawPoint(uRow, uCol);
        xerr += delta_x;
        yerr += delta_y;
        if (xerr > distance)
        {
            xerr -= distance;
            uRow += incx;
        }
        if (yerr > distance)
        {
            yerr -= distance;
            uCol += incy;
        }
    }
}

/*===========================================================================
 * 画矩形
 *=========================================================================== */
void LCD_DrawRectangle(u16 x1, u16 y1, u16 x2, u16 y2)
{
    LCD_DrawLine(x1, y1, x2, y1);
    LCD_DrawLine(x1, y1, x1, y2);
    LCD_DrawLine(x1, y2, x2, y2);
    LCD_DrawLine(x2, y1, x2, y2);
}

/*===========================================================================
 * 方向设置
 *=========================================================================== */
void LCD_direction(u8 direction)
{
    lcddev.setxcmd = 0x2A;
    lcddev.setycmd = 0x2B;
    lcddev.wramcmd = 0x2C;
    switch (direction)
    {
        case 0:
            lcddev.dir = 0;
            lcddev.width  = LCD_W;
            lcddev.height = LCD_H;
            LCD_WriteReg(0x36, (1 << 6) | (0 << 7));   // MX=1 翻转(解字体镜像), BGR=0(硬件不做交换, 发送顺序已是BGR)
            break;
        case 1:
            lcddev.dir = 1;
            lcddev.width  = LCD_H;
            lcddev.height = LCD_W;
            /* MADCTL: bit7=MY bit6=MX bit5=MV(行列交换=横屏关键位) */
            LCD_WriteReg(0x36, (1 << 5) | (1 << 6) | (1 << 7));   /* MV=1 横屏 */
            break;
        case 2:
            lcddev.dir = 2;
            lcddev.width  = LCD_W;
            lcddev.height = LCD_H;
            LCD_WriteReg(0x36, (1 << 6) | (0 << 7));
            break;
        case 3:
            lcddev.dir = 3;
            lcddev.width  = LCD_H;
            lcddev.height = LCD_W;
            LCD_WriteReg(0x36, (1 << 5) | (1 << 7));   /* MV=1 + MY=1 横屏180° */
            break;
        default:
            break;
    }
}

/*===========================================================================
 * 全局变量
 *=========================================================================== */
_lcd_dev lcddev;
u16 POINT_COLOR = 0x0000;
u16 BACK_COLOR  = 0xFFFF;

/*===========================================================================
 * LCD 初始化 (ILI9488)
 *=========================================================================== */
void LCD_Init(void)
{
    SPI_Init();
    LCD_GPIOInit();
    LCD_LED_ON();          // 打开背光

    // 硬件复位
    LCD_RST_CLR();
    delay_ms(20);
    LCD_RST_SET();
    delay_ms(20);

    // ---- ILI9488 初始化序列 ----
    LCD_WR_REG(0xE0);      // PGC
    LCD_WR_DATA(0x00);
    LCD_WR_DATA(0x03);
    LCD_WR_DATA(0x09);
    LCD_WR_DATA(0x08);
    LCD_WR_DATA(0x16);
    LCD_WR_DATA(0x0A);
    LCD_WR_DATA(0x3F);
    LCD_WR_DATA(0x78);
    LCD_WR_DATA(0x4C);
    LCD_WR_DATA(0x09);
    LCD_WR_DATA(0x0A);
    LCD_WR_DATA(0x08);
    LCD_WR_DATA(0x16);
    LCD_WR_DATA(0x1A);
    LCD_WR_DATA(0x0F);

    LCD_WR_REG(0xE1);      // NGC
    LCD_WR_DATA(0x00);
    LCD_WR_DATA(0x16);
    LCD_WR_DATA(0x19);
    LCD_WR_DATA(0x03);
    LCD_WR_DATA(0x0F);
    LCD_WR_DATA(0x05);
    LCD_WR_DATA(0x32);
    LCD_WR_DATA(0x45);
    LCD_WR_DATA(0x46);
    LCD_WR_DATA(0x04);
    LCD_WR_DATA(0x11);
    LCD_WR_DATA(0x0D);
    LCD_WR_DATA(0x35);
    LCD_WR_DATA(0x37);
    LCD_WR_DATA(0x0F);

    LCD_WR_REG(0xC0);      // Power Control 1
    LCD_WR_DATA(0x17);
    LCD_WR_DATA(0x15);

    LCD_WR_REG(0xC1);      // Power Control 2
    LCD_WR_DATA(0x41);

    LCD_WR_REG(0xC5);      // VCOM
    LCD_WR_DATA(0x00);
    LCD_WR_DATA(0x12);
    LCD_WR_DATA(0x80);

    LCD_WR_REG(0x36);      // Memory Access
    LCD_WR_DATA(0x48);

    LCD_WR_REG(0x3A);      // 像素格式
    // ILI9488 SPI 模式实际只支持 18bit(0x66); 每个像素发 3 字节(RGB666)
    LCD_WR_DATA(0x66);

    LCD_WR_REG(0xB0);      // 接口模式
    LCD_WR_DATA(0x00);

    LCD_WR_REG(0xB1);      // Frame rate (70Hz)
    LCD_WR_DATA(0xB0);
    LCD_WR_DATA(0x11);

    LCD_WR_REG(0xB4);      // Display Inversion
    LCD_WR_DATA(0x02);

    LCD_WR_REG(0xB6);      // Display Function Control
    LCD_WR_DATA(0x02);
    LCD_WR_DATA(0x02);

    LCD_WR_REG(0xE9);      // Set Image Function
    LCD_WR_DATA(0x00);

    LCD_WR_REG(0xF7);      // Adjust Control 3
    LCD_WR_DATA(0xA9);
    LCD_WR_DATA(0x51);
    LCD_WR_DATA(0x2C);
    LCD_WR_DATA(0x82);

    LCD_WR_REG(0x11);      // 退出睡眠
    delay_ms(120);

    LCD_WR_REG(0x29);      // 开启显示
    LCD_direction(USE_HORIZONTAL);
    LCD_Clear(WHITE);
}

/*===========================================================================
 * 显示开/关
 *=========================================================================== */
void LCD_DisplayOn(void)
{
    LCD_WR_REG(0x29);
}

void LCD_DisplayOff(void)
{
    LCD_WR_REG(0x28);
}
