/* ============================================================
 * fw_ota.c - OTA 会话状态机 (M9.3d)
 *
 * 状态: IDLE --FWBEGIN--> ERASE(每圈擦一片, 完->ACK,BEGIN)
 *       --> DATA(FW,seq -> 解码 -> 页写 staging -> ACK)
 *       --FWEND--> (整体CRC比对 -> 过: 写STAGED标志+复位 / 不过: 零代价放弃)
 *
 * 节奏设计: 全部动作挂在 UITask 的 20ms 圈上, 每圈至多:
 *   解码 1 块(~µs) + 页写(~1ms) + 一片扇区擦(45~400ms, 仅 ERASE 态)
 *   —— 擦写期间 UI 会短暂卡顿, OTA 模式下可接受的取舍(坑24 特例, 有注释)
 * ============================================================ */
#include "fw_ota.h"
#include "bsp_esp.h"
#include "bsp_debug.h"
#include "w25q64.h"
#include "flash_param.h"
#include "fw_update.h"
#include "cmsis_os.h"        /* osDelay */
#include "stm32f1xx_hal.h"   /* NVIC_SystemReset */
#include <string.h>

#define OTA_TIMEOUT_LOOPS  300U   /* 300×20ms = 6s 无进展放弃(覆盖最坏擦除时间) */
#define OTA_RAW_MAX        180U   /* 块上限: 180B -> b64 240 字符, 行总长 ≤255(契约) */
#define OTA_LINE_MAX       255U

typedef enum
{
    OTA_IDLE = 0,     /* 无会话 */
    OTA_ERASE,        /* 正在擦 staging 扇区(每圈一片) */
    OTA_DATA,         /* 正在收数据块 */
} ota_state_t;

static ota_state_t s_state     = OTA_IDLE;
static uint32_t    s_size      = 0;    /* 固件总字节数 */
static uint32_t    s_chunk     = 0;    /* 每块原始字节数 */
static uint32_t    s_crcWant   = 0;    /* 整体 CRC32 期望值 */
static uint32_t    s_seqNext   = 0;    /* 期望的下一个 seq */
static uint32_t    s_eraseSec  = 0;    /* ERASE 态: 下一个待擦扇区号 */
static uint16_t    s_idle      = 0;    /* 20ms 无进展计数 */
static uint8_t     s_raw[OTA_RAW_MAX]; /* Base64 解码落点(static, 不进栈) */

/* ---------------- 小工具 ---------------- */

/* 阻塞发送一行(内容自动补 '\n' 由 ESP_SendLine 负责) */
static void fw_send_str(const char *s)
{
    ESP_SendLine(s, (uint16_t)strlen(s));
}

/* "FWACK,123" / "FWNAK,45" 形式的应答(避免动用 printf 家族) */
static void fw_reply(const char *head, uint32_t v)
{
    char    b[20];
    char    tmp[10];
    uint8_t i = 0;
    uint8_t t = 0;

    while (*head != '\0')
    {
        b[i++] = *head++;
    }
    if (v == 0U)
    {
        tmp[t++] = '0';
    }
    while (v > 0U)
    {
        tmp[t++] = (char)('0' + (v % 10U));
        v /= 10U;
    }
    while (t > 0U)
    {
        b[i++] = tmp[--t];
    }
    ESP_SendLine(b, i);
}

/* 十进制字段: 返回 1 = 解析成功, *pp 前进到字段尾 */
static uint8_t fw_parse_u32(const char **pp, uint32_t *out)
{
    const char *p = *pp;
    uint32_t    v = 0;

    if ((*p < '0') || (*p > '9'))
    {
        return 0U;
    }
    while ((*p >= '0') && (*p <= '9'))
    {
        v = (v * 10U) + (uint32_t)(*p - '0');
        p++;
    }
    *pp  = p;
    *out = v;
    return 1U;
}

