/**
 ******************************************************************************
 * @file    app_watchdog.c
 * @brief   多任务心跳检测 + IWDG 喂狗
 ******************************************************************************
 */
#include "module_cfg.h"

#if APP_WATCHDOG_ENABLE

#include "app_watchdog.h"
#include "bsp_iwdg.h"
#include "cmsis_os.h"
#if BSP_UART2_DEBUG_ENABLE
#include "bsp_uart.h"
#endif

static volatile uint32_t s_heartbeat[WDT_TASK_COUNT];
static uint32_t          s_prev_heartbeat[WDT_TASK_COUNT];
static uint8_t           s_stall_count[WDT_TASK_COUNT];
static uint8_t           s_task_active[WDT_TASK_COUNT];
static uint8_t           s_active_count = 0U;

#define WDT_STALL_THRESHOLD 5U

void Watchdog_Kick(WDT_TaskID_t id)
{
    if ((uint32_t)id < WDT_TASK_COUNT) {
        s_heartbeat[id]++;
    }
}

void Watchdog_Register(WDT_TaskID_t id)
{
    if ((uint32_t)id < WDT_TASK_COUNT) {
        if (s_task_active[id] == 0U) {
            s_task_active[id] = 1U;
            s_active_count++;
        }
    }
}

void Watchdog_DelayWithKick(WDT_TaskID_t id, uint32_t ms)
{
    uint32_t remaining = ms;
    while (remaining > 0U) {
        uint32_t slice = (remaining > 500U) ? 500U : remaining;
        osDelay(slice);
        Watchdog_Kick(id);
        remaining -= slice;
    }
}

void APP_Watchdog_Task(void *argument)
{
    (void)argument;
    uint32_t i;
    uint8_t  all_alive;
    uint32_t grace_cycles = 5U;

    BSP_IWDG_Refresh();

    for (i = 0U; i < WDT_TASK_COUNT; i++) {
        s_prev_heartbeat[i] = s_heartbeat[i];
    }

#if BSP_UART2_DEBUG_ENABLE
    BSP_UART2_Printf("[WDT] Watchdog started, timeout=3s, monitoring %u/%u tasks\r\n",
                     (unsigned)s_active_count, (unsigned)WDT_TASK_COUNT);
#endif

    for (;;) {
        osDelay(1000U);

        if (grace_cycles > 0U) {
            grace_cycles--;
            BSP_IWDG_Refresh();
            continue;
        }

        all_alive = 1U;
        for (i = 0U; i < WDT_TASK_COUNT; i++) {
            if (s_task_active[i] == 0U) {
                continue;
            }
            if (s_heartbeat[i] == s_prev_heartbeat[i]) {
                s_stall_count[i]++;
                if (s_stall_count[i] >= WDT_STALL_THRESHOLD) {
                    all_alive = 0U;
#if BSP_UART2_DEBUG_ENABLE
                    BSP_UART2_Printf("[WDT] Task %u stalled %us, skip feed\r\n",
                                     (unsigned)i, (unsigned)s_stall_count[i]);
#endif
                    break;
                }
            } else {
                s_stall_count[i] = 0U;
            }
            s_prev_heartbeat[i] = s_heartbeat[i];
        }

        if (all_alive != 0U) {
            BSP_IWDG_Refresh();
        }
    }
}

#endif /* APP_WATCHDOG_ENABLE */
