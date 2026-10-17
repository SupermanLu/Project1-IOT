#ifndef FW_UPDATE_H
#define FW_UPDATE_H

#include <stdint.h>
#include "flash_param.h"

/* ============================================================
 * fw_update.h - 固件搬运: W25Q64 staging -> 内部 app 区
 *
 * 地址约定 (M9 全局布局):
 *   W25Q64: 扇区0=自检 | 扇区1=设置(0x001000) | 扇区2起=staging
 *   内部:   boot 12KB | 参数 4KB | app 48KB (0x08004000, 与 Keil IROM1 一致)
 *
 * 断电安全三原则在本模块的落点:
 *   原则1: COPYING 标志在第一次擦 app 前 写入 (Install 开头)
 *   原则2: COPYING 状态的恢复 = 重做搬运 (幂等, 源在 W25Q64 不会坏);
 *          且重做前必先重验源 CRC —— 源是唯一真值
 *   原则3: 整体 CRC 读回比对通过后, 才把标志清为 IDLE
 * ============================================================ */

#define FW_STAGING_ADDR   0x00002000UL   /* W25Q64 扇区2 (4KB 对齐) */
#define FW_APP_ADDR       0x08004000UL   /* app 向量表 = app 区起始 */
#define FW_APP_MAX_SIZE   0x0000C000UL   /* app 区 48KB 上限 */
#define FW_BACKUP_ADDR    0x0000E000UL   /* W25Q64 扇区14起, 12扇区=48KB (M9.4 回滚备份) */
#define FW_BACKUP_SIZE    0x0000C000UL   /* 备份区 48KB, 与 app 区等大 (尺寸无关设计) */

/* W25Q64 任意偏移写入: 自动按页边界切分 (WritePage 禁止跨页)。
 * 全工程唯一一份切分逻辑 —— M9.4 从 fw_ota 收编 */
uint8_t FwUpdate_W25q64Write(uint32_t addr, const uint8_t *buf, uint32_t len);

/* staging 任意偏移写入 (上方切分器的薄封装) */
uint8_t FwUpdate_WriteStaging(uint32_t off, const uint8_t *buf, uint32_t len);

/* 源校验: 对 staging 前 fw_size 字节做流式 CRC32 (分块读, 不占大 RAM)
 * 返回 0=匹配; 1=不匹配/参数非法(*calcOut 带回实测值供日志) */
uint8_t FwUpdate_VerifyStaging(const UpdateParam_t *p, uint32_t *calcOut);

/* 安装(调用前必须 VerifyStaging 已通过):
 * COPYING 先行 -> 逐页(读 staging 1KB / 擦内部页 / 编程+读回校验)
 * -> 整体 CRC 读回 -> 通过才清 IDLE。
 * 返回 0=安装成功; 1=失败(标志保持, 由调用方决定困住/回滚) */
uint8_t FwUpdate_Install(const UpdateParam_t *p);

/* ---- M9.4 回滚备份 ----
 * 备份粒度 = 整 48KB app 区 (尺寸无关, 简单即正确; W25Q64 空间管够)
 * 备份有效性由参数页 bkp_crc32 承载 (UPD_BKP_NONE = 无备份) */
uint8_t FwUpdate_BackupApp(uint32_t *crcOut);      /* app区整48KB -> 备份区, *crcOut带回CRC */
uint8_t FwUpdate_BackupValid(uint32_t wantCrc);     /* 重算备份区CRC与wantCrc比对; 1=有效 */
uint8_t FwUpdate_RestoreApp(uint32_t wantCrc);      /* 先验备份CRC(不过=1且app分毫不动),
                                                       再擦app区48页+写回; 0=已恢复 */

#endif /* FW_UPDATE_H */