/* 8 位十六进制字段(大小写均可) */
static uint8_t fw_parse_hex8(const char **pp, uint32_t *out)
{
    const char *p = *pp;
    uint32_t    v = 0;
    uint8_t     i;

    for (i = 0U; i < 8U; i++)
    {
        char    c = p[i];
        uint8_t d;

        if ((c >= '0') && (c <= '9'))      { d = (uint8_t)(c - '0'); }
        else if ((c >= 'A') && (c <= 'F')) { d = (uint8_t)(c - 'A' + 10); }
        else if ((c >= 'a') && (c <= 'f')) { d = (uint8_t)(c - 'a' + 10); }
        else { return 0U; }
        v = (v << 4) | d;
    }
    *pp  = p + 8;
    *out = v;
    return 1U;
}

/* Base64 字符 -> 6 位值; 非法字符返回 255 */
static uint8_t fw_b64_val(char c)
{
    if ((c >= 'A') && (c <= 'Z')) { return (uint8_t)(c - 'A'); }
    if ((c >= 'a') && (c <= 'z')) { return (uint8_t)(c - 'a' + 26); }
    if ((c >= '0') && (c <= '9')) { return (uint8_t)(c - '0' + 52); }
    if (c == '+') { return 62U; }
    if (c == '/') { return 63U; }
    return 255U;
}

/* Base64 解码(宽容版): '=' 补位可选 —— 末尾组可为 4/3/2 字符
 * 实测踩坑(2026-10-14): ESP 对非3整数倍的尾块不补 '=' 发裸尾巴,
 * 严格版(len%4!=0 即拒)导致 seq=230(104B 尾块)确定性三连败。
 * 安全性不降级: 正确性由调用方 decLen != expLen 和整体 CRC 双重兜底;
 * 非法字符/中段 '=' 仍一律拒 */
static uint8_t fw_b64_decode(const char *in, uint16_t n, uint8_t *out, uint16_t *outLen)
{
    uint16_t i;
    uint16_t o = 0;

    *outLen = 0U;
    if (n < 2U)
    {
        return 1U;
    }
    for (i = 0U; i < n; i += 4U)
    {
        uint16_t left = (uint16_t)(n - i);      /* 本组剩余字符数 */
        uint8_t v0, v1, v2 = 0, v3 = 0;
        uint8_t pad = 0;

        if (left < 2U) { return 1U; }           /* 孤字节: 无效 */
        v0 = fw_b64_val(in[i]);
        v1 = fw_b64_val(in[i + 1U]);
        if ((v0 == 255U) || (v1 == 255U)) { return 1U; }

        if (left == 2U)
        {
            pad = 2U;                           /* "xy" 尾组 = 1 字节 */
        }
        else if (left == 3U)
        {
            /* "xy=" 带补位 = 1 字节(pad=2) / "xyz" 裸 3 字符 = 2 字节(pad=1) */
            if (in[i + 2U] == '=') { pad = 2U; }
            else
            {
                v2 = fw_b64_val(in[i + 2U]);
                if (v2 == 255U) { return 1U; }
                pad = 1U;
            }
        }
        else
        {
            /* 4 字符组(中段满组或带补位的结尾组), left>=4 全走这条。
             * 标准 Base64 补位语义(2026-10-15 实锤踩反过, 对着标准死磕):
             *   "xxxx" = 3字节 pad=0
             *   "xxx=" = 2字节 pad=1   ← '=' 在第4位
             *   "xx==" = 1字节 pad=2   ← '=' 从第3位开始 */
            if (in[i + 2U] == '=') {                    /* 必为 "xx==" */
                pad = 2U;
            }
            else
            {
                v2 = fw_b64_val(in[i + 2U]);
                if (v2 == 255U) { return 1U; }
                if (in[i + 3U] == '=') { pad = 1U; }    /* "xxx=" */
                else
                {
                    v3 = fw_b64_val(in[i + 3U]);
                    if (v3 == 255U) { return 1U; }
                }
            }
        }
        if ((pad > 0U) && (i + 4U) < n) { return 1U; }   /* 补位只允许在结尾组 */

        out[o++] = (uint8_t)((v0 << 2) | (v1 >> 4));
        if (pad < 1U) { out[o++] = (uint8_t)((v1 << 4) | (v2 >> 2)); }
        if (pad < 2U) { out[o++] = (uint8_t)((v2 << 6) | v3); }
    }
    *outLen = o;
    return 0U;
}

/* staging 写入改用 Shared 版切分器(FwUpdate_WriteStaging) —— M9.4 收编去重 */

