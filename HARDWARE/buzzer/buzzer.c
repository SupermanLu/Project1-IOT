/**
  ******************************************************************************
  * @file    buzzer.c
  * @brief   蜂鸣器驱动（无源，TIM4_CH1 = PB6 输出 2.7kHz 方波）
  * @note    硬件（原理图确认）：PB6 → R10 1kΩ → S9014 NPN 基极，
  *                射极接地、集电极接蜂鸣器负端、蜂鸣器正端接 3V3
  *                → NPN 低边开关 = 高电平触发！
  *                （此前"M0 时代"记录的"低电平触发"是未验证假设——
  *                 GPIO 静态电平下无源片只咔不响，正反极性听不出差别，
  *                 直到画原理图才定案。教训：触发极性以原理图为准）
  *          原理：无源蜂鸣器是压电陶瓷片，必须施加交变电压才能振动发声，
  *                静态电平只会"咔"一声，所以用 TIM4 PWM 出方波。
  *          频率算术：定时器时钟 72MHz（APB1 36MHz ×2）
  *                    PSC=71  → 计数频率 72MHz/(71+1) = 1MHz（1 计数 = 1µs）
  *                    ARR=369 → 周期 (369+1)µs = 370µs → 2702Hz ≈ 2.7kHz
  *                    （铭牌谐振频率，在此频率下声压最大）
  *          极性：OCPolarity=HIGH + PWM1 模式（CNT<CCR 输出高）
  *                → CCR = 高电平持续时间(µs)：
  *                  CCR=185 → 高185µs+低185µs = 50% 方波（发声）
  *                  CCR=0   → 恒低 = NPN 截止 = 静音
  *          自包含：时钟/GPIO/定时器全在本文件配好，不依赖 CubeMX。
  *          注意：BUZZ_On/Off/Toggle 必须在 BUZZ_Init 之后调用。
  ******************************************************************************
  */
#include "buzzer.h"

/* ---- 引脚与定时器参数（自包含宏，改硬件只动这里） ---- */
#define BUZZ_GPIO_PORT   GPIOB
#define BUZZ_GPIO_PIN    GPIO_PIN_6    /* PB6 = TIM4_CH1 默认映射，无需 remap */
#define BUZZ_TIM         TIM4
#define BUZZ_TIMER_PSC   71u           /* 72MHz/(71+1) = 1MHz：1 计数 = 1µs */
#define BUZZ_TIMER_ARR   369u          /* (369+1)µs 周期 → 2702Hz ≈ 2.7kHz 谐振 */
#define BUZZ_DUTY_ON     185u          /* 发声：高 185µs + 低 185µs = 50% 方波 */
#define BUZZ_DUTY_OFF    0u            /* 静音：恒低（NPN 截止） */

static TIM_HandleTypeDef s_htim4;      /* TIM4 句柄（本驱动私有，不进 CubeMX） */

void BUZZ_Init(void)
{
    GPIO_InitTypeDef   gpio = {0};
    TIM_OC_InitTypeDef oc   = {0};

    __HAL_RCC_TIM4_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* PB6：复用推挽——F1 里引脚控制权交给片上外设（TIM4）就是这个模式 */
    gpio.Pin   = BUZZ_GPIO_PIN;
    gpio.Mode  = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BUZZ_GPIO_PORT, &gpio);

    /* 时基：1MHz 计数 + 370 计数周期 = 2.7kHz */
    s_htim4.Instance       = BUZZ_TIM;
    s_htim4.Init.Prescaler = BUZZ_TIMER_PSC;
    s_htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
    s_htim4.Init.Period    = BUZZ_TIMER_ARR;
    s_htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    if (HAL_TIM_PWM_Init(&s_htim4) != HAL_OK)
    {
        return;   /* 初始化失败保持静音（CCR 未配置为发声值） */
    }

    /* 通道：PWM1 + 高极性 → CCR 即"高电平时间"；初始 CCR=0 恒低静音 */
    oc.OCMode     = TIM_OCMODE_PWM1;
    oc.Pulse      = BUZZ_DUTY_OFF;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    HAL_TIM_PWM_ConfigChannel(&s_htim4, &oc, TIM_CHANNEL_1);

    /* 启动 PWM 输出：之后发声/静音只改 CCR，硬件自己跑，零 CPU 开销 */
    HAL_TIM_PWM_Start(&s_htim4, TIM_CHANNEL_1);
}

void BUZZ_On(void)
{
    __HAL_TIM_SET_COMPARE(&s_htim4, TIM_CHANNEL_1, BUZZ_DUTY_ON);
}

void BUZZ_Off(void)
{
    __HAL_TIM_SET_COMPARE(&s_htim4, TIM_CHANNEL_1, BUZZ_DUTY_OFF);
}

void BUZZ_Toggle(void)
{
    static uint8_t s_on = 0;

    if (s_on != 0U)
    {
        BUZZ_Off();
        s_on = 0U;
    }
    else
    {
        BUZZ_On();
        s_on = 1U;
    }
}
