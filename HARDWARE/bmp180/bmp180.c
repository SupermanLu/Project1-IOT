/**

  * @file    bmp180.c
  * @brief   BMP180 气压/温度传感器驱动
  *
  */
#include "bmp180.h"
#include "stm32f1xx_hal.h"
#include "bsp_delay.h"

/* hi2c2 在 Core/Src/i2c.c 中定义 */
extern I2C_HandleTypeDef hi2c2;

/* 校准系数缓存（Init 时读出，供补偿公式使用） */
static BMP180_Calib_t s_cal;

/**
  * @brief  向 BMP180 的某个寄存器写一个字节
  * @note   HAL_I2C_Master_Transmit(&hi2c2, DevAddr, pData, Size, Timeout);
  *         - DevAddr 是 8 位地址 = (BMP180_I2C_ADDR << 1)   ← 为什么？见原理课
  *         - pData 需同时含"寄存器地址 + 数据"两个字节
  *         - 超时给 100
  * @retval 0=成功；1=失败
  */
static uint8_t BMP180_WriteReg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};  // 包含寄存器地址和数据

    if(HAL_I2C_Master_Transmit(&hi2c2, BMP180_I2C_ADDR << 1, buf, 2, 100) != HAL_OK) /*接口,发送地址,数据,数据长度,超时时间*/
    {
        return 1;
    }

    return 0;
}

/**
  * @brief  从 BMP180 的某个寄存器连续读 len 个字节
  * @note   HAL_I2C_Mem_Read(&hi2c2, DevAddr, MemAddress, MemAddSize, pData, Size, Timeout);
  *         - MemAddress = 寄存器地址（8 位，用 I2C_MEMADD_SIZE_8BIT）
  *         - DevAddr 也要 <<1
  * @retval 0=成功；1=失败
  */
static uint8_t BMP180_ReadRegs(uint8_t reg, uint8_t *buf, uint8_t len)
{
  /*哪个i2c, 从机地址, 寄存器地址, 数据长度, 存放地址,, 超时时间 */
   if(HAL_I2C_Mem_Read(&hi2c2, BMP180_I2C_ADDR << 1, reg, I2C_MEMADD_SIZE_8BIT, buf, len, 100) != HAL_OK)
    {
        return 1;
    }
    return 0;
}

/**
  * @brief  读 11 个校准系数（0xAA 起共 22 字节），组装进 s_cal
  * @note   大端字节序（高字节在前）。组装：
  *           有符号 int16  : (int16_t)((buf[n] << 8) | buf[n+1])
  *           无符号 uint16 : (uint16_t)((buf[n] << 8) | buf[n+1])
  *         AC1,AC2,AC3,B1,B2,MB,MC,MD 有符号；AC4,AC5,AC6 无符号
  * @retval 0=成功；1=失败
  */
static uint8_t BMP180_ReadCalib(void)
{
    uint8_t buf[22];

    if (BMP180_ReadRegs(BMP180_REG_CAL, buf, 22) != 0)
    {
        return 1;
    }

    // 将读取的字节拼合成校准系数 (注意高低位顺序)
    s_cal.AC1 = (int16_t)(buf[0] << 8 | buf[1]);
    s_cal.AC2 = (int16_t)(buf[2] << 8 | buf[3]);
    s_cal.AC3 = (int16_t)(buf[4] << 8 | buf[5]);
    s_cal.AC4 = (uint16_t)(buf[6] << 8 | buf[7]);
    s_cal.AC5 = (uint16_t)(buf[8] << 8 | buf[9]);
    s_cal.AC6 = (uint16_t)(buf[10] << 8 | buf[11]);
    s_cal.B1 = (int16_t)(buf[12] << 8 | buf[13]);
    s_cal.B2 = (int16_t)(buf[14] << 8 | buf[15]);
    s_cal.MB = (int16_t)(buf[16] << 8 | buf[17]);
    s_cal.MC = (int16_t)(buf[18] << 8 | buf[19]);
    s_cal.MD = (int16_t)(buf[20] << 8 | buf[21]);

    return 0;
}

/**
  * @brief  触发一次转换并等待完成（只做这一件事）
  * @param  cmd    : 转换命令（BMP180_CMD_TEMP / BMP180_CMD_PRES_x）
  * @param  wait_ms: 对应等待时间
  * @retval 0=成功；1=失败
  */
static uint8_t BMP180_Trigger(uint8_t cmd, uint16_t wait_ms)
{
    if (BMP180_WriteReg(BMP180_REG_CTRL, cmd) != 0)
    {
        return 1;
    }
    /* 思考题：为什么 RTOS 下用"忙等"不理想？（答案面试讲） */
    BSP_Delay_ms(wait_ms);
    return 0;
}

/**
  * @brief  温度补偿：UT → 真实温度(0.1°C)，顺带输出 B5
  * @param  ut : 原始温度 ADC 值
  * @param  b5 : 输出中间量 B5（气压补偿要用，不能每次重算丢精度）
  * @note   公式（全整数，禁止浮点）：
  *         X1 = (UT - AC6) * AC5 / 2^15
  *         X2 = MC * 2^11 / (X1 + MD)
  *         B5 = X1 + X2
  *         T  = (B5 + 8) / 2^4
  *         乘除 2^N 用移位；注意先乘后除防截断
  */
