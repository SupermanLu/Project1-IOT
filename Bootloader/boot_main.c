/* ============================================================
 * boot_main.c - Home IoT Bootloader
 * 链接地址: 0x08000000, 预算 12KB (IROM1 = 0x08000000 / 0x3000)
 * 职责: 上电 -> 调试串口 -> W25Q64 探测 -> 升级状态机(STAGED/COPYING)
 *       -> app 区有效性检查(损坏则备份恢复) -> 交接 SP/PC -> 跳转
 * 调试: USART2 TX(PA2) 115200, 与 app 的 PA2 调试口同口 ——
 *       先开串口再复位才能看到 boot 段输出(老规矩)
 * 报错: 困在 boot 时 LED1(PB4 红) 按故障码闪烁 + 串口日志
 *       (PB4 复位默认是 JTAG-NJTRST, 须先释放, 见 boot_stuck)
 * ============================================================ */
#include "stm32f10x.h"
#include "w25q64.h"
#include "w25q64_port.h"   /* boot_main 直接调 Port_Init/DeInit, 必须见到其声明 */
#include "flash_param.h"
#include "fw_update.h"

#define APP_ADDR        0x08004000U   /* app 区起始 = app 的向量表地址 */

/* app 入口: 无参无返回的函数指针, 指向 app 的 Reset_Handler */
typedef void (*app_entry_t)(void);

/* ==================== boot 调试串口 (寄存器级, 仅 TX) ==================== */

#define BP_USART_CR1_UE   (1UL << 13)
#define BP_USART_CR1_TE   (1UL << 3)
#define BP_USART_SR_TXE   (1UL << 7)
#define BP_USART_SR_TC    (1UL << 6)
#define BP_RCC_IOPAEN     (1UL << 2)    /* APB2: GPIOA 时钟 */
#define BP_RCC_IOPBEN     (1UL << 3)    /* APB2: GPIOB 时钟 */
#define BP_RCC_AFIOEN     (1UL << 0)    /* APB2: AFIO 时钟 */
#define BP_RCC_USART2EN   (1UL << 17)   /* APB1: USART2 时钟 */

static void boot_dbg_init(void)
{
    /* PA2 = USART2_TX: 复用推挽 50MHz (CRL nibble2: MODE=11, CNF=10 -> 0xB) */
    RCC->APB2ENR |= BP_RCC_IOPAEN;
    RCC->APB1ENR |= BP_RCC_USART2EN;
    GPIOA->CRL = (GPIOA->CRL & ~0xF00UL) | (0xBUL << 8);
    USART2->BRR = 0x138UL;    /* BRR = PCLK1/波特率 = 36M/115200 = 312 = 0x138
                                 注意: BRR 寄存器本身就是 USARTDIV×16 = fCK/波特率,
                                 不要再 <<4 (曾错写 0x1398 → 实际 7177 波特率) */
    USART2->CR1 = BP_USART_CR1_UE | BP_USART_CR1_TE;
    /* 注: BRR 依赖 boot SystemInit 已把系统时钟配到 72MHz(HSE 失败则波特率漂移) */
}

static void boot_dbg_deinit(void)
{
    while ((USART2->SR & BP_USART_SR_TC) == 0U) { }  /* 等最后一字节移位完 */
    USART2->CR1 = 0U;                                /* 谁开的谁关 */
    RCC->APB1ENR &= ~BP_RCC_USART2EN;
}

static void boot_dbg_putc(char c)
{
    while ((USART2->SR & BP_USART_SR_TXE) == 0U) { }
    USART2->DR = (uint8_t)c;
}

static void boot_dbg_puts(const char *s)
{
    while (*s != '\0')
    {
        boot_dbg_putc(*s);
        s++;
    }
}

static void boot_dbg_puthex(uint32_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    int8_t i;

    boot_dbg_putc('0');
    boot_dbg_putc('x');
    for (i = 7; i >= 0; i--)
    {
        boot_dbg_putc(hex[(v >> (i * 4)) & 0xFU]);
    }
}

