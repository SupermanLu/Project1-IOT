/* ============================================================
 * fw_update.c - 固件搬运实现
 * 搬运粒度 = 1KB (与内部页同尺寸): 一次 W25Q64 读 = 一次页擦 + 一次编程
 * ============================================================ */
#include "fw_update.h"
#include "w25q64.h"
#include "flash_ops.h"
#include "crc32.h"

#define FW_CHUNK   1024UL

/* 大缓冲绝不进栈: boot 栈仅 ~1KB(startup 默认 0x400), 这是坑46同级的雷 */
static uint8_t s_buf[FW_CHUNK];

/* W25Q64 任意偏移写入: 自动按页边界切分 (WritePage 禁止跨页)
 * M9.4 从 fw_ota 收编 —— 切分逻辑全工程唯一一份 */
uint8_t FwUpdate_W25q64Write(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    while (len > 0U)
    {
        uint32_t n = W25Q64_PAGE_SIZE - (addr & (W25Q64_PAGE_SIZE - 1U));

        if (n > len)
        {
            n = len;
        }
        if (W25Q64_WritePage(addr, buf, (uint16_t)n) != 0U)
        {
            return 1U;
        }
        addr += n;
        buf += n;
        len -= n;
    }
    return 0U;
}

uint8_t FwUpdate_WriteStaging(uint32_t off, const uint8_t *buf, uint32_t len)
{
    return FwUpdate_W25q64Write(FW_STAGING_ADDR + off, buf, len);
}

uint8_t FwUpdate_VerifyStaging(const UpdateParam_t *p, uint32_t *calcOut)
{
    uint32_t ctx;
    uint32_t left;
    uint32_t off = 0U;

    if ((p == (const UpdateParam_t *)0) || (calcOut == (uint32_t *)0))
    {
        return 1U;
    }
    if ((p->fw_size == 0U) || (p->fw_size > FW_APP_MAX_SIZE))
    {
        return 1U;                           /* 尺寸非法: 直接判负 */
    }

    Crc32_Init(&ctx);
    left = p->fw_size;
    while (left > 0U)
    {
        uint32_t n = (left >= FW_CHUNK) ? FW_CHUNK : left;

        if (W25Q64_Read(FW_STAGING_ADDR + off, s_buf, n) != 0U)
        {
            return 1U;                       /* 读源失败 = 校验失败 */
        }
        Crc32_Update(&ctx, s_buf, n);
        off += n;
        left -= n;
    }
    *calcOut = Crc32_Final(&ctx);

    return (*calcOut == p->fw_crc32) ? 0U : 1U;
}

uint8_t FwUpdate_Install(const UpdateParam_t *p)
{
    UpdateParam_t up;
    uint32_t pages;
    uint32_t i;

    if (p == (const UpdateParam_t *)0)
    {
        return 1U;
    }

    /* 原则1: 标志先行 —— 从这一刻起, 任何断电都会进 COPYING 重做分支 */
    up      = *p;
    up.state = UPD_STATE_COPYING;
    if (FlashParam_Write(&up) != 0U)
    {
        return 1U;
    }

    pages = (p->fw_size + FW_CHUNK - 1U) / FW_CHUNK;
    for (i = 0U; i < pages; i++)
    {
        uint32_t off = i * FW_CHUNK;
        uint32_t n   = p->fw_size - off;
        uint32_t nwr;

        if (n > FW_CHUNK)
        {
            n = FW_CHUNK;
        }
        nwr = (n + 1U) & ~1UL;               /* 半字粒度: 偶数化 */

        /* 读源时多读 1 字节到偶数边界: staging 尾部是擦除态 0xFF,
         * 把 0xFFFF 写进已擦除的内部 flash = 无变化, 安全 */
        if (W25Q64_Read(FW_STAGING_ADDR + off, s_buf, nwr) != 0U)
        {
            return 1U;
        }
        if (FlashOps_ErasePage(FW_APP_ADDR + off) != 0U)
        {
            return 1U;
        }
        if (FlashOps_Program(FW_APP_ADDR + off, s_buf, nwr) != 0U)
        {
            return 1U;                       /* 含读回逐字节校验 */
        }
    }
    /* 注: 新固件小于旧固件时, app 区尾部残留旧字节——但新镜像完整覆盖
     * 向量表+全部代码(fw_size 即 bin 大小), 残留不可达, 无需额外擦除 */

    /* 原则3: 整体 CRC 读回 —— 内部 Flash 内存映射, 直接指针读, 零拷贝 */
    if (Crc32_Calc((const uint8_t *)FW_APP_ADDR, p->fw_size) != p->fw_crc32)
    {
        return 1U;                           /* 标志保持 COPYING: 下次上电重做 */
    }

    up.state = UPD_STATE_IDLE;
    if (FlashParam_Write(&up) != 0U)
    {
        return 1U;
    }
    return 0U;
}

