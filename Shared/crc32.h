#ifndef CRC32_H
#define CRC32_H

/* ============================================================
 * crc32.h - 标准 CRC32 (zlib 同款: 多项式 0xEDB88320 反射)
 * 用途: 参数页撕裂写检测 + W25Q64 staging 固件整体校验
 * 纯函数, 无任何硬件依赖 —— app / boot / PC 工具三方可复算
 * ============================================================ */

#include <stdint.h>

/* 增量式: 分段计算时用 context 版本 */
void     Crc32_Init(uint32_t *ctx);
void     Crc32_Update(uint32_t *ctx, const uint8_t *data, uint32_t len);
uint32_t Crc32_Final(const uint32_t *ctx);

/* 一次性: 整段数据直接算 */
uint32_t Crc32_Calc(const uint8_t *data, uint32_t len);

#endif /* CRC32_H */
