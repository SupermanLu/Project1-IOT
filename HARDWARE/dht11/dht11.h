/**
  ******************************************************************************
  * @file    dht11.h
  * @brief   DHT11 温湿度传感器驱动（单总线，数据脚 PB12）
  * @note    依赖 BSP/bsp_delay（DWT 微秒延时）
  ******************************************************************************
  */
#ifndef __DHT11_H
#define __DHT11_H

#include <stdint.h>

typedef struct
{
    uint8_t humi_int;   /* 湿度整数   */
    uint8_t humi_dec;   /* 湿度小数   */
    uint8_t temp_int;   /* 温度整数   */
    uint8_t temp_dec;   /* 温度小数   */
    uint8_t checksum;   /* 校验和     */
} DHT11_Data_t;

/**
  * @brief  读取一次 DHT11
  * @param  data : 输出数据
  * @retval 0=成功；2=校验失败；3=超时/无应答
  * @note   校验失败时 data 中仍保留原始 5 字节（humi_int..checksum 依次为 buf[0..4]），
  *         供调试观察位错位模式；采集周期建议 >=2s；读时序期间关中断约 25ms
  */
uint8_t DHT11_Read(DHT11_Data_t *data);

#endif /* __DHT11_H */
