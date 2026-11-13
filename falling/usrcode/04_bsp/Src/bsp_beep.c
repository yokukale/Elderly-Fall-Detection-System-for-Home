/**
 ******************************************************************************
 * @file    bsp_beep.c
 * @brief   蜂鸣器驱动实现 — PA5（M4核心板 v1.2，Q1三极管驱动）
 *
 *  - BSP_BEEP_PWM_ENABLE=1：复用 TIM2_CH1 (PA5 GPIO_AF1_TIM2) 输出 1MHz 时基 PWM 方波，
 *    支持通过 BSP_BEEP_SetTone(freq_hz) 播放不同音高旋律；静音/停响时切回推挽低电平。
 *  - BSP_BEEP_PWM_ENABLE=0：纯 GPIO 高低电平模式，SetTone(>0) 等效于直流高电平导通，
 *    可让无源/有源切换只需改 module_cfg.h 一行宏。
 ******************************************************************************
 */
#include "module_cfg.h"

#if BSP_BEEP_ENABLE

#include "bsp_beep.h"
#include "main.h"

static uint8_t s_beep_on = 0U;

#if BSP_BEEP_PWM_ENABLE

static TIM_HandleTypeDef s_htim2;
static uint8_t           s_tim2_ready = 0U;
static uint8_t           s_pwm_running = 0U;
static uint8_t           s_pin_is_af = 0U;

static void beep_pin_to_gpio(void)
{
    GPIO_InitTypeDef gpio = {0};

    if (s_pin_is_af == 0U) {
        return;
    }

    HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, GPIO_PIN_RESET);
    gpio.Pin   = BEEP_Pin;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BEEP_GPIO_Port, &gpio);
    s_pin_is_af = 0U;
}

static void beep_pin_to_af(void)
{
    GPIO_InitTypeDef gpio = {0};

    if (s_pin_is_af != 0U) {
        return;
    }

    gpio.Pin       = BEEP_Pin;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_PULLDOWN;
    gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF1_TIM2;
    HAL_GPIO_Init(BEEP_GPIO_Port, &gpio);
    s_pin_is_af = 1U;
}

static uint32_t beep_get_tim2_clk_hz(void)
{
    RCC_ClkInitTypeDef clk_cfg;
    uint32_t           flash_latency;
    uint32_t           pclk1 = HAL_RCC_GetPCLK1Freq();

    HAL_RCC_GetClockConfig(&clk_cfg, &flash_latency);
    return (clk_cfg.APB1CLKDivider == RCC_HCLK_DIV1) ? pclk1 : (2UL * pclk1);
}

static void beep_tim2_init(void)
{
    TIM_OC_InitTypeDef oc = {0};
    uint32_t           tim_clk;
    uint32_t           psc;

    if (s_tim2_ready != 0U) {
        return;
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_TIM2_CLK_ENABLE();

    tim_clk = beep_get_tim2_clk_hz();
    psc     = (tim_clk >= 1000000U) ? ((tim_clk / 1000000U) - 1U) : 0U;

    s_htim2.Instance               = TIM2;
    s_htim2.Init.Prescaler         = psc;
    s_htim2.Init.CounterMode       = TIM_COUNTERMODE_UP;
    s_htim2.Init.Period            = 1000U - 1U;
    s_htim2.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    s_htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_PWM_Init(&s_htim2) != HAL_OK) {
        return;
    }

    oc.OCMode     = TIM_OCMODE_PWM1;
    oc.Pulse      = 0U;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&s_htim2, &oc, TIM_CHANNEL_1) != HAL_OK) {
        return;
    }

    s_tim2_ready = 1U;
}

void BSP_BEEP_Init(void)
{
    beep_tim2_init();
    BSP_BEEP_Off();
}

void BSP_BEEP_SetTone(uint16_t freq_hz)
{
    uint32_t period;
    uint32_t arr;
    uint32_t ccr;

    if (freq_hz == 0U) {
        BSP_BEEP_Off();
        return;
    }

    beep_tim2_init();
    if (s_tim2_ready == 0U) {
        return;
    }

    /* 1MHz 计数器下限制合理音频范围，避免 period < 4 导致计数过短 */
    if (freq_hz < 50U) {
        freq_hz = 50U;
    } else if (freq_hz > 10000U) {
        freq_hz = 10000U;
    }

    period = 1000000U / (uint32_t)freq_hz;
    if (period < 4U) {
        period = 4U;
    }
    arr = period - 1U;
    ccr = period / 2U;

    beep_pin_to_af();

    __HAL_TIM_SET_AUTORELOAD(&s_htim2, arr);
    __HAL_TIM_SET_COMPARE(&s_htim2, TIM_CHANNEL_1, ccr);
    __HAL_TIM_SET_COUNTER(&s_htim2, 0U);
    TIM2->EGR = TIM_EGR_UG;

    if (s_pwm_running == 0U) {
        if (HAL_TIM_PWM_Start(&s_htim2, TIM_CHANNEL_1) == HAL_OK) {
            s_pwm_running = 1U;
        }
    }
    s_beep_on = 1U;
}

void BSP_BEEP_On(void)
{
    if (s_pwm_running != 0U) {
        (void)HAL_TIM_PWM_Stop(&s_htim2, TIM_CHANNEL_1);
        s_pwm_running = 0U;
    }
    beep_pin_to_gpio();
    HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, GPIO_PIN_SET);
    s_beep_on = 1U;
}

void BSP_BEEP_Off(void)
{
    if (s_pwm_running != 0U) {
        (void)HAL_TIM_PWM_Stop(&s_htim2, TIM_CHANNEL_1);
        s_pwm_running = 0U;
    }
    beep_pin_to_gpio();
    HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, GPIO_PIN_RESET);
    s_beep_on = 0U;
}

#else /* !BSP_BEEP_PWM_ENABLE */

void BSP_BEEP_Init(void)
{
    BSP_BEEP_Off();
}

void BSP_BEEP_SetTone(uint16_t freq_hz)
{
    if (freq_hz != 0U) {
        BSP_BEEP_On();
    } else {
        BSP_BEEP_Off();
    }
}

void BSP_BEEP_On(void)
{
    HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, GPIO_PIN_SET);
    s_beep_on = 1U;
}

void BSP_BEEP_Off(void)
{
    HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, GPIO_PIN_RESET);
    s_beep_on = 0U;
}

#endif /* BSP_BEEP_PWM_ENABLE */

void BSP_BEEP_Toggle(void)
{
    if (s_beep_on != 0U) {
        BSP_BEEP_Off();
    } else {
        BSP_BEEP_On();
    }
}

uint8_t BSP_BEEP_GetState(void)
{
    return s_beep_on;
}

void BSP_BEEP_Beep(uint32_t ms)
{
#if BSP_BEEP_PWM_ENABLE
    BSP_BEEP_SetTone(659U);
#else
    BSP_BEEP_On();
#endif
    HAL_Delay(ms);
    BSP_BEEP_Off();
}

#endif /* BSP_BEEP_ENABLE */