/* ==================== 困死报错: LED 故障码闪烁 (M9.4) ====================
 * 任何"boot 无法把控制权交出去"的终点都收敛到这里: 串口日志(若还活着)
 * + LED1(PB4 红) 按故障码闪烁, 永不返回。
 * 故障码: 2=W25Q64 探测失败  3=app 无效且无法恢复  4=安装失败且无法恢复
 * PB4 陷阱: 复位默认为 JTAG-NJTRST 功能, 必须开 AFIO 时钟并把 SWJ_CFG
 * 设为 010(仅保留 SWD) 才能当 GPIO 用 —— app 里是 CubeMX 代劳的。 */
static void boot_stuck(const char *msg, uint8_t code) __attribute__((noreturn));
static void boot_stuck(const char *msg, uint8_t code)
{
    uint8_t n;

    /* 串口若已被 DeInit 关掉(跳转兜底路径), 打印跳过 —— 不然 putc 死等 TXE */
    if ((USART2->CR1 & BP_USART_CR1_UE) != 0U)
    {
        boot_dbg_puts("[boot] STUCK(");
        if (code < 10U) { boot_dbg_putc((char)('0' + code)); }
        boot_dbg_puts("): ");
        boot_dbg_puts(msg);
        boot_dbg_puts("\r\n");
    }

    RCC->APB2ENR |= BP_RCC_IOPBEN | BP_RCC_AFIOEN;
    /* SWJ_CFG=010: 关 JTAG(释放 PB3/PB4/PA15) 留 SWD。SWJ_CFG 位域写后读为 0,
     * MAPR 其余位 boot 从未设置(复位值0), RMW 安全 */
    AFIO->MAPR = (AFIO->MAPR & ~0x07000000UL) | (0x2UL << 24);
    /* PB4 = LED1: 推挽输出 50MHz (CRH nibble4: MODE=11, CNF=00 -> 0x3)
     * nibble 表: pin4 -> nibble4 -> bits 19:16 (坑49 同族, 逐位核对过) */
    GPIOB->CRH = (GPIOB->CRH & ~0xF0000UL) | (0x3UL << 16);
    GPIOB->BSRR = (1UL << 4) << 16;      /* 灭 */

    for (;;)
    {
        for (n = 0U; n < code; n++)      /* code 次快闪 */
        {
            GPIOB->BSRR = (1UL << 4);
            W25Q64_Port_DelayMs(150);
            GPIOB->BSRR = (1UL << 4) << 16;
            W25Q64_Port_DelayMs(150);
        }
        W25Q64_Port_DelayMs(2000);       /* 停 2s 再来一轮 (Port_DelayMs 是纯软件延时,
                                            SPI2 即便已 DeInit 也不受影响) */
    }
}

/* ==================== app 区有效性检查 (M9.4 抽出复用) ==================== */

static uint8_t app_region_valid(void)
{
    uint32_t sp = *(volatile uint32_t *)(APP_ADDR + 0U);      /* 向量表[0]: 初始MSP */
    uint32_t pc = *(volatile uint32_t *)(APP_ADDR + 4U);      /* 向量表[1]: 复位入口 */

    return (((sp & 0xFF000000U) == 0x20000000U) &&           /* SP 落在 RAM 区 */
            ((pc & 0xFF000000U) == 0x08000000U) &&           /* PC 落在 Flash 区 */
            ((pc & 0x00000001U) == 0x00000001U));            /* 带 Thumb 位 */
}

/* ==================== W25Q64 探测 ==================== */

static void boot_probe_w25q64(void)
{
    uint32_t id = 0;

    if (W25Q64_Init() != 0U)
    {
        /* 黑匣子: 失败也要吐原始 ID ——
         * 0x000000 = 芯片从未应答(CS/SCK/MOSI 死或未选中)
         * 0xFFFFFF = MISO 悬空恒高(CS 未选中/引脚断)
         * 其他乱值 = 采样相位/模式错 */
        (void)W25Q64_ReadID(&id);
        boot_dbg_puts("[boot] W25Q64 FAIL id=");
        boot_dbg_puthex(id);
        boot_stuck("W25Q64 probe FAIL", 2U);  /* 无 W25Q64 即无备份无 staging,
                                                 原地报错闪烁 */
    }

    boot_dbg_puts("[boot] W25Q64 id=0xEF4017 [OK]\r\n");
}

