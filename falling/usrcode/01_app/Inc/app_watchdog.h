/**
 ******************************************************************************
 * @file    app_watchdog.h
 * @brief   多任务心跳检测 — 所有关键任务心跳正常才喂狗，任一任务卡死则触发看门狗复位
 ******************************************************************************
 */
#ifndef APP_WATCHDOG_H
#define APP_WATCHDOG_H

#include "module_cfg.h"

#if APP_WATCHDOG_ENABLE

#include <stdint.h>

typedef enum {
    WDT_TASK_FRAME_RX   = 0,  /* K230 串口收帧解析任务 */
    WDT_TASK_FALL_JUDGE = 1,  /* 跌倒滑动窗口判决任务 */
    WDT_TASK_ALARM      = 2,  /* 本地蜂鸣器报警任务 */
    WDT_TASK_MQTT       = 3,  /* MQTT 上云任务 */
    WDT_TASK_COUNT      = 4
} WDT_TaskID_t;

void Watchdog_Kick(WDT_TaskID_t id);
void Watchdog_Register(WDT_TaskID_t id);
void Watchdog_DelayWithKick(WDT_TaskID_t id, uint32_t ms);
void APP_Watchdog_Task(void *argument);

#endif /* APP_WATCHDOG_ENABLE */
#endif /* APP_WATCHDOG_H */