/* ---------------- 行处理器 ---------------- */

static void fw_on_begin(const char *args)
{
    const char *p = args;
    uint32_t    size;
    uint32_t    chunk;
    uint32_t    crc;

    if ((fw_parse_u32(&p, &size)  == 0U) || (*p++ != ',')) { goto bad; }
    if ((fw_parse_u32(&p, &chunk) == 0U) || (*p++ != ',')) { goto bad; }
    if ((fw_parse_hex8(&p, &crc)  == 0U) || (*p   != '\0')){ goto bad; }
    if ((size == 0U) || (size > FW_APP_MAX_SIZE) ||
        (chunk == 0U) || (chunk > OTA_RAW_MAX))
    {
        goto bad;
    }

    s_size    = size;
    s_chunk   = chunk;
    s_crcWant = crc;
    s_seqNext = 0U;
    s_eraseSec = 0U;
    s_idle    = 0U;
    s_state   = OTA_ERASE;
    DBG_Printf("OTA: begin size=%lu chunk=%lu crc=%08lX\r\n",
               (unsigned long)size, (unsigned long)chunk, (unsigned long)crc);
    return;

bad:
    fw_send_str("FWNAK,BEGIN");
    DBG_Printf("OTA: FWBEGIN rejected\r\n");
}

static void fw_on_data(const char *args, uint16_t alen)
{
    const char *p = args;
    uint32_t    seq;
    uint16_t    b64len;
    uint16_t    decLen = 0U;
    uint16_t    expLen;
    uint32_t    off;

    if ((fw_parse_u32(&p, &seq) == 0U) || (*p != ','))
    {
        fw_reply("FWNAK,", s_seqNext);
        return;
    }
    p++;
    if ((uint32_t)(p - args) > alen)
    {
        return;                              /* 防御: 行比声明的还短 */
    }
    b64len = (uint16_t)(alen - (uint16_t)(p - args));

    if (seq == s_seqNext)                    /* 正常顺序块 */
    {
        off    = seq * s_chunk;
        expLen = (uint16_t)(((s_size - off) >= s_chunk) ? s_chunk : (s_size - off));

        if ((fw_b64_decode(p, b64len, s_raw, &decLen) != 0U) || (decLen != expLen))
        {
            fw_reply("FWNAK,", seq);         /* 坏载荷: 让 ESP 原样重发 */
            DBG_Printf("OTA: decode FAIL seq=%lu b64len=%u tail=",
                       (unsigned long)seq, (unsigned int)b64len);
            if (b64len >= 8U)                /* 黑匣子: 带出 base64 尾部 8 字符 */
            {
                DBG_Printf("%.8s\r\n", p + b64len - 8U);
            }
            else
            {
                DBG_Printf("%s\r\n", p);
            }
            return;
        }
        if (FwUpdate_WriteStaging(off, s_raw, decLen) != 0U)
        {
            fw_reply("FWNAK,", seq);
            DBG_Printf("OTA: stage write FAIL seq=%lu\r\n", (unsigned long)seq);
            return;
        }
        s_seqNext++;
        s_idle = 0U;
        fw_reply("FWACK,", seq);
        if ((s_seqNext % 64U) == 0U)         /* 进度日志: 别把 PA2 刷成 base64 海 */
        {
            DBG_Printf("OTA: %lu chunks\r\n", (unsigned long)s_seqNext);
        }
    }
    else if ((seq + 1U) == s_seqNext)        /* 重复块 = 上次 ACK 丢了, 补 ACK 不重写 */
    {
        fw_reply("FWACK,", seq);
    }
    else                                      /* 缺口: 告诉 ESP 从哪续 */
    {
        fw_reply("FWNAK,", s_seqNext);
    }
}

