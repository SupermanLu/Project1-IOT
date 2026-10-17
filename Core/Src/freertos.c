/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "bsp_debug.h"
#include "led.h"
#include "buzzer.h"
#include "relay.h"
#include "dht11.h"
#include "mq5.h"
#include "light.h"
#include "bmp180.h"
#include "w25q64.h"
#include "lcd.h"
#include "bsp_key.h"
#include "infrared.h"   /* 红外适配层（内部含 ir_decode.h 解码器） */
#include "gui_page.h"
#include "app_data.h"
#include <string.h>   /* memcmp 用 */
#include "bsp_esp.h"   /* ESP 接口层 */
#include "fw_ota.h"    /* M9.3d: OTA 会话状态机 */
#include "usart.h"
#include <stdio.h>    /* snprintf */
#include <stdlib.h>   /* strtol（S4：解析下行设置项的值，不用 atof 以避开浮点库） */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
static void StartSensorTask(void const * argument);
static void StartCtrlTask(void const * argument);

osMutexDef(appMutex);       /*定义互斥锁*/
osMutexId xAppMutex = NULL; /*句柄在 Init 里创建 , (互斥锁的名称)*/
osThreadDef(sensorTask, StartSensorTask, osPriorityNormal, 0, 256); /* 传感器任务 */
osThreadDef(ctrlTask, StartCtrlTask, osPriorityNormal, 0, 256);     /* 控制任务：报警判定+输出 */


/* USER CODE END Variables */
osThreadId defaultTaskHandle;

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void const * argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* GetIdleTaskMemory prototype (linked to static allocation support) */
void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize );

/* USER CODE BEGIN GET_IDLE_TASK_MEMORY */
static StaticTask_t xIdleTaskTCBBuffer;
static StackType_t xIdleStack[configMINIMAL_STACK_SIZE];

void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize )
{
  *ppxIdleTaskTCBBuffer = &xIdleTaskTCBBuffer;
  *ppxIdleTaskStackBuffer = &xIdleStack[0];
  *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
  /* place for user code */
}
/* USER CODE END GET_IDLE_TASK_MEMORY */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */
  xAppMutex = osMutexCreate(osMutex(appMutex)); /* 创建互斥锁 , 在任务跑起来之前存在*/
  ESP_TxMutexInit();                  /* ESP 发送仲裁锁: 同处创建, 见 bsp_esp.c */
  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* definition and creation of defaultTask */
  osThreadDef(defaultTask, StartDefaultTask, osPriorityNormal, 0, 256);
  defaultTaskHandle = osThreadCreate(osThread(defaultTask), NULL);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  osThreadCreate(osThread(sensorTask), NULL);
  osThreadCreate(osThread(ctrlTask), NULL);
  /* USER CODE END RTOS_THREADS */

}

/* USER CODE BEGIN Header_StartDefaultTask */


/* UI 已迁往 HARDWARE/lcd/page_main.c（仪表盘页）和 page_setting.c（设置页），
 * 由 gui_page.c 页面管理器统一调度；本文件只负责"采集数据 + 喂页面" */

/**
  * @brief  W25Q64 自检：擦 → 写 → 读回比对（只在上电时跑一次）
  * @note   Flash 驱动的标准验收流程；函数必须定义在任务函数"外面"
  */
static void W25Q64_SelfTest(void)
{
    uint8_t  wr[16] = "Hello W25Q64!";
    uint8_t  rd[16] = {0};
    uint32_t addr   = 0x000000;   /* 扇区0，页0 */

    if (W25Q64_Init() != 0)
    {
        DBG_Printf("W25Q64: Init FAIL (ID error)\r\n");
        return;
    }

    W25Q64_EraseSector(addr);
    W25Q64_WritePage(addr, wr, 14);
    W25Q64_Read(addr, rd, 14);

    DBG_Printf("W25Q64: rd=%s  %s\r\n", rd,
               (memcmp(wr, rd, 14) == 0) ? "[OK]" : "[MISMATCH]");
}

