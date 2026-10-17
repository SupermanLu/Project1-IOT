/**
  ******************************************************************************
  * @file    dht11.c
  * @brief   DHT11 温湿度传感器驱动（单总线，数据脚 PB12）
  * @note    v3：读 41 个脉冲 + 校验和自动对齐（兼容同步点偏 1 位的情形）。
  *          v2 实测：某些模块应答高电平尾巴会被当成第一个数据位（整帧左移 1 位），
  *          真实数据 H=98% T=29.3°C 被读成 B1 00 0E 81 C1（校验必错）。
  *          v3 对偏移 0 / 偏移 1 两种对齐都做校验，哪种通过用哪种。
  *          硬件：数据线需 4.7k~10k 上拉到 3.3V（模块自带亦可），连线尽量短。
  ******************************************************************************
  */
#include "dht11.h"
#include "main.h"
#include "bsp_delay.h"

#define DHT11_PULSE_NUM      41U  /* 多读 1 个脉冲，用于对齐自愈 */
#define DHT11_DATA_BITS      40U

/* 位判定阈值：高电平脉宽 > 40us 判 '1'（'0'≈26-28us，'1'≈70us） */
static uint32_t g_bit_threshold;
static uint32_t g_timeout_cycles;  /* 200us 超时 */

#define DHT11_READ_OK       0U
#define DHT11_ERR_PARAM     1U
#define DHT11_ERR_CHECKSUM  2U
#define DHT11_ERR_TIMEOUT   3U

/* 将数据脚切为开漏输出（用于拉低起始信号） */
static void DHT11_SetOutput(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin   = DHT11_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_OD;  /* 开漏，配合外部上拉 */
    GPIO_InitStruct.Pull  = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DHT11_GPIO_Port, &GPIO_InitStruct);
}

/* 将数据脚切为输入（用于读取） */
static void DHT11_SetInput(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin  = DHT11_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(DHT11_GPIO_Port, &GPIO_InitStruct);
}

/**
  * @brief  等待引脚达到指定电平（用 DWT 周期做超时）
  * @retval 0=达到目标电平；1=超时
  */
static uint8_t DHT11_WaitLevel(GPIO_PinState level)
{
    uint32_t start = DWT->CYCCNT;
    while (HAL_GPIO_ReadPin(DHT11_GPIO_Port, DHT11_Pin) != level)
    {
        if ((DWT->CYCCNT - start) > g_timeout_cycles)
        {
            return 1U;  /* 超时 */
        }
    }
    return 0U;
}

/**
  * @brief  从 41 位流按偏移取出 5 字节并校验（checksum + 物理合理性）
  * @note   错位帧的 checksum 有小概率碰巧通过（本项目实测踩坑：真实 H=98% T=28.9C
  *         左移一位后 B1 00 0E 04 C3 校验恰好成立，被当成有效数据输出 177.0%/14.4C），
  *         因此必须叠加"湿度<=100、温度<=60"的合理性检查双重过滤。
  * @retval 0=校验且合理性通过（out 已填充）
  */
static uint8_t DHT11_Assemble(const uint8_t *bits, uint8_t offset, uint8_t *out)
{
    uint8_t i;
    uint32_t sum = 0;

    for (i = 0; i < 5; i++)
    {
        out[i] = 0;   /* 每次组装前清零（修复跨次 OR 残留 bug） */
    }

    for (i = 0; i < DHT11_DATA_BITS; i++)
    {
        if (bits[offset + i])
        {
            out[i >> 3] |= (uint8_t)(0x80U >> (i & 7U));
        }
    }

    for (i = 0; i < 4; i++)
    {
        sum += out[i];
    }
    if ((sum & 0xFFU) != out[4])
    {
        return 1U;  /* checksum 不通过 */
    }

    /* 物理合理性：DHT11 湿度 20~100%，温度 0~60°C，小数位 <10 */
    if ((out[0] > 100U) || (out[2] > 60U) || (out[1] >= 10U) || (out[3] >= 10U))
    {
        return 1U;  /* 合理性不通过（很可能是错位帧碰巧过了 checksum） */
    }
    return 0U;
}

