/**
  ******************************************************************************
  * @file    bsp_esp.c
  * @brief   ESP-01S 链路驱动 (USART1)
  * @note    发送: ESP_SendLine() —— 一行一帧, 自动补 '\n' ; ESP_GetSetCommand() 供应用层取下行命令
  *          接收: USART1 RX 中断 → 环形缓冲 → ESP_PollLine() 拼行
  *                → 白名单过滤('{'..'}' / '+STATE:' / 'SET,') → 调试口回显
  *                → 'SET,key=val' 拆进命令槽, 由应用层取走
  *          硬件: ESP U0T(GPIO1) -> PA10 ; ESP U0R(GPIO3) <- PA9
  ******************************************************************************
  */
#include "bsp_esp.h"
#include "stm32f1xx_hal.h"
#include <string.h>      /* strncmp / strchr / memcpy */
#include "cmsis_os.h"   /* osMutex: huart1 发送仲裁 */
#include "bsp_debug.h"  /* DBG_Printf: S4 起用它打印 SET 命令的接受/拒绝 */

#define ESP_RB_SIZE 512U    /* 2 的幂, 用掩码取模 */

static volatile uint8_t s_rbBuf[ESP_RB_SIZE];
static volatile uint16_t s_rbW = 0 ; /* 中断写 */
static volatile uint16_t s_rbR = 0 ; /* 主循环读 */

#define ESP_LINE_MAX 255U                  /* 契约: 单行 ≤255 字符 */
static char     s_line[ESP_LINE_MAX + 1U]; /* +1 放结尾 '\0' */
static uint16_t s_len = 0;                 /* 当前行内容字节数(不含 '\0') */
static uint8_t  s_discard = 0;

/* huart1 发送仲裁：生产者有 SensorTask(周期帧) 和 UITask(下发 ACK) 两个 */
osMutexDef(espTxMutex);
static osMutexId s_espTxMutex = NULL;

/* ---- 下行设置命令单槽信箱（M3.5/S4）----
 * 写入方 ESP_ParseSetCommand() 和取走方 ESP_GetSetCommand() 都跑在 UITask 上下文
 * （前者由 UITask 里的 ESP_PollLine 调用），同任务不会并发 → 不需要 volatile。
 * 语义与 ir_decode 的 result 单槽一致：读-清，取走即作废。 */
static ESP_SetCmd_t s_setCmd;        /* 消息体：key + val 文本 */
static uint8_t      s_setCmdValid = 0; /* 1=有新命令待取 */

/* ---- FW* 行单槽信箱（M9.3d, OTA 固件传输）----
 * 与 SET 槽同模式: 写入方(ESP_HandleLine)和取走方(ESP_GetFwLine)都在
 * UITask 上下文, 无并发; 停等协议下至多一条在途, 新行覆盖旧行 */
#define ESP_FWLINE_MAX 255U
static char     s_fwLine[ESP_FWLINE_MAX + 1U];
static uint16_t s_fwLen = 0;
static uint8_t  s_fwValid = 0;

static void ESP_RB_Push(uint8_t b);
static void ESP_HandleLine(void);

/* huart1/huart2 在 Core/Src/usart.c 中定义 */
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;

/* 单字节接收缓冲: HAL 每次中断搬 1 字节到这里.
 * 不需要 volatile: 它只在接收中断上下文中被写和读, 没有跨上下文访问 */
static uint8_t s_rxByte = 0;

/* 内部工具: (重新)挂 1 字节接收中断 */
static void ESP_ArmRx(void)
{
    HAL_UART_Receive_IT(&huart1, &s_rxByte, 1);
}

void ESP_Start(void)
{


    HAL_NVIC_SetPriority(USART1_IRQn, 0x0F, 0); /* 最低优先级, ISR 只塞字节不抢 RTOS 时间 */
    HAL_NVIC_EnableIRQ(USART1_IRQn);

    ESP_ArmRx();    /* 挂第一次接收中断, 之后靠回调自己续挂 */
}

/* 发送仲裁锁的创建：必须在 MX_FREERTOS_Init() 里、和 xAppMutex 挨着调用。
 * 绝不能放进 ESP_Start()：它跑在 HAL_Delay() 之前，而调度器启动前
 * uxCriticalNesting 初值 0xaaaaaaaa，会让 osMutexCreate 内部的
 * taskENTER/EXIT_CRITICAL 把 PRIMASK 永久留在 1 → 中断全关 → SysTick 停
 * → HAL_Delay 死循环（表现为"烧完毫无反应"，详见笔记坑 43）。 */ 
void ESP_TxMutexInit ( void ) 
{
    s_espTxMutex = osMutexCreate(osMutex(espTxMutex));
}

