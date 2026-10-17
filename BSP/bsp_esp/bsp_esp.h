#ifndef __BSP_ESP_H__
#define __BSP_ESP_H__

#include "stm32f1xx_hal.h"   /* 必须在下面第一个用 uint8_t 的地方之前 —— 头文件要自包含
                              * （坑 15 同族：谁用到的类型，谁自己 include，且顺序要够早） */

/* ---- 下行设置命令槽（M3.5/S4）----
 * 链路层只负责"拆出 key/val"，不认识任何业务键名 —— 保持可移植 */
#define ESP_SET_KEY_MAX  32U    /* 契约：ESP 侧键名截断到 31 字符 */
#define ESP_SET_VAL_MAX  48U    /* 契约：ESP 侧值截断到 47 字符 */

typedef struct
{
    char key[ESP_SET_KEY_MAX];   /* 键名（已 '\0' 结尾） */
    char val[ESP_SET_VAL_MAX];   /* 值文本（已 '\0' 结尾） */
} ESP_SetCmd_t;

uint8_t ESP_GetSetCommand(ESP_SetCmd_t *out);   /* 取走一条命令：有→填 out 返回 1；无→返回 0 */

/* ---- M9.3d: FW* 行单槽(OTA 固件传输) ----
 * 白名单放行 "FW" 前缀行, 整行存槽; 应用层(fw_ota)读-清取走。
 * 停等协议下至多一条在途, 新行覆盖旧行 = 主动丢陈旧 */
uint8_t ESP_GetFwLine(char *buf, uint16_t *len);   /* 有→拷入 buf 返回 1(带 '\0' 结尾) */

void ESP_Start(void);   /*挂第一次接收中断 , 上电调用一次*/

void ESP_SendATStateQuery(void);   /* 发送 AT+STATE? 查询帧(M2 测试) */
uint8_t ESP_RB_Pop(uint8_t *out);   /* 取 1 字节, 空返回 0 */

void ESP_PollLine(void);   /* 轮询行缓冲区 */
void ESP_SendLine(const char *s, uint16_t len);   /* 发一行(自动补 '\n') */

void ESP_TxMutexInit(void);   /* 在 MX_FREERTOS_Init 里调用(不能放 ESP_Start) */

#endif
