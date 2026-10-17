/**
  ******************************************************************************
  * @file    app_data.h
  * @brief   应用层共享数据（实时传感器数据 g_app + 阈值设置 g_set）
  * @note    架构原则：数据与视图分离
  *          - 主循环（生产者）：读传感器 → 写 g_app
  *          - 页面（消费者）  ：只读 g_app 画图，不关心数据怎么来的
  *          - g_set 阈值      ：页面改 → 存 W25Q64（掉电不丢）→ 上电恢复
  *          后续报警逻辑（M6）也只依赖 g_app 和 g_set，不碰 UI
  ******************************************************************************
  */
#ifndef __APP_DATA_H
#define __APP_DATA_H


#include <stdint.h>

/* ============ 实时传感器数据（主循环每 2s 更新一次） ============ */
typedef struct
{
    /* DHT11 温湿度 */
    uint8_t  dht_ok;                 /* 本次读取成功标志：1=成功 */
    uint8_t  temp_int, temp_dec;     /* 温度 整数/小数位（小数 1 位） */
    uint8_t  humi_int, humi_dec;     /* 湿度 整数/小数位 */

    /* BMP180 气压 */
    uint8_t  bmp_ok;
    int32_t  bmp_t;                  /* 温度 0.1°C（顺带显示用） */
    int32_t  bmp_p;                  /* 气压 Pa */

    /* MQ5 气体 */
    uint16_t mq5_raw;                /* ADC 原始值 0~4095 */
    uint16_t mq5_level;              /* 等级 0~99 */

    /* 光敏 */
    uint16_t light_raw;
    uint16_t light_level;

    uint8_t  seq;                    /* 提交序号：SensorTask 每次提交+1（新样本凭证） */
} AppData_t;

/* ============ 阈值设置（掉电保存到 W25Q64） ============ */
typedef struct
{
    uint16_t magic;                  /* 有效标志 0xA55B：Flash 读出的数据可信 */
    uint16_t temp_max;               /* 温度上限 °C */
    uint16_t humi_max;               /* 湿度上限 %   */
    uint16_t gas_max;                /* 气体等级上限 0~99 */
    uint16_t light_max;              /* 光照等级上限 0~99 */
    uint16_t sample_ms;              /* 采样周期 ms（100~60000）——M3.5 云可调 */
} Setting_t;                         /* 全 uint16 → 12 字节，无对齐空洞 */


#define SETTING_MAGIC    0xA55BUL   /* v2：新增 sample_ms；旧数据(magic=0xA55A)自动作废 */
#define SETTING_FLASH_ADDR  0x001000UL  /* 扇区1（扇区0留给 W25Q64 自检） */

/* --- 结构界限：仅用于判断"Flash 数据是否写坏"（Load 用），必须与设置页可调上限一致 --- */
#define SET_TEMP_MAX_HI    60U      /* °C    ← 对应 page_setting.c 的 s_items[].max */
#define SET_LEVEL_MAX_HI   99U      /* 等级  ← humi_max / gas_max / light_max 共用 */
#define SET_SAMPLE_MS_LO   100U
#define SET_SAMPLE_MS_HI   60000U
#define SET_SAMPLE_MS_STEP 100U     /* 设置页每按一次的步进（须能整除 LO，否则 LO 不可达） */

/* --- 业务界限：下行 SET 用（比结构界限更严：拒绝那些会让报警长鸣的值） --- */
#define DL_TEMP_MAX_LO  5U                /* °C：低于此值 t10 > temp_max*10 恒成立 → 温度报警长鸣+继电器常吸 */
#define DL_TEMP_MAX_HI  SET_TEMP_MAX_HI   /* 上限复用结构界限 60 */

extern AppData_t g_app;
extern Setting_t g_set;
extern AppData_t g_app_view;  /* 视图:页面用，不依赖传感器 */

extern uint8_t g_buzzer_mute;    /* 消音请求：UITask 置1，CtrlTask 报警解除时清0 */
extern uint8_t g_alarm_gas;      /* CtrlTask 发布的 gas 报警状态（0/1） */
extern uint8_t g_set_dirty;      /* S4b: 云端设置待落盘标志（UITask 置1 / SensorTask 清0+存Flash） */

void     Setting_Default(void);                      /* 恢复出厂默认 */
void     Setting_Load(void);                         /* 上电从 Flash 读（无效则用默认） */
uint8_t  Setting_Save(void);                         /* 擦+写 Flash，0=成功 */

#endif /* __APP_DATA_H */
