/* ============================================================
 * crc32.c - 位反转逐位 CRC32
 * 设计取舍: 不用 512 字节查表法, 换逐位计算(8 次移位/字节)
 *   - boot 的 12KB 预算寸土寸金, 省下整张表
 *   - 40KB 固件 @72MHz 约 3~4ms, boot 场景完全够用
 * ============================================================ */
#include "crc32.h"

#define CRC32_POLY   0xEDB88320UL   /* 0x04C11DB7 的位反转形式 */
#define CRC32_INIT   0xFFFFFFFFUL
#define CRC32_XOROUT 0xFFFFFFFFUL

void Crc32_Init(uint32_t *ctx)
{
    *ctx = CRC32_INIT;
}

void Crc32_Update(uint32_t *ctx, const uint8_t *data, uint32_t len)
{
    uint32_t crc = *ctx;

    while (len > 0U)
    {
        crc ^= (uint32_t)(*data);
        data++;

        for (uint8_t i = 0U; i < 8U; i++)
        {
            if ((crc & 1UL) != 0U)
            {
                crc = (crc >> 1) ^ CRC32_POLY;
            }
            else
            {
                crc = crc >> 1;
            }
        }
        len--;
    }
    *ctx = crc;
}

uint32_t Crc32_Final(const uint32_t *ctx)
{
    return (*ctx ^ CRC32_XOROUT);
}

uint32_t Crc32_Calc(const uint8_t *data, uint32_t len)
{
    uint32_t ctx;

    Crc32_Init(&ctx);
    Crc32_Update(&ctx, data, len);
    return Crc32_Final(&ctx);
}
