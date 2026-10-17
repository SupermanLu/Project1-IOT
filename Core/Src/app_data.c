/**
  ******************************************************************************
  * @file    app_data.c
  * @brief   应用层共享数据实现
  ******************************************************************************
  */
#include "app_data.h"
#include "w25q64.h"
#include "bsp_debug.h"

AppData_t g_app;
Setting_t g_set;
AppData_t g_app_view;
uint8_t g_buzzer_mute;    /* 消音请求：UITask 置1，CtrlTask 报警解除时清0 */
uint8_t g_alarm_gas;      /* CtrlTask 发布的 gas 报警状态（0/1） */
uint8_t g_set_dirty;      /* S4b: 云端改了 g_set 待落盘标志。UITask 置1，
                             SensorTask 圈首清0并执行 Setting_Save()。
                             单字节读写原子，无需加锁 */

/**
  * @brief  恢复默认阈值
  */
void Setting_Default(void)
{
    g_set.magic      = SETTING_MAGIC;
    g_set.temp_max   = 35;   /* °C */
    g_set.humi_max   = 85;   /* %  */
    g_set.gas_max    = 80;   /* 等级 */
    g_set.light_max  = 90;   /* 等级 */
    g_set.sample_ms  = 2000; /* 与既有 osDelay(2000) 保持一致 */
}

/**
  * @brief  上电从 Flash 读阈值；magic 不对（首次上电/数据损坏）用默认值
  * @note   读 Flash 无风险（不会改变芯片状态），所以失败也可以容忍
  */
void Setting_Load(void)
{
    if (W25Q64_Read(SETTING_FLASH_ADDR, (uint8_t *)&g_set, sizeof(Setting_t)) != 0)
    {
        Setting_Default();
        return;
    }
    if (g_set.magic != SETTING_MAGIC)   /* 没存过 / 旧版本(0xA55A) → 回默认 */
    {
        Setting_Default();
        return;
    }
    /* magic 对也可能是"擦写中途掉电"的坏数据 → 逐项结构校验，任一项越界就整体回默认。
     * 注意：只设上限，不设下限 —— 设置页允许把阈值调到 0，设了下限会把你自己的设置判非法 */
    if ((g_set.temp_max  > SET_TEMP_MAX_HI)  ||
        (g_set.humi_max  > SET_LEVEL_MAX_HI) ||
        (g_set.gas_max   > SET_LEVEL_MAX_HI) ||
        (g_set.light_max > SET_LEVEL_MAX_HI) ||
        (g_set.sample_ms < SET_SAMPLE_MS_LO) ||
        (g_set.sample_ms > SET_SAMPLE_MS_HI))
    {
        DBG_Printf("SET: Flash data invalid, use default\r\n");
        Setting_Default();
    }
}

/**
  * @brief  保存阈值到 Flash（擦除 → 写入）
  * @retval 0=成功；1=失败
  * @note   Flash 写入三原则：先擦后写。每次保存都整扇区重擦，
  *         W25Q64 扇区寿命 10 万次，正常使用绰绰有余
  */
uint8_t Setting_Save(void)
{
    g_set.magic = SETTING_MAGIC;
    if (W25Q64_EraseSector(SETTING_FLASH_ADDR) != 0)
    {
        return 1;
    }
    return W25Q64_WritePage(SETTING_FLASH_ADDR, (uint8_t *)&g_set, sizeof(Setting_t));
}
