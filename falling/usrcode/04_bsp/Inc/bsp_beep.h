/**
 ******************************************************************************
 * @file    bsp_beep.h
 * @brief   蜂鸣器驱动头文件 — PA5（支持 TIM2_CH1 PWM 音调输出与 GPIO 电平开关）
 ******************************************************************************
 */
#ifndef BSP_BEEP_H
#define BSP_BEEP_H

#include "module_cfg.h"

#if BSP_BEEP_ENABLE

#include <stdint.h>

void    BSP_BEEP_Init(void);
void    BSP_BEEP_SetTone(uint16_t freq_hz);
void    BSP_BEEP_On(void);
void    BSP_BEEP_Off(void);
void    BSP_BEEP_Toggle(void);
uint8_t BSP_BEEP_GetState(void);
void    BSP_BEEP_Beep(uint32_t ms);

#endif /* BSP_BEEP_ENABLE */
#endif /* BSP_BEEP_H */
