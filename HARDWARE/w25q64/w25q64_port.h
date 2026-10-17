#ifndef W25Q64_PORT_H
#define W25Q64_PORT_H

#include <stdint.h>

/* ============================================================
 * w25q64_port.h - W25Q64 驱动的硬件接缝 (两工程共用)
 *
 * w25q64.c 只认这里的接口, 不认识任何 HAL/寄存器:
 *   - w25q64_port_app.c  : app 工程, HAL 实现 (复用 CubeMX 的 hspi2)
 *   - w25q64_port_boot.c : boot 工程, 寄存器级实现 (自带初始化)
 * 换 MCU/换库时只改 port 文件, 协议逻辑零改动 —— 分层收益所在
 * ============================================================ */

/* 总线启动: boot 必须自配时钟/GPIO/SPI; app 端 CubeMX 已配好, 为 no-op */
void    W25Q64_Port_Init(void);

/* 总线关闭: "谁开的谁关" —— boot 跳转前必须交还干净硬件 (app 端 no-op) */
void    W25Q64_Port_DeInit(void);

/* 片选: 整条指令帧期间保持低, 帧结束拉高 */
void    W25Q64_Port_CsLow(void);
void    W25Q64_Port_CsHigh(void);

/* SPI 全双工收发 1 字节 (收必须发: 发 0xFF 制造时钟), 返回收到的字节;
 * 通信失败语义 = 返回 0xFF (总线悬空默认电平) */
uint8_t W25Q64_Port_Xfer(uint8_t tx);

/* 毫秒延时 (W25Q64_Init 的 tVSL≈1ms 用, 精度要求低) */
void    W25Q64_Port_DelayMs(uint32_t ms);

#endif /* W25Q64_PORT_H */