/* HAL 弱符号重写: 收满 Size 字节后 HAL 在中断上下文里调用它.
 * 整个工程只能有一份, 所有 UART 共用, 所以必须先判实例 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        
        ESP_RB_Push(s_rxByte);      /* 塞 1 字节到接收缓冲区 只做微秒级的活: 塞进环形缓冲就走人*/
        /* 必须重挂, 否则只收得到第 1 个字节 */
        ESP_ArmRx();
    }
}

/* HAL 弱符号重写: 发生 UART 错误时调用.
 * ESP 复位瞬间以 74880 波特率吐 bootloader 乱码, STM32 按 115200 收会产生
 * 帧错误/溢出(ORE); ORE 会被 HAL 当作阻塞错误直接关掉接收中断 ——
 * 不在这里重挂, 串口就永久失聪 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        ESP_ArmRx();
    }
}

/* 发送一行: 内容 + 自动补 '\n' (契约: '\n' 分帧).
 * 阻塞式: 一帧 ~60B 在 115200 下约 5ms —— 只在任务上下文调用,
 * 禁止在 ISR 或互斥锁内调用 */
void ESP_SendLine(const char *s, uint16_t len)
{
    uint8_t nl = '\n';

    if ((0 == s) || (0U == len))
    {
        return;
    }

    /* 锁必须包住"内容 + '\n'"两次发送：
     *  不加锁      → 撞车方拿到 HAL_BUSY，数据无声消失
     *  只锁半截    → 别人的帧会插进内容和换行之间，两条帧一起废
     * 铁律：绝不在持有 xAppMutex 时调用本函数（两把锁不嵌套） */
    (void)osMutexWait(s_espTxMutex, osWaitForever);

    (void)HAL_UART_Transmit(&huart1, (uint8_t *)s, len, 200);   /* 内容 */
    (void)HAL_UART_Transmit(&huart1, &nl, 1U, 50);              /* 行尾 */

    osMutexRelease(s_espTxMutex);
}

void ESP_SendATStateQuery(void)
{
    uint8_t cmd[] = "AT+STATE?\n";

    /* 这里不加锁：它只在 main 里、调度器启动前调用一次，此时没有第二个发送者。
     * 在调度器未启动时调用 osMutexWait(..., osWaitForever) 会踩未定义行为区
     * （实测：程序在 osKernelStart 之前就卡死，连 banner 都不打）。
     * 若将来改成周期性调用，必须回到锁里 —— 且必须放到任务上下文。 */
    (void)HAL_UART_Transmit(&huart1, cmd, sizeof(cmd) - 1, 100);
}

void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart1);
}

/*中断测: 塞 1 字节 ,满则丢弃(遥测数据宁可丢也不能堵)*/
static void ESP_RB_Push(uint8_t b)
{
    uint16_t next = (uint16_t)((s_rbW + 1U) & (ESP_RB_SIZE - 1U));  /*环形前进*/
    if(next != s_rbR)
    {
        s_rbBuf[s_rbW] = b;
        s_rbW = next;
    }
}

/*主循环侧: 取 1 字节; 空(读指针相等) 放回 0*/
uint8_t ESP_RB_Pop(uint8_t *out)
{
    if(s_rbR == s_rbW)
    {
        return 0;
    }

    *out = s_rbBuf[s_rbR];

    s_rbR = (uint16_t)((s_rbR + 1U) & (ESP_RB_SIZE - 1U));
    return 1 ;
}

/*每20ms 调一次; 非阻塞 , 喂一批字节就返回*/
void ESP_PollLine(void)
{
    uint8_t b, n = 0;

    while ((n++ < 32U) && ESP_RB_Pop(&b))
    {
        /* ① 丢弃态最高优先: 只等 '\n' 重新对齐, 其余字节全丢 */
        if (s_discard)
        {
            if (b == '\n')
            {
                s_discard = 0;   /* 锚点到了, 下一字节从新行行首重新开始 */
                s_len = 0;
            }
            continue;
        }

        /* ② 行尾: 一行到此为止 → 补 '\0' → 处理 → 清空迎下一行 */
        if (b == '\n')
        {
            s_line[s_len] = '\0';
            ESP_HandleLine();
            s_len = 0;
            continue;
        }

        /* ③ 放不下: 整行作废 + 丢弃态丢到 '\n' (绝不保留半行) */
        if (s_len >= ESP_LINE_MAX)
        {
            s_discard = 1;
            s_len = 0;
            continue;
        }

        /* ④ 普通字节: 拼进当前行 */
        s_line[s_len++] = (char)b;
    }
}

/* ---- 把 s_line 里的 "SET,key=val" 拆进命令槽（M3.5/S4）----
 * 只做"拆包"：格式不对 / 空键 / 空值 / 超长 一律丢弃（不报错、不阻塞）。
 * 它不认识任何键名的业务含义 —— 那是应用层（freertos.c）的事，分层不越界。 */