uint8_t DHT11_Read(DHT11_Data_t *data)
{
    uint8_t bits[DHT11_PULSE_NUM] = {0};
    uint8_t frame[5] = {0};
    uint8_t n;
    uint8_t err = DHT11_READ_OK;
    uint32_t t_rise;

    if (data == NULL)
    {
        return DHT11_ERR_PARAM;
    }

    g_bit_threshold  = (SystemCoreClock / 1000000UL) * 40UL;  /* 40us 阈值 */
    g_timeout_cycles = (SystemCoreClock / 1000000UL) * 200UL; /* 200us 超时 */

    /* 读时序期间关中断防抢占（采集间隔 >=2s，影响可忽略） */
    __disable_irq();

    /* 1. 起始信号：拉低 20ms 后释放总线 */
    DHT11_SetOutput();
    HAL_GPIO_WritePin(DHT11_GPIO_Port, DHT11_Pin, GPIO_PIN_RESET);
    BSP_Delay_us(20000);
    HAL_GPIO_WritePin(DHT11_GPIO_Port, DHT11_Pin, GPIO_PIN_SET); /* 释放 */
    DHT11_SetInput();
    BSP_Delay_us(30);                            /* 等 DHT11 应答（20~40us） */

    /* 2. 应答：80us 低 + 80us 高 */
    if (DHT11_WaitLevel(GPIO_PIN_RESET)) { err = DHT11_ERR_TIMEOUT; goto exit; } /* 等低 */
    if (DHT11_WaitLevel(GPIO_PIN_SET))   { err = DHT11_ERR_TIMEOUT; goto exit; } /* 等 80us 高开始 */
    if (DHT11_WaitLevel(GPIO_PIN_RESET)) { err = DHT11_ERR_TIMEOUT; goto exit; } /* 等 80us 高结束 */

    /* 3. 读 41 个脉冲：'0'≈27us，'1'≈70us，按实际脉宽判定。
          多读 1 个是为对齐自愈：若同步点多采了应答尾巴（额外前导 '1'），
          真实 40bit 位于 bits[1..40]；否则位于 bits[0..39]。
          第 41 个脉冲若在"无后续传输"时等待下降沿会超时——这是正常结束，
          不算错误（bits[40] 保持 0，由对齐校验裁决）。 */
    for (n = 0; n < DHT11_PULSE_NUM; n++)
    {
        /* 3.1 等待本 bit 的低电平结束（上升沿） */
        if (DHT11_WaitLevel(GPIO_PIN_SET))
        {
            if (n >= DHT11_DATA_BITS) { break; }  /* 传输已结束，正常退出 */
            err = DHT11_ERR_TIMEOUT; break;
        }
        t_rise = DWT->CYCCNT;

        /* 3.2 等待高电平结束（下降沿）；第 41 个脉冲允许超时（发送结束总线释放） */
        if (DHT11_WaitLevel(GPIO_PIN_RESET))
        {
            if (n >= DHT11_DATA_BITS) { break; }
            err = DHT11_ERR_TIMEOUT; break;
        }

        /* 3.3 按实际高电平脉宽判定：>40us = '1' */
        if ((DWT->CYCCNT - t_rise) > g_bit_threshold)
        {
            bits[n] = 1U;
        }
    }

exit:
    __enable_irq();

    if (err != DHT11_READ_OK)
    {
        return err;
    }

    /* 4. 对齐校验：偏移 0（正常）优先，失败再试偏移 1（同步点多采一位） */
    if (DHT11_Assemble(bits, 0, frame) == 0U)
    {
        /* 正常对齐 */
    }
    else if (DHT11_Assemble(bits, 1, frame) == 0U)
    {
        /* 同步偏 1 位，自动纠正 */
    }
    else
    {
        /* 两种对齐都失败：输出偏移 0 的原始字节供调试 */
        (void)DHT11_Assemble(bits, 0, frame);
        data->humi_int = frame[0];
        data->humi_dec = frame[1];
        data->temp_int = frame[2];
        data->temp_dec = frame[3];
        data->checksum = frame[4];
        return DHT11_ERR_CHECKSUM;
    }

    data->humi_int = frame[0];
    data->humi_dec = frame[1];
    data->temp_int = frame[2];
    data->temp_dec = frame[3];
    data->checksum = frame[4];
    return DHT11_READ_OK;
}