/* ============ 升级状态机 (STAGED/COPYING 消费者 + M9.4 回滚) ============
 * 流程: 读参数页 -> IDLE 直接过
 *       -> STAGED/COPYING: 先验源 CRC(staging 无效则零代价放弃, app 不动)
 *       -> STAGED 首次尝试: 先整区备份旧固件(回滚保险) 再动 app 区
 *       -> 安装: 标志先行/逐页搬运/整体CRC/清标志
 *       -> 安装失败且备份有效: 自动恢复旧固件 -> 继续跳老固件
 */
static void boot_try_update(void)
{
    UpdateParam_t p;
    uint32_t calc = 0;

    if (FlashParam_Read(&p) != 0U)
    {
        boot_dbg_puts("[boot] param: none/invalid\r\n");
        return;                              /* 无有效参数 = 无升级请求 */
    }

    boot_dbg_puts("[boot] param state=");
    boot_dbg_puthex(p.state);
    boot_dbg_puts(" size=");
    boot_dbg_puthex(p.fw_size);
    boot_dbg_puts(" crc=");
    boot_dbg_puthex(p.fw_crc32);
    boot_dbg_puts(" bkp=");
    boot_dbg_puthex(p.bkp_crc32);
    boot_dbg_puts("\r\n");

    if (UPD_STATE_IDLE == p.state)
    {
        return;                              /* 正常路径 */
    }

    /* 源校验先于一切破坏: staging 是垃圾时 app 区一个字节都不动,
     * 老固件安然无恙 —— 失败零代价 */
    if (FwUpdate_VerifyStaging(&p, &calc) != 0U)
    {
        boot_dbg_puts("[boot] update: staging CRC FAIL calc=");
        boot_dbg_puthex(calc);
        boot_dbg_puts(" want=");
        boot_dbg_puthex(p.fw_crc32);
        boot_dbg_puts("\r\n[boot] update: app untouched, boot old fw\r\n");
        return;                              /* 继续跳老 app */
    }

    /* M9.4: STAGED 首次尝试 -> 把当前(升级前)固件整体备份进 W25Q64。
     * 只在 STAGED 态备份: COPYING 断电续传时 app 区是半成品, 备份它=备份垃圾。
     * 备份 CRC 先落参数页, 然后才允许擦 app 区 (原则1 的回滚版)。 */
    if (UPD_STATE_STAGED == p.state)
    {
        uint32_t bkp = UPD_BKP_NONE;

        boot_dbg_puts("[boot] update: backing up current app...\r\n");
        if (FwUpdate_BackupApp(&bkp) != 0U)
        {
            boot_stuck("backup FAIL", 4U);   /* 备份失败绝不进入擦除 */
        }
        p.bkp_crc32 = bkp;
        if (FlashParam_Write(&p) != 0U)      /* 备份 CRC 先落参数页, 才允许擦 app 区 */
        {
            boot_stuck("param write FAIL", 4U);
        }
        boot_dbg_puts("[boot] update: backup ok crc=");
        boot_dbg_puthex(bkp);
        boot_dbg_puts("\r\n");
    }

    boot_dbg_puts("[boot] update: installing...\r\n");
    if (FwUpdate_Install(&p) != 0U)
    {
        boot_dbg_puts("[boot] update: INSTALL FAIL\r\n");
        /* 回滚: 备份区装的是"升级前的完好固件", 验 CRC 后整区写回 */
        if ((p.bkp_crc32 != UPD_BKP_NONE) &&
            (FwUpdate_RestoreApp(p.bkp_crc32) == 0U) &&
            (app_region_valid() != 0U))
        {
            p.state = UPD_STATE_IDLE;        /* 备份保留(bkp_crc32 不动), 供下次再滚 */
            if (FlashParam_Write(&p) != 0U)
            {
                boot_stuck("restore OK but param write FAIL", 4U);
            }
            boot_dbg_puts("[boot] update: ROLLED BACK to old fw\r\n");
            return;                          /* 继续跳老 app */
        }
        boot_stuck("install FAIL & no restore", 4U);
    }
    boot_dbg_puts("[boot] update: install OK, flag -> IDLE\r\n");
}

/* ==================== 把控制权交给 app ====================
 * 等价于调试器里手动做的: RST清脏 -> 填SP -> 填PC -> Run */