static void ESP_ParseSetCommand(void)
{
    const char *p  = s_line + 4U;        /* 跳过 "SET," */
    const char *eq = strchr(p, '=');     /* 找键值分隔符 */
    uint16_t    klen, vlen;

    if (0 == eq)
    {
        DBG_Printf("SET: no '=' -> drop\r\n");
        return;
    }

    klen = (uint16_t)(eq - p);                    /* 键名长度 */
    vlen = (uint16_t)(s_len - 4U - klen - 1U);    /* 值长度 = 总长 - "SET," - 键名 - '=' */

    if ((0U == klen) || (0U == vlen) ||
        (klen >= ESP_SET_KEY_MAX) || (vlen >= ESP_SET_VAL_MAX))
    {
        DBG_Printf("SET: key/val empty or too long -> drop\r\n");
        return;
    }

    memcpy(s_setCmd.key, p, klen);        /* 只拷内容，不拷 '\0'（源串中间没有） */
    s_setCmd.key[klen] = '\0';            /* 自己补结尾，之后才能当字符串用 */
    memcpy(s_setCmd.val, eq + 1U, vlen);
    s_setCmd.val[vlen] = '\0';

    s_setCmdValid = 1U;                   /* 数据全部就位后才置有效位（不留半条命令） */
}

/* ---- 应用层取走一条下行命令（读-清语义）----
 * 返回 1 = 有命令并已填进 out；返回 0 = 当前没有 */
uint8_t ESP_GetSetCommand(ESP_SetCmd_t *out)
{
    if ((0 == out) || (0U == s_setCmdValid))
    {
        return 0U;
    }

    memcpy(out->key, s_setCmd.key, ESP_SET_KEY_MAX);
    memcpy(out->val, s_setCmd.val, ESP_SET_VAL_MAX);

    s_setCmdValid = 0U;                   /* 取走即作废：同一条命令绝不会被执行两次 */
    return 1U;
}

/* ---- M9.3d: 取走一条 FW* 行（读-清语义, fw_ota 在 UITask 里轮询）---- */
uint8_t ESP_GetFwLine(char *buf, uint16_t *len)
{
    if ((0 == buf) || (0 == len) || (0U == s_fwValid))
    {
        return 0U;
    }

    memcpy(buf, s_fwLine, (uint16_t)(s_fwLen + 1U));   /* 含 '\0' */
    *len = s_fwLen;
    s_fwValid = 0U;
    return 1U;
}

/* ---- 整行处理：白名单过滤 + 分发 ----
 * 白名单认四种"形状"：'{'..'}' 数据帧 / "+STATE:" 状态应答 / "SET," 下行设置
 * / "FW" 前缀(OTA 固件传输, M9.3d)。形状不对的一律丢 ——
 * ESP 复位瞬间 74880 波特率的 bootloader 乱码就靠这层挡住。 */
static void ESP_HandleLine(void)
{
    /* ① 空行: 直接丢. 必须先判, 否则下面 s_len-1U 无符号下溢 */
    if(s_len == 0)
    {
        return;
    }

    /* ② 白名单: 条件任一成立才放行（|| 短路，前面的不成立才去算后面的） */
    if((('{' == s_line[0]) && ('}' == s_line[s_len - 1U])) ||   /* JSON 数据帧 */
       (0 == strncmp(s_line, "+STATE:", 7U))                 ||   /* 状态应答 */
       (0 == strncmp(s_line, "SET,", 4U))                    ||   /* 下行设置 */
       (0 == strncmp(s_line, "FW", 2U)))                          /* OTA 固件行(M9.3d) */
    {
        uint8_t nl = '\n';

        /* 放行: 回显到调试口（测试期用，能直观看到进出的每一行）。
         * 例外: FW 数据行(FW,seq,base64) 不回显 —— 216 行×250B 会把 PA2
         * 日志和 20ms 循环一起淹没; FWBEGIN/FWEND 照常回显(事件性, 有诊断价值) */
        if (0 != strncmp(s_line, "FW,", 3U))
        {
            HAL_UART_Transmit(&huart2, (uint8_t *)s_line, (uint16_t)s_len, 100);
            HAL_UART_Transmit(&huart2, &nl, 1, 10);
        }

        /* ③ 下行设置 → 拆进命令槽 */
        if(0 == strncmp(s_line, "SET,", 4U))
        {
            ESP_ParseSetCommand();
        }

        /* ④ OTA 固件行 → 整行存 FW 槽, fw_ota 在 UITask 里取走处理 */
        if(0 == strncmp(s_line, "FW", 2U))
        {
            memcpy(s_fwLine, s_line, (uint16_t)(s_len + 1U));  /* s_len+1 含拼行时补的 '\0' */
            s_fwLen = s_len;
            s_fwValid = 1U;   /* 停等协议下至多一条在途: 新行覆盖旧行 = 丢陈旧 */
        }
    }
    /* 白名单之外: 什么都不做 = 丢弃 */
}