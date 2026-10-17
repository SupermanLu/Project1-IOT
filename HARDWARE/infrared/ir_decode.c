#include "ir_decode.h"

// 全局红外解码器实例
IR_Decoder_t ir_decoder;

volatile uint16_t g_ir_chk_fail_cnt = 0;

// NEC协议时间参数（单位：微秒）
#define NEC_GUIDE_PULSE      13500   // 引导码上升沿间隔：9ms低+4.5ms高=13.5ms，实际测得约4.5ms
#define NEC_GUIDE_TOLERANCE  1500    // 引导码容差
#define NEC_BIT0_PULSE       1120    // 数据0上升沿间隔：560us高+560us低
#define NEC_BIT1_PULSE       2250    // 数据1上升沿间隔：1690us高+560us低
#define NEC_BIT_TOLERANCE    400     // 数据位容差
#define NEC_REPEAT_PULSE     11250   // 连发码：相邻下降沿间隔 = 9ms载波 + 2.25ms空闲
#define NEC_REPEAT_TOLERANCE 500     // 连发码容差（故意收窄！防止与引导码窗口重叠）

/**
 * @brief 初始化红外解码器
 */
void IR_Decoder_Init(void)
{
    IR_Decoder_Reset(&ir_decoder);
}

/**
 * @brief 复位红外解码器状态
 * @param ir 红外解码器指针
 */
void IR_Decoder_Reset(IR_Decoder_t *ir)
{
    int i;
    ir->state = IR_STATE_IDLE;
    ir->last_time = 0;
    ir->pulse_width = 0;
    ir->bit_count = 0;
    ir->result.valid = 0;
    ir->result.repeat = 0;
    for (i = 0; i < IR_DATA_LEN; i++)
    {
        ir->data[i] = 0;
    }
}

/**
 * @brief 红外解码处理函数（在外部中断中调用）
 * @param ir 红外解码器指针
 * @param current_time 当前时间（微秒，来自 DWT 自由计数 BSP_DWT_GetUs）
 * 
 * NEC协议接收端波形（上升沿触发测量）：
 * - 引导码后第一个上升沿间隔：约4.5ms（4.5ms高电平 + 560us低电平前的上升沿）
 * - 数据0下降沿间隔：约1.12ms（560us高 + 560us低）
 * - 数据1下降沿间隔：约2.25ms（1690us高 + 560us低）
 */
void IR_Decoder_Process(IR_Decoder_t *ir, uint32_t current_time)
{
    int i;
    // 计算脉冲宽度（两次上升沿之间的时间间隔）
    uint32_t width;
    uint32_t last_time = ir->last_time;
    ir->last_time = current_time;
    
    /* 移植适配（03-work1.0）：时间源由 16 位 TIM2(1us) 换成 32 位 DWT(BSP_DWT_GetUs)。
     * 32 位无符号减法天然处理回绕（约 59.6s 一次，远大于帧长），直接相减即可；
     * 原 65536 补偿逻辑只适用于 16 位定时器，在 32 位计数下反而会把回绕帧算错 */
    width = current_time - last_time;
    
    switch (ir->state)
    {
        case IR_STATE_IDLE:
            // 空闲状态，等待引导码
            // 引导码特征：9ms低电平 + 4.5ms高电平
            // 第一个上升沿在9ms低电平后触发
            // 但我们无法直接测量9ms，所以等待下一个上升沿来判断
            // 当检测到约4.5ms的间隔时，认为是引导码后的第一个数据位
            if (width > (NEC_GUIDE_PULSE - NEC_GUIDE_TOLERANCE) && 
                width < (NEC_GUIDE_PULSE + NEC_GUIDE_TOLERANCE))
            {
                // 检测到引导码后的第一个上升沿
                ir->state = IR_STATE_DATA;
                ir->bit_count = 0;
                for (i = 0; i < IR_DATA_LEN; i++)
                {
                    ir->data[i] = 0;
                }
            }
            else if(width < (NEC_REPEAT_PULSE + NEC_REPEAT_TOLERANCE) && 
                    width > (NEC_REPEAT_PULSE - NEC_REPEAT_TOLERANCE))
                    {
                        if(ir->result.valid == 0) /* 信箱空才投递：不覆盖没读走的数据帧 */
                        {
                            ir->result.repeat = 1;  /* 连发码 */
                            ir->result.valid = 1;   /* 有效数据 */
                        }
                    }
            break;
            
        case IR_STATE_GUIDE:
            // 这个状态在简化版本中不使用
            ir->state = IR_STATE_IDLE;
            break;
            
        case IR_STATE_DATA:
            // 数据接收状态，根据脉冲宽度判断是0还是1
            if (width > (NEC_BIT0_PULSE - NEC_BIT_TOLERANCE) && 
                width < (NEC_BIT0_PULSE + NEC_BIT_TOLERANCE))
            {
                // 数据0
                ir->data[ir->bit_count / 8] &= ~(1 << (ir->bit_count % 8));
                ir->bit_count++;
            }
            else if (width > (NEC_BIT1_PULSE - NEC_BIT_TOLERANCE) && 
                     width < (NEC_BIT1_PULSE + NEC_BIT_TOLERANCE))
            {
                // 数据1
                ir->data[ir->bit_count / 8] |= (1 << (ir->bit_count % 8));
                ir->bit_count++;
            }
            else
            {
                // 脉冲宽度异常，复位
                ir->state = IR_STATE_IDLE;
                break;
            }
            
            // 检查是否接收完32位数据
            if (ir->bit_count >= 32)
            {
                // 验证数据：地址和命令的反码是否正确
                if ((ir->data[0] ^ ir->data[1]) == 0xFF &&
                    (ir->data[2] ^ ir->data[3]) == 0xFF)
                {
                    // 数据有效
                    ir->result.address = ir->data[0];
                    ir->result.address_inv = ir->data[1];
                    ir->result.command = ir->data[2];
                    ir->result.command_inv = ir->data[3];
                    ir->result.valid = 1;
                    ir->result.repeat = 0;
                }
                else
                {
                    
                }
                ir->state = IR_STATE_IDLE;
            }
            break;
    }
}

/**
 * @brief 获取解码后的键码
 * @param ir 红外解码器指针
 * @param data 输出数据结构
 * @return 1表示有新数据，0表示无数据
 */
uint8_t IR_Decoder_GetKey(IR_Decoder_t *ir, IR_Data_t *data)
{
    if (ir->result.valid)
    {
        *data = ir->result;
        ir->result.valid = 0;
        return 1;
    }
    return 0;
}