static void StartSensorTask(void const * argument)
{
  DHT11_Data_t dht;
  uint8_t ret;
  int32_t bmp_p , bmp_t;
  uint8_t bmp_ok;
  uint16_t mq5_raw, mq5_level;
  uint16_t light_level;
  uint8_t alarm_now;          /* M3.3: 锁内拷出的报警状态(发云端用) */
  uint16_t set_temp_hi;       /* M3.5/S2: 锁内拷出的当前生效阈值(回传给云端) */
  uint16_t set_sample_ms;     /* M3.5/S2: 锁内拷出的当前采样周期 */
  uint32_t last_sample_tick;  /* M3.5/S5: 上次真正采样的时刻(HAL_GetTick, ms) */
  uint8_t  first_loop;        /* M3.5/S5: 首圈强制采样(否则大周期时首帧发的是未初始化局部变量) */

  last_sample_tick = 0;
  first_loop = 1;

  LIGHT_Init();
  /* BMP180 初始化（芯片在位检测 + 读校准系数） */
  ret = BMP180_Init();
  if (ret != 0)
  {
    DBG_Printf("BMP180 init FAIL, err=%d\r\n", ret);
    // g_app_view.bmp_ok = 0;
  }
  else
  {
    // DBG_Printf("BMP180 init OK\r\n");
  }

  for(;;)
  {
      /* ---- S4b: 落盘云端新设置 ----
       * Setting_Save() 是扇区擦写（几十~几百 ms），只配待在 SensorTask 这种慢任务里，
       * 绝不能进 UITask 的 20ms 循环（同坑 24）。
       * 顺序讲究"先清标志、再保存"：若保存期间又来了新 SET，标志会再次置位，
       * 下一圈再存一次（多存一次无害）；反过来"先存后清"则可能把保存期间
       * 到达的新设置丢掉（存的是旧值、标志还被清了）。
       * 注：保存期间 g_set 理论上可能被云端再次修改（保存没拿锁，锁内不做慢事），
       * 极端情况 Flash 里会存到"新旧混合"的值 —— 每个字段都合法，Load 校验能过，
       * 影响仅限一次设置周期，可接受。 */
      /* M9.3d: OTA 会话独占 W25Q64(staging 写入), 期间推迟落盘 ——
       * 脏标志保持置位, OTA 结束/放弃后的下一圈补存, 设置不丢。
       * (两条路都用 W25Q64 却没有互斥, 靠"同一时刻只有一个使用者"约定) */
      if (g_set_dirty && (FWOTA_Active() == 0U))
      {
          g_set_dirty = 0U;
          if (Setting_Save() != 0)
          {
              DBG_Printf("SET: Flash save FAIL\r\n");
          }
      }

      /* ---- M3.5/S5: 采集分频 —— "采样"与"上报"解耦 ----
       * 上报固定 2s 一圈（保活必须），但只有距上次采样 ≥ sample_ms 才真正读传感器；
       * 未到期的圈沿用上一轮的局部变量值 —— 上行帧内容不变、保活不断。
       * 这就是把局部变量声明在 for(;;) 外面的原因：跨圈保持（C 作用域设计）。
       *
       * 两处细节:
       * ① 减法写法 (now - last) >= period 而不是 (now >= last + period)：
       *    HAL_GetTick 49.7 天回绕，无符号减法天然免疫回绕，加法则会翻车。
       * ② first_loop 首圈强制采样：开机时 tick 只有 ~2s，若 sample_ms 设了
       *    60000，没有这个标志首圈就不采样，发出去的是未初始化局部变量（垃圾）。
       *
       * DHT11 物理限制：单次读取 ≥1s（数据手册）。sample_ms < 2000 时 DHT 会
       * 频繁 FAIL，帧自动降级为不带 temp/humi（宁缺不假）—— 设计行为非故障。
       * g_set.sample_ms 是 16 位对齐读，单条指令完成，原子，无需加锁。 */
      if (first_loop ||
          ((HAL_GetTick() - last_sample_tick) >= (uint32_t)g_set.sample_ms))
      {
          first_loop = 0;
          last_sample_tick = HAL_GetTick();

          /* DHT11 温湿度 */
          ret = DHT11_Read(&dht);
          mq5_raw = MQ5_ReadRaw();
          mq5_level = MQ5_ReadLevel();
          light_level = LIGHT_ReadLevel();
          bmp_t = BMP180_ReadTemp();
          bmp_p = BMP180_ReadPressure(2);
          bmp_ok = ((bmp_t != 0x7FFF) && (bmp_p != 0));
      }
      /* else: 未到采样点 —— 沿用上轮局部变量，直接走提交+上报 */

        osMutexWait(xAppMutex, osWaitForever);   /* ③ 提交：锁内一口气写 g_app（注意！不是 g_app_view） */
        g_app.dht_ok = (ret == 0);
        if (ret == 0)
        {
          g_app.temp_int = dht.temp_int;
          g_app.temp_dec = dht.temp_dec;
          g_app.humi_int = dht.humi_int;
          g_app.humi_dec = dht.humi_dec;
          // DBG_Printf("DHT11: H=%d.%d%% T=%d.%d C\r\n",
          //            dht.humi_int, dht.humi_dec,
          //            dht.temp_int, dht.temp_dec);
        }
        else
        {
          /* 2=校验失败 3=超时/无应答；失败时 g_app_view 旧值保留但 dht_ok=0，
          * 页面会显示 "--"（物理量冻结 + 可信标志，两个概念分开） */
          DBG_Printf("DHT11: FAIL err=%d raw=%02X %02X %02X %02X %02X\r\n",
                    ret, dht.humi_int, dht.humi_dec,
                    dht.temp_int, dht.temp_dec, dht.checksum);
        }

                  /* MQ5 气体（读一次存 g_app_view UI 共用） */
          g_app.mq5_raw   = mq5_raw;
          g_app.mq5_level = mq5_level;
        //  DBG_Printf("MQ5 : raw=%d level=%d\r\n", g_app.mq5_raw, g_app.mq5_level);

          /* 光敏 */
        //  g_app_view.light_raw   = LIGHT_ReadRaw();
        //  g_app_view.light_level = LIGHT_ReadLevel();
        //  DBG_Printf("LIGHT: raw=%d level=%d\r\n", g_app_view.light_raw, g_app_view.light_level);

          /*光敏 : 二值读法*/
          g_app.light_level = light_level;
          // DBG_Printf("LIGHT: level=%d\r\n", g_app.light_level);
          // DBG_Printf("IR: key_evt=%d\r\n", key_evt);

          /* BMP180 气压/温度（oss=2；T 单位 0.1°C，P 单位 Pa） */
          g_app.bmp_t = bmp_t;
          g_app.bmp_p = bmp_p;
          g_app.bmp_ok = bmp_ok;
          if (g_app.bmp_ok)
          {
            // DBG_Printf("BMP180: T=%d.%d C P=%ld Pa\r\n",
            //            (int)(g_app_view.bmp_t / 10), (int)(g_app_view.bmp_t % 10),
            //            (long)g_app_view.bmp_p);  
          }
          else
          {
            DBG_Printf("BMP180: read FAIL\r\n");
          }
          alarm_now    = g_alarm_gas;    /* M3.3: 锁内只拷 1 个标量 */
          set_temp_hi  = g_set.temp_max; /* S2: 回传"当前生效"的阈值 */
          set_sample_ms = g_set.sample_ms;
          g_app.seq++;
          osMutexRelease(xAppMutex);   /* 释放 g_app_view 锁 */

                  /* ---- M3.3 上行: 锁外拼 JSON → 发给 ESP ----
         * ① 数据用锁外已有的局部变量(dht/mq5_level/light_level), 不必再读 g_app
         * ② 2s 一帧 → 天然满足契约"每 5s 至少一帧"的保活要求
         * ③ 拼串 + 发送共约 5ms, 必须放锁外 */
        {
          char        frame[160];    /* 加了 temp_hi/sample_ms 后满帧约 105 字符 */
          int32_t     n;
          const char *alarm_str = (0U != alarm_now) ? "true" : "false";

          if (0 == ret)   /* DHT11 有效才带 temp/humi; 失败时 raw 是垃圾, 不能上云 */
          {
            n = snprintf(frame, sizeof(frame),
                         "{\"temp\":%u.%u,\"humi\":%u.%u,\"light_level\":%u,\"gas_level\":%u,"
                         "\"alarm\":%s,\"temp_hi\":%u.0,\"sample_ms\":%u}",
                         (unsigned)dht.temp_int, (unsigned)dht.temp_dec,
                         (unsigned)dht.humi_int, (unsigned)dht.humi_dec,
                         (unsigned)light_level, (unsigned)mq5_level,
                         alarm_str,
                         (unsigned)set_temp_hi, (unsigned)set_sample_ms);
          }
          else
          {
            n = snprintf(frame, sizeof(frame),
                         "{\"light_level\":%u,\"gas_level\":%u,\"alarm\":%s,"
                         "\"temp_hi\":%u.0,\"sample_ms\":%u}",        /*`%u.0` 里的`.0` 是字面文本 ，不是格式符 ——`%u` 吃掉一个参数后，`.` 和`0` 原样输出，所以`35` 会变成`35.0` 。这就是"整数存的是阈值，但发出去是 double"的桥*/
                         (unsigned)light_level, (unsigned)mq5_level,
                         alarm_str,
                         (unsigned)set_temp_hi, (unsigned)set_sample_ms);
          }

          if ((n > 0) && (n < (int32_t)sizeof(frame)))
          {
            ESP_SendLine(frame, (uint16_t)n);
            DBG_Printf("TX: %s\r\n", frame);  /* 测试期回显, M3.4 验完删 */
          }
        }


        osDelay(2000);  /* 每 2s 采集一次 */
  }
}