/* ---------------- M9.4 回滚备份 ---------------- */

/* 备份: app 区整 48KB -> 备份区 (尺寸无关设计), *crcOut 带回备份内容 CRC。
 * 仅在 STAGED 首次尝试(动 app 区之前)调用 —— COPYING 续传绝不能重复备份:
 * 此时 app 区是半成品, 备份它 = 备份垃圾 */
uint8_t FwUpdate_BackupApp(uint32_t *crcOut)
{
    uint32_t ctx;
    uint32_t off;
    uint32_t sec;

    if (crcOut == (uint32_t *)0)
    {
        return 1U;
    }
    for (sec = 0U; sec < (FW_BACKUP_SIZE / 4096U); sec++)
    {
        if (W25Q64_EraseSector(FW_BACKUP_ADDR + (sec * 4096U)) != 0U)
        {
            return 1U;
        }
    }
    Crc32_Init(&ctx);
    for (off = 0U; off < FW_BACKUP_SIZE; off += FW_CHUNK)
    {
        const uint8_t *src = (const uint8_t *)(FW_APP_ADDR + off);
        uint32_t i;

        for (i = 0U; i < FW_CHUNK; i++)      /* app 内存映射, 直接指针读 */
        {
            s_buf[i] = src[i];
        }
        if (FwUpdate_W25q64Write(FW_BACKUP_ADDR + off, s_buf, FW_CHUNK) != 0U)
        {
            return 1U;
        }
        Crc32_Update(&ctx, s_buf, FW_CHUNK);
    }
    *crcOut = Crc32_Final(&ctx);
    return 0U;
}

/* 备份有效性: 重算备份区 CRC 与期望值比对
 * (验源先于破坏原则在恢复路径上的应用: 备份不可信就绝不动 app 区) */
uint8_t FwUpdate_BackupValid(uint32_t wantCrc)
{
    uint32_t ctx;
    uint32_t off;

    Crc32_Init(&ctx);
    for (off = 0U; off < FW_BACKUP_SIZE; off += FW_CHUNK)
    {
        if (W25Q64_Read(FW_BACKUP_ADDR + off, s_buf, FW_CHUNK) != 0U)
        {
            return 0U;
        }
        Crc32_Update(&ctx, s_buf, FW_CHUNK);
    }
    return (Crc32_Final(&ctx) == wantCrc) ? 1U : 0U;
}

/* 恢复: 先验备份 CRC (不过=返回1, app 区分毫不动), 再擦 app 区全部 48 页,
 * 从备份区整 48KB 写回 (含读回逐字节校验)。约 2.5~3s */
uint8_t FwUpdate_RestoreApp(uint32_t wantCrc)
{
    uint32_t off;
    uint32_t page;

    if (FwUpdate_BackupValid(wantCrc) == 0U)
    {
        return 1U;
    }
    for (page = 0U; page < (FW_APP_MAX_SIZE / 1024U); page++)
    {
        if (FlashOps_ErasePage(FW_APP_ADDR + (page * 1024U)) != 0U)
        {
            return 1U;
        }
    }
    for (off = 0U; off < FW_BACKUP_SIZE; off += FW_CHUNK)
    {
        if (W25Q64_Read(FW_BACKUP_ADDR + off, s_buf, FW_CHUNK) != 0U)
        {
            return 1U;
        }
        if (FlashOps_Program(FW_APP_ADDR + off, s_buf, FW_CHUNK) != 0U)
        {
            return 1U;
        }
    }
    return 0U;
}