static int32_t BMP180_CalcTemp(int32_t ut, int32_t *b5)
{
    int32_t X1, X2, B5, T;

    /********** TODO：翻译上面公式，把 B5 写入 *b5，返回 T **********/
    X1 = ((ut - s_cal.AC6) * s_cal.AC5 ) >> 15;
    X2 = (s_cal.MC * 2048) / (X1 + s_cal.MD);  //MC 是负数，用 *2048 代替 <<11（负数左移是未定义行为）  
    B5 = X1 + X2;
    T = (B5 + 8) >> 4;       // 温度值，单位0.1°C
    *b5 = B5;

    return T;
}

/**
  * @brief  气压补偿：UP → 真实气压(Pa)
  * @param  up : 原始气压 ADC 值（20 位已拼好）
  * @param  b5 : 温度补偿算出的 B5
  * @param  oss: 过采样倍率（与测量时一致）
  * @note   公式：
  *         B6 = B5 - 4000
  *         X1 = (B2 * (B6*B6 / 2^12)) / 2^11
  *         X2 = AC2 * B6 / 2^11
  *         X3 = X1 + X2
  *         B3 = ((AC1*4 + X3) << oss + 2) / 4      ← 注意括号优先级
  *         X1 = AC3 * B6 / 2^13
  *         X2 = (B1 * (B6*B6 / 2^12)) / 2^16
  *         X3 = ((X1 + X2) + 2) / 4
  *         B4 = AC4 * (uint32_t)(X3 + 32768) / 2^15
  *         B7 = ((uint32_t)up - B3) * (50000 >> oss)
  *         if (B7 < 0x80000000) p = (B7 * 2) / B4; else p = (B7 / B4) * 2
  *         X1 = (p / 2^8) * (p / 2^8);  X1 = (X1 * 3038) / 2^16
  *         X2 = (-7357 * p) / 2^16
  *         p  = p + (X1 + X2 + 3791) / 2^4
  *         类型：B4/B7 用 uint32，其余 int32
  */
static int32_t BMP180_CalcPressure(int32_t up, int32_t b5, uint8_t oss)
{
    /* 1. 计算 B6 */
    int32_t B6, X1, X2, X3, B3, p;
    uint32_t B4, B7;

    B6 = b5 - 4000;
    X1 = (s_cal.B2 * ((B6 * B6) >> 12)) / 2048;
    X2 = s_cal.AC2 * B6 / 2048;
    X3 = X1 + X2;

    B3 = (((s_cal.AC1 * 4 + X3) << oss) + 2) / 4;

    X1 = s_cal.AC3 * B6 / 8192;
    X2 = (s_cal.B1 * ((B6 * B6) >> 12)) / 65536;
    X3 = ((X1 + X2) + 2) / 4;
    B4 = s_cal.AC4 * (uint32_t)(X3 + 32768) / 32768;

    B7 = ((uint32_t)up - B3) * (50000 >> oss);
    if (B7 < 0x80000000)
    {
        p = (B7 * 2) / B4;
    }
    else
    {
        p = (B7 / B4) * 2;
    }
    X1 = (p >> 8) * (p >> 8);
    X1 = (X1 * 3038) >> 16;
    X2 = (-7357 * p) >> 16;
    p = p + ((X1 + X2 + 3791) >> 4);

    return p;
}

uint8_t BMP180_Init(void)
{
    uint8_t id = 0;

    /* 1. 校验芯片 ID（0x55） */
    if (BMP180_ReadRegs(BMP180_REG_ID, &id, 1) != 0)
    {
        return 2;   /* I2C 通信失败 */
    }
    if (id != 0x55)
    {
        return 1;   /* 芯片不在位 */
    }

    /* 2. 读校准系数 */
    if (BMP180_ReadCalib() != 0)
    {
        return 2;
    }
    return 0;
}

int32_t BMP180_ReadTemp(void)
{
    int32_t ut = 0, b5 = 0;
    uint8_t buf_t[2];

    /* 1. 触发温度转换 */
    if (BMP180_Trigger(BMP180_CMD_TEMP, 5) != 0)
    {
        return 0x7FFF;
    }

    /* 2. 读 F6/F7（16 位） */
    if (BMP180_ReadRegs(BMP180_REG_DATA, buf_t, 2) != 0)
    {
        return 0x7FFF;
    }
    ut = (buf_t[0] << 8) | buf_t[1];

    /* 3. 补偿 */
    return BMP180_CalcTemp(ut, &b5);
}

int32_t BMP180_ReadPressure(uint8_t oss)
{
    int32_t up = 0, b5 = 0;
    int32_t ut;
    uint8_t wait_ms;
    uint8_t buf_t[2], buf_up[3];

    if (oss > 3)
    {
        oss = 3;
    }

    /* 先测温度拿到 B5（气压补偿依赖它） */
    if (BMP180_Trigger(BMP180_CMD_TEMP, 5) != 0)
    {
        return 0;
    }
    if (BMP180_ReadRegs(BMP180_REG_DATA, buf_t, 2) != 0)
    {
        return 0;
    }
    ut = (buf_t[0] << 8) | buf_t[1];
    BMP180_CalcTemp(ut, &b5);

    /* 再测气压：命令和等待时间按 oss 选择 */
    wait_ms = (oss == 0) ? 5 : (oss == 1) ? 8 : (oss == 2) ? 14 : 26;
    if (BMP180_Trigger(BMP180_CMD_PRES_0 + (oss * 0x40), wait_ms) != 0)
    {
        return 0;
    }
    if (BMP180_ReadRegs(BMP180_REG_DATA, buf_up, 3) != 0)
    {
        return 0;
    }
    up = ((buf_up[0] << 16) | (buf_up[1] << 8) | buf_up[2]) >> (8 - oss);

    return BMP180_CalcPressure(up, b5, oss);
}