/**
  * @brief  控制任务：输出设备初始化 + 驱动自检 + 报警状态机
  * @note   LED/蜂鸣器/继电器的 init 全部搬到这里（谁用谁初始化）——
  *         若 Alarm_Tick 在 BUZZ_Init 之前碰到 PWM 零句柄会 HardFault，
  *         同一任务内顺序执行，先后顺序天然保证
  */
void StartCtrlTask(void const * argument)
{
  LED_Init();
  BUZZ_Init();
  RELAY_Init();

  /* 上电驱动自检（蜂鸣器硬件修好后，报警 beep 本身就是复测） */
  LED_On(LED1);
  osDelay(500);
  LED_Off(LED1);
  osDelay(500);
  LED_On(LED2);
  osDelay(500);
  LED_Off(LED2);
  RELAY_ON();
  osDelay(1000);
  RELAY_OFF();
  osDelay(500);

  Alarm_Init();

  for(;;)
  {
    Alarm_Tick();
    osDelay(100);
  }
}


/* ================= M3.5/S4: 云端下行设置命令处理 =================
 * 数据流: OneNET 下发 → ESP 转成 "SET,key=val\n" → bsp_esp 拆进命令槽
 *         → 这里取走 → 业务校验 → 锁内写 g_set → 锁外回 ACK
 *
 * 三条铁律（全部来自前面踩过的坑）:
 *   ① 写 g_set 必须进 xAppMutex —— CtrlTask 是在锁内读阈值的；
 *   ② 回 ACK 必须在锁外 —— ESP_SendLine 自己会拿 TX 锁，两把锁绝不嵌套；
 *   ③ 落盘不在这里 —— Setting_Save() 是扇区擦写（几十~几百 ms），会把这 20ms 的
 *      UI 循环冻住（同坑 24"慢事不进快循环"）→ 交给 SensorTask，见 S4b。
 *
 * 定义位置有意放在 StartDefaultTask 之前：调用点在同一函数里，
 * 这样天然不用前置声明，避开"定义在使用之后"那个老坑（坑 42）。 */
