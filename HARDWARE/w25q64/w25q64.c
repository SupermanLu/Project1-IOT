/**
  ******************************************************************************
  * @file    w25q64.c
  * @brief   W25Q64 SPI Flash 驱动
  *
  * 依赖顺序：SPI收发 → 读ID → 写使能 → 读状态/等空闲 → 扇区擦除 → 页写入 → 任意读
  ******************************************************************************
  */
#include "w25q64.h"
#include "w25q64_port.h"

/* ============================ 底层：CS 与 SPI 收发 ============================ */

/* 片选：CS 低 = 选中。整条指令帧期间必须保持低，帧结束才拉高
 * (M9.3b: 硬件操作全部经 w25q64_port 接缝, HAL 实现移入 w25q64_port_app.c) */
#define W25Q64_CS_LOW()   W25Q64_Port_CsLow()
#define W25Q64_CS_HIGH()  W25Q64_Port_CsHigh()

/**
  * @brief  SPI 全双工收发 1 字节
  * @note   核心认知：SPI 时钟由主机产生，想"收"就必须"发"一个字节制造时钟。
  *         所以"读"= 发 0xFF dummy，收到的才是真数据。
  * @param  tx : 要发送的字节（纯读操作时传 0xFF）
  * @retval 从机返回的字节
  */
static uint8_t W25Q64_ReadWriteByte(uint8_t tx)
{
    /* 通信失败返回全 1（总线悬空的默认电平）—— 语义由 port 实现保证 */
    return W25Q64_Port_Xfer(tx);
}

/**
  * @brief  发送 24 位地址（高字节在前，大端）
  * @note   擦除/写入/读取三条指令都要发地址，抽出来避免三处重复
  */
static void W25Q64_SendAddr(uint32_t addr)
{
    W25Q64_ReadWriteByte((addr >> 16) & 0xFF);   /* A23~A16 */
    W25Q64_ReadWriteByte((addr >> 8)  & 0xFF);   /* A15~A8  */
    W25Q64_ReadWriteByte( addr        & 0xFF);   /* A7~A0   */
}

/* ============================ 指令帧：读 JEDEC ID ============================ */

/**
  * @brief  读 JEDEC ID（3 字节）
  * @note   帧结构：CS↓ → 0x9F → dummy读3字节 → CS↑
  *         返回顺序固定：[0]=厂商ID [1]=存储类型 [2]=容量ID
  *         注意：这条指令没有地址！发完指令直接收。
  * @retval 0=成功；1=失败
  */
uint8_t W25Q64_ReadID(uint32_t *id)
{
    uint8_t id_buf[3] = {0};
    uint8_t i;

    W25Q64_CS_LOW();
    W25Q64_ReadWriteByte(W25Q64_CMD_JEDEC_ID);  /* 发指令 */
    for (i = 0; i < 3; i++)
    {
        id_buf[i] = W25Q64_ReadWriteByte(0xFF); /* dummy 发 0xFF，收真 ID */
    }
    W25Q64_CS_HIGH();

    *id = ((uint32_t)id_buf[0] << 16) | ((uint32_t)id_buf[1] << 8) | id_buf[2];

    return 0;
}

/* ============================ 指令帧：写使能 + 状态轮询 ============================ */

/**
  * @brief  写使能：每次擦除/写入之前必须执行一次
  * @note   帧结构：CS↓ → 0x06 → CS↑（无地址无数据）
  *         上电默认"写禁止"，0x06 置位 WEL 后擦/写才被放行；
  *         擦/写完成后芯片自动清 WEL —— "一次性通行证"。
  * @retval 0=成功
  */
static uint8_t W25Q64_WriteEnable(void)
{
    W25Q64_CS_LOW();
    W25Q64_ReadWriteByte(W25Q64_CMD_WRITE_ENABLE);
    W25Q64_CS_HIGH();

    return 0;
}

/**
  * @brief  等待芯片内部操作完成（轮询 SR1 的 BUSY 位）
  * @note   帧结构：CS↓ → 0x05 → dummy读1字节 → CS↑
  *         SR1 的 bit0 = BUSY：1=忙 0=空闲
  * @retval 0=成功；1=超时
  */
uint8_t W25Q64_WaitBusy(void)
{
    uint32_t timeout = 1000000;   /* 软件超时计数 */
    uint8_t  sr;

    while (timeout--)
    {
        W25Q64_CS_LOW();
        W25Q64_ReadWriteByte(W25Q64_CMD_READ_SR1);
        sr = W25Q64_ReadWriteByte(0xFF);
        W25Q64_CS_HIGH();

        if ((sr & 0x01) == 0)   /* BUSY 清零 = 空闲 */
        {
            return 0;
        }
    }

    return 1;   /* 超时 */
}

