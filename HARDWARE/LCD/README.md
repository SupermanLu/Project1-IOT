# ILI9488 3.5" SPI LCD 驱动 (STM32F407 HAL 移植版)

原驱动基于 MSPM0G3507，已移植为 STM32F407VG + HAL 库。
本目录可直接整体复制到你的 F407 工程中。

## 目录结构
```
BSP/
├── sys.h            // u8/u16 等类型定义（STM32 风格）
├── delay.h          // HAL_Delay() 封装
├── pic.h            // 图片取模数据 gImage_qq[3200]
├── GUI.c            // GUI 绘图/字符/中文/图片函数
├── GUI.h            // GUI 函数声明
├── test.c           // 综合测试 Demo（仅显示，不含触摸屏）
├── test.h           // Demo 函数声明
├── LCD/
│   ├── lcd.c        // ILI9488 驱动实现（HAL_GPIO + HAL_SPI）
│   ├── lcd.h        // 引脚映射宏 + 驱动声明
│   └── FONT.H       // ASCII + 中文点阵字库
└── SPI/
    ├── SPI.c        // SPI1 发送封装（HAL_SPI_Transmit）
    └── SPI.h        // SPI 句柄 extern 声明
```

## 一、硬件接线（默认，需与你实际接线核对）

| LCD 模块 | STM32F407VG | 说明 |
|----------|-------------|------|
| SCK      | PA5         | SPI1_SCK |
| SDI(MOSI)| PA7         | SPI1_MOSI |
| LED(BL)  | PA1         | GPIO_Output (背光) |
| RST      | PA2         | GPIO_Output (硬件复位) |
| DC/RS    | PA3         | GPIO_Output (命令/数据) |
| CS       | PA4         | GPIO_Output (软件片选) |
| VCC      | 3.3V        | |
| GND      | GND         | |

> 若接线不同，只改 `LCD/lcd.h` 里的 `LCD_CS_PORT/LCD_CS_PIN` 等 8 个宏即可。

## 二、CubeMX 配置

1. **SYS** → Debug = Serial Wire（如用 ST-Link）。
2. **RCC** → HSE = Crystal/Ceramic Resonator；系统时钟配到 **HCLK=168MHz**（SPI1 挂在 APB2，PCLK2=84MHz）。
3. **GPIO**：PA1(LED)、PA2(RST)、PA3(DC)、PA4(CS) 设为 **GPIO_Output**（默认推挽、无上下拉、低速即可）。
4. **SPI1**：
   - Mode = **Full-Duplex Master**
   - Hardware NSS = Disable（我们用软件 CS，即 PA4）
   - Parameter Settings：
     - Frame Format = Motorola
     - **Data Size = 8 Bits**
     - **CPOL = Low**，**CPHA = 1 Edge**（模式0）
     - **Baud Rate = Prescaler(2) → 42MHz**（APB2=84M 时）
     - **MSB First**
     - CRC Disabled
   - PA5/PA7 会自动复用为 SPI1 引脚。
5. 生成代码（选 MDK-ARM / STM32CubeIDE 等）。

> CubeMX 生成后 `main.c` 里会有 `hspi1` 句柄和 `MX_SPI1_Init()`。
> 本驱动的 `SPI.h` 用 `extern SPI_HandleTypeDef hspi1;` 引用它。
> 若你的句柄不叫 `hspi1`，请修改 `SPI/SPI.h` 这一行。

## 三、把工程加入编译

1. 你已经把整个 `BSP/` 目录放进工程（源码目录，如 `./Src/BSP/` 或直接放 `Core/` 同级）。
2. 在 Keil / STM32CubeIDE 里把以下文件加入工程编译：
   - `BSP/LCD/lcd.c`
   - `BSP/SPI/SPI.c`
   - `BSP/GUI.c`
   - `BSP/test.c`
3. 把以下目录加入头文件包含路径（include paths）：
   - `BSP/`
   - `BSP/LCD/`
   - `BSP/SPI/`
4. 确保工程已包含 STM32 HAL 库（`stm32f4xx_hal.c` 及 `stm32f4xx_hal_spi.c`、`stm32f4xx_hal_gpio.c`、`stm32f4xx_hal_cortex.c` 等）。

## 四、在你的 main.c 里调用

```c
#include "BSP/LCD/lcd.h"
#include "BSP/GUI.h"
#include "BSP/test.h"

int main(void)
{
    HAL_Init();
    SystemClock_Config();   // CubeMX 生成
    MX_GPIO_Init();         // CubeMX 生成 (PA1=LED,PA2=RST,PA3=DC,PA4=CS Output)
    MX_SPI1_Init();         // CubeMX 生成

    LCD_Init();             // 初始化 LCD（含 ILI9488 复位+初始化序列）

    // 跑综合测试
    main_test();
    Test_Color();
    Test_FillRec();
    Test_Circle();
    Test_Triangle();
    English_Font_test();
    Chinese_Font_test();
    Pic_test();
    Rotate_Test();

    while (1)
    {
        // 你的应用代码，例如 LCD_Clear(RED);
    }
}
```

## 五、常见问题

- **白屏/花屏**：先确认接线（尤其 DC/SCK/MOSI），再确认 SPI 波特率别太高（可先降到 Prescaler=4 即 21MHz 试），最后确认 `lcd.h` 引脚宏与硬件一致。
- **编译报错找不到 `hspi1`**：检查 CubeMX 生成的句柄名，改 `SPI/SPI.h`。
- **编译报 `USE_HORIZONTAL` 未定义**：已在 `lcd.h` 定义，确认 include 路径包含 `BSP/LCD/`。
- **触摸功能**：本目录不含 TOUCH（原工程是 MSPM0 的），需要的话另行移植。

## 六、色深说明

ILI9488 原生 18bit 色（0x66 像素格式）。驱动里 `Lcd_WriteData_16Bit()` 把 RGB565 拆成 3 字节发送（每个分量 8bit 近似），`test.c` 里的颜色常量均为 RGB565，可直接使用。
