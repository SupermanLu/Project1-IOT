#ifndef FW_OTA_H
#define FW_OTA_H

#include <stdint.h>

/* ============================================================
 * fw_ota.h - OTA 会话 (M9.3d): 经 ESP 线路接收固件写入 W25Q64 staging
 *
 * 线路契约 (方案 B: 文本行 + Base64 + 停等 ACK, 与 ESP 侧共同约定):
 *   ESP -> STM32:
 *     FWBEGIN,<size>,<chunk>,<crc32hex8>   开局: 固件字节数/每块原始字节数/整体CRC32
 *     FW,<seq>,<base64>                    数据块: 偏移 = seq*chunk, '=' 补尾
 *     FWEND                                收尾声明
 *   STM32 -> ESP:
 *     FWACK,BEGIN / FWACK,<seq> / FWACK,END
 *     FWNAK,BEGIN / FWNAK,<期望seq> / FWNAK,SHORT / FWNAK,CRC / FWNAK,PARAM
 *   停等协议: ESP 每发一行必须等对应 ACK/NAK —— STM32 环缓 512B +
 *   20ms 轮询 32B 的吞吐喂不动满速流, ACK 节流是必选项不是可选项
 *
 * 断电/失败安全: staging 写坏 = 零代价(参数页在整体 CRC 通过前绝不动),
 * app 全程存活; 复位后 boot 接手 (M9.3c 已验证的搬运链)
 * ============================================================ */

/* UITask 每圈(20ms)调用: 取走 FW 行 + 状态机推进 + 超时看门狗 */
void FWOTA_Poll(void);

/* 1 = OTA 会话进行中 (W25Q64 被本模块独占, SensorTask 应推迟 Setting_Save) */
uint8_t FWOTA_Active(void);

#endif /* FW_OTA_H */
