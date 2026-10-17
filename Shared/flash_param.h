#ifndef FLASH_PARAM_H
#define FLASH_PARAM_H

#include <stdint.h>

/* ============================================================
 * flash_param.h - 内部 Flash 参数页 (升级标志), 两工程共用
 *
 * 布局: 0x08000000 boot 12KB | 0x08003000 参数 4KB | 0x08004000 app 48KB
 *       参数页只用第一页 0x08003000(1KB), 之后 3 页留作扩展
 *
 * 断电安全三原则 (M9.3 设计评审结论):
 *   原则1: 标志必须在擦除 app 区【之前】写入 (顺序反了必砖)
 *   原则2: COPYINING 状态的恢复 = 重做搬运(幂等), 源在 W25Q64 不会坏
 *   原则3: 标志清除必须在读回校验【通过】之后
 * ============================================================ */

/* --- 参数页地址: F103 中容量页大小 1KB, 擦除粒度 = 页 --- */
#define UPD_PARAM_ADDR      0x08003000UL

/* --- 魔数: v2 = 'UPDU'。M9.4 结构体新增 bkp_crc32, 升版作废旧记录(无害,
 *      boot 会按需重写)。空页(0xFFFFFFFF)/撕裂写都对不上 --- */
#define UPD_PARAM_MAGIC     0x55504455UL

/* --- 升级状态机 (M9.3a 定义的三个稳态) ---
 * IDLE    : 无升级请求, boot 直接跳 app
 * STAGED  : app 已把新固件写入 W25Q64 staging 并通过 CRC, 等 boot 搬运
 * COPYING : boot 已开始擦写 app 区; 任何时刻断电, 下次上电必须重做搬运 */
#define UPD_STATE_IDLE      0x00000000UL
#define UPD_STATE_STAGED    0x00000001UL
#define UPD_STATE_COPYING   0x00000002UL

/* --- 备份区有效性标记 (M9.4 回滚) --- */
#define UPD_BKP_NONE        0xFFFFFFFFUL   /* 无有效备份 */

typedef struct {
    uint32_t magic;      /* = UPD_PARAM_MAGIC */
    uint32_t state;      /* UPD_STATE_xxx */
    uint32_t fw_size;    /* staging 内新固件字节数 */
    uint32_t fw_crc32;   /* 新固件整体 CRC32 (对应 W25Q64 staging 内容) */
    uint32_t bkp_crc32;  /* 备份区 CRC32 (W25Q64 扇区14起, 全48KB);
                            UPD_BKP_NONE = 无有效备份。M9.4 新增 */
    uint32_t param_crc;  /* 以上 5 字段的 CRC32 —— 撕裂写检测, Write 自动填 */
} UpdateParam_t;         /* 24 字节; 加字段须插在 param_crc 之前并保持偶数字节 */

/* 读参数页并做完整性校验
 * 返回 0 = 有效读出; 1 = 无效(空页/撕裂/魔数不符), 调用方一律视作 IDLE */
uint8_t FlashParam_Read(UpdateParam_t *out);

/* 擦页 + 半字编程 + 读回逐字节校验, param_crc 由本函数自动计算填入
 * 返回 0 = OK; 1 = 失败(BSY 超时 / 编程错误 / 读回不符) */
uint8_t FlashParam_Write(const UpdateParam_t *in);

#endif /* FLASH_PARAM_H */
