/**
 ******************************************************************************
 * @file    app_alarm.h
 * @brief   本地报警执行模块头文件 — PA5 有源蜂鸣器控制
 ******************************************************************************
 */
#ifndef APP_ALARM_H
#define APP_ALARM_H

#include "module_cfg.h"

#if APP_ALARM_ENABLE

#include <stdint.h>

#define ALARM_TASK_STACK_SIZE   (512U * 4U)
#define ALARM_TASK_PRIORITY     osPriorityAboveNormal

void APP_ALARM_Task(void *argument);

#endif /* APP_ALARM_ENABLE */
#endif /* APP_ALARM_H */