static void ESP_HandleSetCommand(void)
{
    ESP_SetCmd_t cmd;
    int32_t      v = 0;
    uint8_t      applied = 0U;

    if (0U == ESP_GetSetCommand(&cmd))
    {
        return;                                     /* 当前没有新命令 */
    }

    if (0 == strcmp(cmd.key, "temp_hi"))
    {
        /* 物模型 temp_hi 是 double，云端滑块可能发 40.5 → 四舍五入成整数 °C。
         * 做法：strtol 取整数部分，再看第一位小数决定是否进位（不用 atof，避开浮点库）。
         * 注：合法范围 5~60，负数会在下面的界限检查里被拒，所以不必处理符号。 */
        char   *endp = NULL;
        int32_t ip   = (int32_t)strtol(cmd.val, &endp, 10);
        int32_t add  = ((endp != NULL) && ('.' == *endp) &&
                        (endp[1] >= '5') && (endp[1] <= '9')) ? 1 : 0;

        v = ip + add;

        if ((v >= (int32_t)DL_TEMP_MAX_LO) && (v <= (int32_t)DL_TEMP_MAX_HI))
        {
            osMutexWait(xAppMutex, osWaitForever);
            g_set.temp_max = (uint16_t)v;
            osMutexRelease(xAppMutex);
            applied = 1U;
        }
    }
    else if (0 == strcmp(cmd.key, "sample_ms"))
    {
        v = (int32_t)strtol(cmd.val, NULL, 10);

        if ((v >= (int32_t)SET_SAMPLE_MS_LO) && (v <= (int32_t)SET_SAMPLE_MS_HI))
        {
            osMutexWait(xAppMutex, osWaitForever);
            g_set.sample_ms = (uint16_t)v;
            osMutexRelease(xAppMutex);
            applied = 1U;
        }
    }
    /* 其它键名：applied 保持 0 → 走下面的拒绝分支 */

    if (0U == applied)
    {
        /* 未知键 / 越界 → 不回 ACK。
         * 理由：回 ACK 等于骗云端"已生效"；不回 ACK，云端的 3s 超时才是真实的失败信号。 */
        DBG_Printf("SET: REJECT key=%s val=%s\r\n", cmd.key, cmd.val);
        return;
    }

    /* S4b: 标记待落盘。真正的 Setting_Save() 由 SensorTask 圈首执行 ——
     * 这里的职责只到"改内存 + 回 ACK"为止（ACK 3s 期限内必须出去）。 */
    g_set_dirty = 1U;

    /* 回"实际生效值"（不是请求值）—— 让云端显示的与设备真正执行的一致。
     * 例：云端发 40.5，设备四舍五入成 41，就回 ACK,temp_hi=41。 */
    {
        char ack[64];
        int  n = snprintf(ack, sizeof(ack), "ACK,%s=%ld", cmd.key, (long)v);

        if ((n > 0) && (n < (int)sizeof(ack)))
        {
            ESP_SendLine(ack, (uint16_t)n);         /* 自带 TX 仲裁锁，此处无需再锁 */
            DBG_Printf("SET: %s -> %ld (ACK sent)\r\n", cmd.key, (long)v);
        }
    }
}


