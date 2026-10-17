# Project1-IOT

基于 **STM32F103C8T6** 的 IoT 智能终端（HAL 库 + STM32CubeMX + Keil MDK-ARM），
通过 ESP 模块联网，支持传感器数据采集、本地报警与 **OTA 远程固件升级**。

## 功能特性

- **双固件架构**：Bootloader + App 分区运行，支持 IAP/OTA 升级（App 起始地址 0x08004000）
- **OTA 升级链路**：ESP → STM32，文本行 + Base64 + 停等 ACK 协议，固件先写入 W25Q64 staging 区，CRC32 校验后跳转升级
- **环境监测**：DHT11 温湿度、BMP180 气压、MQ-5 可燃气体、光照、红外感应
- **本地交互**：LCD 显示、按键输入、蜂鸣器/LED 报警（alarm 应用逻辑）、继电器控制
- **无线通信**：ESP 模块 AT 指令联网
- **存储扩展**：W25Q64 SPI Flash（固件 staging + 参数存储）

## 工程结构

```text
├── Bootloader/     # 引导程序：升级校验、跳转 App
├── Core/           # CubeMX 生成代码（main / 中断 / GPIO 初始化）
├── APP/            # 应用层：alarm 报警逻辑、fw_ota 会话、fw_update 升级流程
├── BSP/            # 板级支持包：adc / debug / delay / esp / key
├── HARDWARE/       # 外设驱动：dht11 / bmp180 / mq5 / lcd / led / key / relay / buzzer / w25q64 ...
├── Shared/         # 双固件共享模块：crc32 / flash_ops / flash_param
├── Middlewares/    # 中间件（LVGL 等）
├── Drivers/        # HAL 库与 CMSIS
└── MDK-ARM/        # Keil 工程文件
```

## 开发环境

| 工具 | 说明 |
| ---- | ---- |
| MCU | STM32F103C8T6（64KB Flash / 20KB RAM） |
| IDE | Keil MDK-ARM |
| 配置工具 | STM32CubeMX（`03-work1.0.ioc`） |
| 库 | STM32 HAL |

## 构建说明

1. 用 STM32CubeMX 打开 `03-work1.0.ioc`（如需修改引脚/外设配置）
2. 用 Keil 打开 `MDK-ARM/03-work1.0.uvprojx` 工程，编译 App
3. 打开 `Bootloader/` 下的 Bootloader 工程并编译
4. 烧录后 Bootloader 负责校验并跳转到 App（App 位于 0x08004000）
