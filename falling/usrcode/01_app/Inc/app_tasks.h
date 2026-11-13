/**
 ******************************************************************************
 * @file    app_tasks.h
 * @brief   应用任务管理头文件 — 所有FreeRTOS任务的创建入口
 *
 *  集成点（CubeMX USER CODE区域）：
 *    1. APP_Init()                -> freertos.c MX_FREERTOS_Init Init
 *    2. APP_TASKS_CreateObjects() -> freertos.c RTOS_QUEUES
 *    3. APP_TASKS_CreateTasks()   -> freertos.c RTOS_THREADS
 ******************************************************************************
 */
#ifndef APP_TASKS_H
#define APP_TASKS_H

void APP_Init(void);
void APP_TASKS_CreateObjects(void);
void APP_TASKS_CreateTasks(void);

#endif /* APP_TASKS_H */
