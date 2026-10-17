#ifndef __IR_DECODE_H
#define __IR_DECODE_H

#include "stm32f1xx_hal.h"

// NEC协议数据长度
#define IR_DATA_LEN       4    // 4字节：地址+地址反码+命令+命令反码

// 红外解码状态
typedef enum {
    IR_STATE_IDLE = 0,     // 空闲状态，等待引导码
    IR_STATE_GUIDE,        // 接收到引导码
    IR_STATE_DATA          // 接收数据中
} IR_State_t;

// 红外解码数据结构
typedef struct {
    uint8_t address;       // 地址码
    uint8_t address_inv;   // 地址反码
    uint8_t command;       // 命令码（键码）
    uint8_t command_inv;   // 命令反码
    uint8_t valid;         // 数据有效标志
    uint8_t repeat;        // 连发帧标志：1=连发帧（键值沿用上一帧），0=正常数据帧
} IR_Data_t;

// 红外解码器结构
typedef struct {
    IR_State_t state;      // 当前状态
    uint32_t last_time;    // 上次中断时间
    uint32_t pulse_width;  // 当前脉冲宽度
    uint8_t bit_count;     // 已接收位数
    uint8_t data[IR_DATA_LEN]; // 接收的数据
    IR_Data_t result;      // 解码结果
} IR_Decoder_t;

// 全局红外解码器实例
extern IR_Decoder_t ir_decoder;

// 函数声明
void IR_Decoder_Init(void);
void IR_Decoder_Reset(IR_Decoder_t *ir);
void IR_Decoder_Process(IR_Decoder_t *ir, uint32_t current_time);
uint8_t IR_Decoder_GetKey(IR_Decoder_t *ir, IR_Data_t *data);

#endif // __IR_DECODE_H
