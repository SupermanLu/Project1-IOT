/* ============================================================
 * flash_param.c - 内部 Flash 参数页 (升级标志) 纯逻辑层
 *
 * M9.3c 重构: 寄存器级擦写全部移交 Shared/flash_ops.c
 * (全工程唯一内部 Flash 寄存器代码), 本文件只负责
 * 结构体编组 + param_crc 计算 + 有效性判定。
 * 读回校验由 FlashOps_Program 内建(逐字节), 此处不再重复。
 * ============================================================ */
#include "flash_param.h"
#include "flash_ops.h"
#include "crc32.h"

uint8_t FlashParam_Write(const UpdateParam_t *in)
{
    UpdateParam_t local;

    if (in == (const UpdateParam_t *)0)
    {
        return 1U;
    }

    local = *in;
    /* param_crc 永远由本模块计算, 调用方不填 —— 防止调用方算错导致假标志 */
    local.param_crc = Crc32_Calc((const uint8_t *)&local,
                                 sizeof(UpdateParam_t) - 4U);

    if (FlashOps_ErasePage(UPD_PARAM_ADDR) != 0U)
    {
        return 1U;
    }
    if (FlashOps_Program(UPD_PARAM_ADDR, (const uint8_t *)&local,
                         sizeof(UpdateParam_t)) != 0U)   /* 含读回校验 */
    {
        return 1U;
    }
    return 0U;
}

uint8_t FlashParam_Read(UpdateParam_t *out)
{
    const volatile uint32_t *src = (const volatile uint32_t *)UPD_PARAM_ADDR;

    if (out == (UpdateParam_t *)0)
    {
        return 1U;
    }

    out->magic     = src[0];
    out->state     = src[1];
    out->fw_size   = src[2];
    out->fw_crc32  = src[3];
    out->bkp_crc32 = src[4];                 /* M9.4 v2: 备份区 CRC */
    out->param_crc = src[5];

    if (UPD_PARAM_MAGIC != out->magic)
    {
        return 1U;                           /* 空页 / 未初始化 */
    }
    if ((UPD_STATE_IDLE    != out->state) &&
        (UPD_STATE_STAGED  != out->state) &&
        (UPD_STATE_COPYING != out->state))
    {
        return 1U;                           /* 撕裂写出的非法状态 */
    }
    if (out->param_crc != Crc32_Calc((const uint8_t *)out,
                                     sizeof(UpdateParam_t) - 4U))
    {
        return 1U;                           /* 撕裂写: 内容对不上校验 */
    }
    return 0U;
}