/* ============================ 指令帧：扇区擦除 ============================ */

/**
  * @brief  擦除一个扇区（4KB），内部全变 0xFF
  * @param  addr : 扇区内任意地址（函数内按扇区对齐）
  * @note   流程：对齐 → 写使能 → 指令+24位地址 → CS↑ → WaitBusy
  * @retval 0=成功；1=失败
  */
uint8_t W25Q64_EraseSector(uint32_t addr)
{
    addr = addr & ~(W25Q64_SECTOR_SIZE - 1);   /* 低 12 位清零，对齐扇区首 */

    W25Q64_WriteEnable();
    W25Q64_CS_LOW();
    W25Q64_ReadWriteByte(W25Q64_CMD_SECTOR_ERASE);
    W25Q64_SendAddr(addr);
    W25Q64_CS_HIGH();

    return W25Q64_WaitBusy();   /* 擦除期间芯片忙，等待结果作为返回值 */
}

/* ============================ 指令帧：页写入 ============================ */

/**
  * @brief  页写入（1~256 字节，禁止跨页）
  * @note   帧结构：CS↓ → 0x02 → 24位地址 → N字节数据 → CS↑
  *         页边界 = 地址低 8 位，一次 Program 不能跨页！
  * @retval 0=成功；1=参数错误或失败
  */
uint8_t W25Q64_WritePage(uint32_t addr, const uint8_t *buf, uint16_t len)
{
    uint16_t i;

    /* 参数检查：空数据 / 超过一页 / 跨页，都不允许 */
    if (len == 0 || len > W25Q64_PAGE_SIZE)
    {
        return 1;
    }
    if ((addr % W25Q64_PAGE_SIZE) + len > W25Q64_PAGE_SIZE)
    {
        return 1;   /* 跨页禁止 */
    }

    W25Q64_WriteEnable();
    W25Q64_CS_LOW();
    W25Q64_ReadWriteByte(W25Q64_CMD_PAGE_PROGRAM);
    W25Q64_SendAddr(addr);
    for (i = 0; i < len; i++)
    {
        W25Q64_ReadWriteByte(buf[i]);
    }
    W25Q64_CS_HIGH();

    return W25Q64_WaitBusy();   /* 数据在 CS↑ 时才真正写入，之后忙 */
}

/* ============================ 指令帧：任意读 ============================ */

/**
  * @brief  任意长度读数据（可跨页跨扇区）
  * @note   帧结构：CS↓ → 0x03 → 24位地址 → 收 N 字节 → CS↑
  *         读不受页边界限制，地址 24 位自动递增。
  *         读操作不会让芯片忙，结束后不需要 WaitBusy。
  * @retval 0=成功；1=失败
  */
uint8_t W25Q64_Read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    uint32_t i;

    W25Q64_CS_LOW();
    W25Q64_ReadWriteByte(W25Q64_CMD_READ_DATA);
    W25Q64_SendAddr(addr);
    for (i = 0; i < len; i++)
    {
        buf[i] = W25Q64_ReadWriteByte(0xFF);   /* dummy 发，真收 */
    }
    W25Q64_CS_HIGH();

    return 0;
}

/* ============================ 初始化 ============================ */

/**
  * @brief  初始化：CS 默认拉高（空闲态），读 ID 校验
  * @note   gpio.c 里 CubeMX 把 W_CS 初始电平配成了 RESET（低），
  *         意味着初始化前芯片一直被选中——这里第一步先纠正为高。
  * @retval 0=成功；1=ID 不符
  */
uint8_t W25Q64_Init(void)
{
    uint32_t id = 0;

    W25Q64_Port_Init();      /* 总线启动: boot=寄存器级自配 / app=no-op(CubeMX已配) */
    W25Q64_CS_HIGH();
    W25Q64_Port_DelayMs(1);  /* 上电后芯片需要 tVSL(约1ms) 才能接受指令 */

    if (W25Q64_ReadID(&id) != 0)
    {
        return 1;
    }

    /* 期望 0xEF4017，你可以先打印 id 看看实际读到什么 */
    if (((id >> 16) & 0xFF) != W25Q64_MANUF_ID ||
        ((id >>  8) & 0xFF) != W25Q64_MEM_TYPE ||
        ( id        & 0xFF) != W25Q64_CAPACITY_ID)
    {
        return 1;
    }

    return 0;
}
