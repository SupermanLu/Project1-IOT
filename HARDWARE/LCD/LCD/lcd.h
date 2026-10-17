/**
 * @file    lcd.h
 * @brief   ILI9488 LCD 驱动头文件 (STM32F407 HAL 移植版)
 *
 * @note    引脚 (已由 CubeMX 初始化为 GPIO_Output, SPI1 模式0):
 *          LED  = PA1   (背光控制)
 *          RST  = PA2   (硬件复位)
 *          DC   = PA3   (数据/命令选择)
 *          CS   = PA4   (片选, 软件控制)
 *          MOSI = PA7   (SPI1_MOSI, 由 SPI 驱动管理)
 *          SCK  = PA5   (SPI1_SCK,   由 SPI 驱动管理)
 *
 *          如果你的实际接线不同, 只需修改下方 LCD_xxx_PORT / LCD_xxx_PIN 即可。
 */

#ifndef __LCD_H
#define __LCD_H

#include "stm32f1xx_hal.h"
#include "../sys.h"

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * LCD 尺寸 (ILI9488: 320x480)
 *===========================================================================*/
#define LCD_W   320
#define LCD_H   480

/*===========================================================================
 * 显示方向 (0=竖屏, 1=横屏, 2=竖屏180°, 3=横屏270°)
 *===========================================================================*/
#define USE_HORIZONTAL  1

/*===========================================================================
 * LCD 控制引脚定义 — 按你的 STM32F407 接线修改这里
 *    端口宏写 GPIOx, 引脚宏写 GPIO_PIN_x
 *===========================================================================*/
#define LCD_LED_PORT    GPIOA
#define LCD_LED_PIN     GPIO_PIN_0

#define LCD_RST_PORT    GPIOA
#define LCD_RST_PIN     GPIO_PIN_4   /* LCD_REST = PA4（PA5 是 SPI1_SCK，不能用！） */

#define LCD_DC_PORT     GPIOB
#define LCD_DC_PIN      GPIO_PIN_8

#define LCD_CS_PORT     GPIOB
#define LCD_CS_PIN      GPIO_PIN_1

/*===========================================================================
 * GPIO 操作宏 (STM32 HAL)
 *===========================================================================*/
#define LCD_CS_SET()    HAL_GPIO_WritePin(LCD_CS_PORT,  LCD_CS_PIN,  GPIO_PIN_SET)
#define LCD_CS_CLR()    HAL_GPIO_WritePin(LCD_CS_PORT,  LCD_CS_PIN,  GPIO_PIN_RESET)
#define LCD_DC_SET()    HAL_GPIO_WritePin(LCD_DC_PORT,  LCD_DC_PIN,  GPIO_PIN_SET)
#define LCD_DC_CLR()    HAL_GPIO_WritePin(LCD_DC_PORT,  LCD_DC_PIN,  GPIO_PIN_RESET)
#define LCD_RST_SET()   HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_SET)
#define LCD_RST_CLR()   HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_RESET)
// 背光为高电平有效: 拉高点亮, 拉低熄灭
#define LCD_LED_ON()    HAL_GPIO_WritePin(LCD_LED_PORT, LCD_LED_PIN, GPIO_PIN_SET)
#define LCD_LED_OFF()   HAL_GPIO_WritePin(LCD_LED_PORT, LCD_LED_PIN, GPIO_PIN_RESET)

/*===========================================================================
 * 颜色常量 (RGB565)
 *===========================================================================*/
#define WHITE       0xFFFF
#define BLACK       0x0000
#define BLUE        0x001F
#define BRED        0xF81F
#define GRED        0xFFE0
#define GBLUE       0x07FF
#define RED         0xF800
#define MAGENTA     0xF81F
#define GREEN       0x07E0
#define CYAN        0x7FFF
#define YELLOW      0xFFE0
#define BROWN       0xBC40
#define BRRED       0xFC07
#define GRAY        0x8430
#define DARKBLUE    0x01CF
#define LIGHTBLUE   0x7D7C
#define GRAYBLUE    0x5458
#define LIGHTGREEN  0x841F
#define LIGHTGRAY   0xEF5B
#define LGRAY       0xC618
#define LGRAYBLUE   0xA651
#define LBBLUE      0x2B12

/*===========================================================================
 * LCD 参数结构体
 *===========================================================================*/
typedef struct
{
    uint16_t width;
    uint16_t height;
    uint16_t id;
    uint8_t  dir;
    uint16_t wramcmd;
    uint16_t setxcmd;
    uint16_t setycmd;
} _lcd_dev;

/*===========================================================================
 * 全局变量声明
 *===========================================================================*/
extern _lcd_dev lcddev;
extern u16 POINT_COLOR;
extern u16 BACK_COLOR;

/*===========================================================================
 * 函数声明
 *===========================================================================*/
void LCD_Init(void);
void LCD_DisplayOn(void);
void LCD_DisplayOff(void);
void LCD_Clear(u16 Color);
void LCD_SetCursor(u16 Xpos, u16 Ypos);
void LCD_DrawPoint(u16 x, u16 y);
u16 LCD_ReadPoint(u16 x, u16 y);
void LCD_DrawLine(u16 x1, u16 y1, u16 x2, u16 y2);
void LCD_DrawRectangle(u16 x1, u16 y1, u16 x2, u16 y2);
void LCD_SetWindows(u16 xStar, u16 yStar, u16 xEnd, u16 yEnd);
u16 LCD_RD_DATA(void);
void LCD_WriteReg(u8 LCD_Reg, u16 LCD_RegValue);
void LCD_WR_DATA(u8 data);
u16 LCD_ReadReg(u8 LCD_Reg);
void LCD_WriteRAM_Prepare(void);
void LCD_WriteRAM(u16 RGB_Code);
u16 LCD_ReadRAM(void);
u16 LCD_BGR2RGB(u16 c);
void LCD_WriteData_16Bit(u16 Data);
void LCD_direction(u8 direction);

#ifdef __cplusplus
}
#endif

#endif /* __LCD_H */