static void boot_jump_to_app(void)
{
    uint32_t app_sp  = *(volatile uint32_t *)(APP_ADDR + 0U); /* 向量表[0]: 初始MSP */
    uint32_t app_rst = *(volatile uint32_t *)(APP_ADDR + 4U); /* 向量表[1]: 复位入口 */

    /* 最终守卫: main 里已做过有效性检查+恢复, 到这还无效 = 恢复也没救回来。
     * 此时串口可能已被 DeInit, boot_stuck 内部会自行跳过打印只闪 LED */
    if (((app_sp  & 0xFF000000U) != 0x20000000U) ||
        ((app_rst & 0xFF000000U) != 0x08000000U) ||
        ((app_rst & 0x00000001U) == 0U))
    {
        boot_stuck("app invalid (final guard)", 3U);
    }

    /* ---- 交接前清理: 谁开的谁关 ---- */
    __disable_irq();               /* 清理期间屏蔽中断 */
    SysTick->CTRL = 0U;            /* 停 SysTick: 清 ENABLE + TICKINT */
    SysTick->LOAD = 0U;
    SysTick->VAL  = 0U;            /* 写 VAL 顺便清 COUNTFLAG */
    __enable_irq();                /* 必须! PRIMASK=1 会跨跳转存活 —— 坑43
                                      同款: 留着关中断跳走, app 上电窗口的
                                      HAL_Delay 冻死在 uwTick, 而修 PRIMASK 的
                                      osKernelStart 永远到不了 = 必砖 */

    /* ---- 跳转本体: 换栈与跳转必须一气呵成 ----
     * 坑46: __set_MSP() 之后再用 C 语句调函数指针, -O0 下编译器会从栈上
     * 重读局部变量 -> 读到换栈后的垃圾(实测 0x56CFD2CC, XN 区) -> IACCVIOL
     * 铁律: 换栈之后、跳转落地之前, 不允许任何内存访问。
     * 内联汇编保证两个操作数在换栈前就已在寄存器中就位 */
    __asm volatile (
        "msr msp, %0\n\t"      /* 1) 装 app 的初始 MSP */
        "bx   %1"              /* 2) 跳转; bit0 由硬件置 T 位, 不再返回 */
        :
        : "r" (app_sp), "r" (app_rst));
}

int main(void)
{
    /* boot 调试口: 与 app 的 PA2 同口, app 端 MX_USART2_UART_Init 会重配,
     * 因此这里无需精细关闭, 但时钟要还(见下方 dbg_deinit) */
    boot_dbg_init();
    boot_dbg_puts("\r\n[boot] Home IoT Bootloader M9.4\r\n");

    boot_probe_w25q64();       /* W25Q64 = staging+备份的载体, 探测不过全免谈 */
    boot_try_update();         /* STAGED/COPYING 消费者 + 回滚 */

    /* M9.4: 冷启动 app 区损坏(如历史上某次异常写入) -> 趁 W25Q64 还活着,
     * 从备份恢复旧固件。COPYING 态的半成品不会走到这(已在 try_update 重做) */
    if (app_region_valid() == 0U)
    {
        UpdateParam_t p;

        boot_dbg_puts("[boot] app invalid, trying backup restore...\r\n");
        if ((FlashParam_Read(&p) != 0U) ||
            (p.bkp_crc32 == UPD_BKP_NONE) ||
            (FwUpdate_RestoreApp(p.bkp_crc32) != 0U) ||
            (app_region_valid() == 0U))
        {
            boot_stuck("app invalid & no backup", 3U);
        }
        boot_dbg_puts("[boot] app restored from backup\r\n");
        /* param 本就是 IDLE(损坏场景不可能带 COPYING 走到这), 无需改写;
         * bkp_crc32 保持不变 —— 备份继续留着, 万一下次还要滚 */
    }

    boot_dbg_puts("[boot] jumping to app...\r\n");
    W25Q64_Port_DeInit();      /* 谁开的谁关: SPI2 时钟/使能交还 */
    boot_dbg_deinit();         /* USART2 交还 (TC 等待保证日志完整发出) */
    boot_jump_to_app();        /* 内含最终守卫: 失败 -> LED 故障码 3 */

    boot_stuck("unreachable", 3U);           /* 理论不可达; 误入 = 跳转没生效 */
}
