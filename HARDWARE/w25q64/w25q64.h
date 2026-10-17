/**
  ******************************************************************************
  * @file    w25q64.h
  * @brief   W25Q64 SPI Flash 驱动（SPI2 + PA8 片选，软件 NSS）
  * @note    W25Q64：64Mbit = 8MB
  *          - 页   Page   = 256 字节（写入最小单位，一次 Program 不能跨页）
  *          - 扇区 Sector = 4KB   = 16 页（擦除最小单位）
  *          - 块   Block  = 64KB  = 16 扇区
  *          写入三原则：先擦后写 / 擦完是 0xFF / 写只能 1→0
  ******************************************************************************
  */
#ifndef __W25Q64_H
#define __W25Q64_H

#include <stdint.h>   /* 自包含: 只依赖标准类型; 引脚/HAL 细节全部下沉到 w25q64_port */

/* ============================ 容量参数 ============================ */
#define W25Q64_PAGE_SIZE        256UL               /* 页大小 */
#define W25Q64_SECTOR_SIZE      4096UL              /* 扇区大小 */
#define W25Q64_TOTAL_SIZE      (8UL * 1024 * 1024)  /* 总容量 8MB */

/* ============================ 指令集（见数据手册 8.1） ============================ */
#define W25Q64_CMD_WRITE_ENABLE 0x06  /* 写使能：每次擦/写前必须执行 */
#define W25Q64_CMD_READ_SR1     0x05  /* 读状态寄存器1：BUSY 位在此 */
#define W25Q64_CMD_READ_DATA    0x03  /* 读数据：指令+24位地址 → 连续读 */
#define W25Q64_CMD_PAGE_PROGRAM 0x02  /* 页写入：指令+24位地址+数据(≤256B) */
#define W25Q64_CMD_SECTOR_ERASE 0x20  /* 扇区擦除(4KB)：指令+24位地址 */
#define W25Q64_CMD_JEDEC_ID     0x9F  /* 读 JEDEC ID：返回 3 字节 */

/* JEDEC ID 期望值（W25Q64：厂商 EF=Winbond，类型 40，容量 17） */
#define W25Q64_MANUF_ID         0xEF
#define W25Q64_MEM_TYPE         0x40
#define W25Q64_CAPACITY_ID      0x17

/* ============================ 函数原型 ============================ */

/**
  * @brief  初始化：读 JEDEC ID 并校验是否为 W25Q64
  * @retval 0=成功；1=失败（通信不通或 ID 不符）
  */
uint8_t W25Q64_Init(void);

/**
  * @brief  读 JEDEC ID（3 字节：厂商/类型/容量）
  * @param  id : 输出 24 位 ID，例如 0xEF4017
  * @retval 0=成功；1=失败
  */
uint8_t W25Q64_ReadID(uint32_t *id);

/**
  * @brief  等待芯片内部操作完成（轮询 SR1 的 BUSY 位清零）
  * @retval 0=成功；1=超时
  */
uint8_t W25Q64_WaitBusy(void);

/**
  * @brief  擦除一个扇区（4KB），擦完后该区域全是 0xFF
  * @param  addr : 扇区内任意地址（函数内按扇区对齐）
  * @retval 0=成功；1=失败
  * @note   阻塞时间典型 45ms，最大 400ms——FreeRTOS 下先用忙等，M6 再改
  */
uint8_t W25Q64_EraseSector(uint32_t addr);

/**
  * @brief  页写入（最多 256 字节，不允许跨页！）
  * @param  addr : 起始地址（页内偏移 + len 不得超过 256）
  * @param  buf  : 数据
  * @param  len  : 长度 1~256
  * @retval 0=成功；1=失败
  */
uint8_t W25Q64_WritePage(uint32_t addr, const uint8_t *buf, uint16_t len);

/**
  * @brief  任意长度读数据（可跨页、可读全片）
  * @param  addr : 起始地址
  * @param  buf  : 输出缓冲
  * @param  len  : 任意长度
  * @retval 0=成功；1=失败
  */
uint8_t W25Q64_Read(uint32_t addr, uint8_t *buf, uint32_t len);

#endif /* __W25Q64_H */
