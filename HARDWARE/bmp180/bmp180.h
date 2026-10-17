/**
  ******************************************************************************
  * @file    bmp180.h
  * @brief   BMP180 气压/温度传感器驱动（I2C2，地址 0x77）
  * @note    - 依赖 HAL I2C2（PB10=SCL, PB11=SDA）与 bsp_delay
  *          - 上电必须先读校准系数，才能换算真实值
  *          - 温度单位 0.1°C；气压单位 Pa（100000Pa = 1000hPa）
  ******************************************************************************
  */
#ifndef __BMP180_H
#define __BMP180_H

#include <stdint.h>

/* 7 位 I2C 从机地址 */
#define BMP180_I2C_ADDR    (0x77)

/* 寄存器地址 */
#define BMP180_REG_ID      0xD0    /* 芯片 ID，应=0x55 */
#define BMP180_REG_CAL     0xAA    /* 校准系数区起始地址 */
#define BMP180_REG_CTRL    0xF4    /* 控制寄存器：写转换命令 */
#define BMP180_REG_DATA    0xF6    /* 转换结果 MSB */

/* 转换命令（写进 CTRL 寄存器） */
#define BMP180_CMD_TEMP    0x2E    /* 温度转换，等待 4.5ms */
#define BMP180_CMD_PRES_0  0x34    /* 气压 oss=0，等待 4.5ms */
#define BMP180_CMD_PRES_1  0x74    /* 气压 oss=1，等待 7.5ms */
#define BMP180_CMD_PRES_2  0xB4    /* 气压 oss=2，等待 13.5ms */
#define BMP180_CMD_PRES_3  0xF4    /* 气压 oss=3，等待 25.5ms */

/* 11 个校准系数（注意符号：AC1~AC3/B1/B2/MB/MC/MD 有符号，AC4~AC6 无符号） */
typedef struct
{
    int16_t  AC1;
    int16_t  AC2;
    int16_t  AC3;
    uint16_t AC4;
    uint16_t AC5;
    uint16_t AC6;
    int16_t  B1;
    int16_t  B2;
    int16_t  MB;
    int16_t  MC;
    int16_t  MD;
} BMP180_Calib_t;

/**
  * @brief  初始化：校验芯片 ID + 读取全部校准系数
  * @retval 0=成功；1=ID 错误；2=I2C 错误
  */
uint8_t BMP180_Init(void);

/**
  * @brief  读取温度（触发转换→等待→读原始值→补偿）
  * @retval 温度值，单位 0.1°C（如 251 表示 25.1°C）；失败返回 0x7FFF
  */
int32_t BMP180_ReadTemp(void);

/**
  * @brief  读取气压
  * @param  oss : 过采样倍率 0~3（越大越准越慢）
  * @retval 气压值，单位 Pa（101325 = 1013.25hPa）；失败返回 0
  */
int32_t BMP180_ReadPressure(uint8_t oss);

#endif /* __BMP180_H */