static void fw_on_end(void)
{
    UpdateParam_t up;
    uint32_t      calc = 0;

    if ((s_seqNext * s_chunk) < s_size)      /* 块没收齐 */
    {
        fw_send_str("FWNAK,SHORT");
        DBG_Printf("OTA: short (%lu/%lu chunks), abort\r\n",
                   (unsigned long)s_seqNext,
                   (unsigned long)((s_size + s_chunk - 1U) / s_chunk));
        s_state = OTA_IDLE;
        return;
    }

    /* 整体 CRC(一次性 ~25ms, 复用 boot 同款验证器 —— 两端同一份代码) */
    up.fw_size  = s_size;
    up.fw_crc32 = s_crcWant;
    if (FwUpdate_VerifyStaging(&up, &calc) != 0U)
    {
        fw_send_str("FWNAK,CRC");
        DBG_Printf("OTA: staging CRC FAIL calc=%08lX want=%08lX, abort\r\n",
                   (unsigned long)calc, (unsigned long)s_crcWant);
        s_state = OTA_IDLE;                  /* 参数页从未动过: app 照常, 零代价失败 */
        return;
    }

    /* 全部就绪: 写 STAGED 标志, 复位交给 boot(M9.3c 已验证的搬运链)。
     * bkp_crc32 必须保留现值(M9.4)——boot 的回滚备份记录在这里不能被冲掉 */
    up.magic    = UPD_PARAM_MAGIC;
    up.state    = UPD_STATE_STAGED;
    up.fw_size  = s_size;
    up.fw_crc32 = s_crcWant;
    {
        UpdateParam_t cur;

        up.bkp_crc32 = (FlashParam_Read(&cur) == 0U) ? cur.bkp_crc32
                                                     : UPD_BKP_NONE;
    }
    up.param_crc = 0U;                       /* FlashParam_Write 内部重算覆盖 */
    if (FlashParam_Write(&up) != 0U)
    {
        fw_send_str("FWNAK,PARAM");
        DBG_Printf("OTA: param write FAIL, abort\r\n");
        s_state = OTA_IDLE;
        return;
    }

    fw_send_str("FWACK,END");
    DBG_Printf("OTA: staged OK, rebooting into bootloader...\r\n");
    osDelay(100U);                           /* 让 ACK 与日志走完 TX 移位 */
    NVIC_SystemReset();                      /* 不返回 */
}

/* ---------------- 对外接口 ---------------- */

uint8_t FWOTA_Active(void)
{
    return (s_state != OTA_IDLE) ? 1U : 0U;
}

void FWOTA_Poll(void)
{
    static char     buf[OTA_LINE_MAX + 1U];  /* 行拷贝落点(static, 不进栈) */
    uint16_t        l = 0;

    /* ① 超时看门狗: 会话中 6s 无任何进展 -> 放弃(app 无恙, ESP 可重开) */
    if (s_state != OTA_IDLE)
    {
        if (++s_idle > OTA_TIMEOUT_LOOPS)
        {
            DBG_Printf("OTA: timeout, abort\r\n");
            s_state = OTA_IDLE;
        }
    }

    /* ② 取一条 FW 行(停等协议下至多一条在途)并派发 */
    if (ESP_GetFwLine(buf, &l) != 0U)
    {
        s_idle = 0U;
        if (strncmp(buf, "FWBEGIN,", 8U) == 0)
        {
            fw_on_begin(buf + 8U);
        }
        else if ((l == 5U) && (strncmp(buf, "FWEND", 5U) == 0))
        {
            fw_on_end();
        }
        else if (strncmp(buf, "FW,", 3U) == 0)
        {
            fw_on_data(buf + 3U, (uint16_t)(l - 3U));
        }
        else
        {
            DBG_Printf("OTA: unknown FW line, drop\r\n");
        }
    }

    /* ③ 后台进度: ERASE 态每圈擦一片(擦完最后一片才 ACK,BEGIN) */
    if (s_state == OTA_ERASE)
    {
        uint32_t totalSec = (s_size + 4095U) / 4096U;

        if (W25Q64_EraseSector(FW_STAGING_ADDR + (s_eraseSec * 4096U)) != 0U)
        {
            fw_send_str("FWNAK,BEGIN");
            DBG_Printf("OTA: erase FAIL, abort\r\n");
            s_state = OTA_IDLE;
            return;
        }
        s_eraseSec++;
        s_idle = 0U;                         /* 每片擦成功都算进展 */
        if (s_eraseSec >= totalSec)
        {
            fw_send_str("FWACK,BEGIN");
            s_state = OTA_DATA;
            DBG_Printf("OTA: staging erased (%lu sectors)\r\n",
                       (unsigned long)totalSec);
        }
    }
}
