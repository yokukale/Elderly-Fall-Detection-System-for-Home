/**
 ******************************************************************************
 * @file    bsp_iwdg.c
 * @brief   独立看门狗 IWDG 驱动（配置由 Core/Src/iwdg.c 完成）
 ******************************************************************************
 */
#include "module_cfg.h"

#if BSP_IWDG_ENABLE

#include "bsp_iwdg.h"
#include "iwdg.h"

void BSP_IWDG_Init(uint32_t timeout_ms)
{
    (void)timeout_ms;
}

void BSP_IWDG_Refresh(void)
{
    MX_IWDG_Refresh();
}

#endif /* BSP_IWDG_ENABLE */
