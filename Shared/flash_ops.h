#ifndef FLASH_OPS_H
#define FLASH_OPS_H

#include <stdint.h>

/* ============================================================
 * flash_ops.h - F103 内部 Flash 通用擦写 (寄存器级, 两工程共用)
 *
 * 全工程唯一的内部 Flash 寄存器代码 —— 坑48/49 教训:
 * 这类代码写两份就会雷两次, 参数页(flash_param)与固件搬运
 * (fw_update)都走这里。
 *
 * F103 中容量硬件事实 (RM0008):
 *   - 擦除粒度 = 1KB 页 (PER+AR+STRT)
 *   - 编程粒度 = 半字, 目标必须已擦除(0xFFFF), 否则 PGERR
 *   - 擦写期间同 bank 取指被总线暂停(自然停顿, 无需 RAM 执行)
 * ============================================================ */

/* 擦除一个 1KB 页 (pageAddr 必须 1KB 对齐)。返回 0=OK; 1=失败 */
uint8_t FlashOps_ErasePage(uint32_t pageAddr);

/* 编程 + 读回逐字节校验。
 * addr 区域必须已擦除; len 必须为非 0 偶数(半字粒度)。
 * 返回 0=OK(含读回一致); 1=失败 */
uint8_t FlashOps_Program(uint32_t addr, const uint8_t *data, uint32_t len);

#endif /* FLASH_OPS_H */
