/**
 ******************************************************************************
 * @file    bsp_iwdg.h
 * @brief   独立看门狗 IWDG 驱动 — 系统跑飞后自动复位
 ******************************************************************************
 */
#ifndef BSP_IWDG_H
#define BSP_IWDG_H

#include "module_cfg.h"

#if BSP_IWDG_ENABLE

#include <stdint.h>

void BSP_IWDG_Init(uint32_t timeout_ms);
void BSP_IWDG_Refresh(void);

#endif /* BSP_IWDG_ENABLE */
#endif /* BSP_IWDG_H */