/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void const * argument)
{
  /* USER CODE BEGIN StartDefaultTask */
    

  uint8_t  key_evt;
  IR_Data_t ir_data;   /* 红外解码结果（infrared.h → ir_decode.h） */

  IR_Init();    /* NEC 红外接收（EXTI9+DWT，依赖 DWT 已启动，main.c 里已 BSP_Delay_Init） */

  DBG_Printf("\r\n========= Home IoT Node Start (v1.1) =========\r\n");  /* 版本号: OTA 前后对比升级效果的视觉证据 (M9.3d) */

  /* M4: W25Q64 自检（上电跑一次，占用扇区0） */
  W25Q64_SelfTest();

  /* LCD 初始化 + 背光 */
  LCD_Init();
  LCD_LED_ON();

  /* 阈值：上电从 Flash 恢复（无效则用默认值） */
  Setting_Load();

  /* 页面管理器启动：画第一屏（仪表盘） */
  GUI_Page_Init();


  /* 主循环节拍：20ms 一圈
   *  - 每 2s  采集一次传感器 → 写 g_app_view
   *  - 每 20ms 扫按键 + 刷新当前页面（消费者）
   * 这就是"分频调度"：不同周期的事在同一个循环里用时间戳分流 */
  for(;;) 
  {



      /* ESP 链路: 非阻塞排空环形缓冲 → 拼行 → 白名单过滤 */
      ESP_PollLine();
      ESP_HandleSetCommand();     /* M3.5/S4: 处理云端下行设置命令(必须在 ESP_PollLine 之后) */
      FWOTA_Poll();               /* M9.3d: OTA 会话状态机(取 FW 行/擦 staging/超时看门狗) */
  
    /* ---- 快事：按键事件 → 页面 → 刷新 ----
     * 两个输入源统一成 KEY_EVT_* 事件码：板载按键 + 红外遥控（上/下/OK） */
    key_evt = KEY_Scan();
    if (key_evt == KEY_EVT_NONE)
    {
      key_evt = IR_GetKeyEvent();   /* 板载无事件再问遥控（读-清语义，不丢帧） */
    }
        /* ★ 报警拦截：短按 OK = 消音，事件吃掉（页面不响应） */
    if ((key_evt == KEY_EVT_3) && (g_alarm_gas != 0U) && (g_buzzer_mute == 0U))
    {
        g_buzzer_mute = 1U;
        key_evt = KEY_EVT_NONE;
    }

    if (key_evt != KEY_EVT_NONE)
    {
      GUI_Page_OnKey(key_evt);
    }

    osMutexWait(xAppMutex, osWaitForever);  /* 保护 g_app_view，避免被其他任务修改 */
    g_app_view = g_app; /* 复制 g_app 到 g_app_view */
    osMutexRelease(xAppMutex);   /* 释放 g_app_view 锁 */
    
    GUI_Page_Update();

    osDelay(20);   /* 节拍：按键消抖 + 降低功耗，页面更新足够跟手 */
  }
  /* USER CODE END StartDefaultTask */
}


/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

