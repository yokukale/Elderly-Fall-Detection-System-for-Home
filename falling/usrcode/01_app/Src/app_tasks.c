/**
 ******************************************************************************
 * @file    app_tasks.c
 * @brief   应用任务管理 — 集中定义所有 FreeRTOS 任务与对象
 *
 *  任务划分（对照开发文档 §6.2）：
 *    - WatchdogTask  : 多任务心跳检测 + IWDG 喂狗
 *    - FrameRxTask   : USART1 读取 K230 数据 -> 帧解析状态机 -> 投递队列
 *    - FallJudgeTask : 队列收帧 -> 滑动窗口判决 -> 触发报警/冷却/掉线检测
 *    - AlarmTask     : PA5 蜂鸣器本地报警控制
 *    - MqttTask      : ESP-01S + Paho MQTT 属性上报 / 命令订阅
 ******************************************************************************
 */
#include "module_cfg.h"

#if APP_TASKS_ENABLE

#include "app_tasks.h"
#include "bsp_uart.h"
#if BSP_BEEP_ENABLE
#include "bsp_beep.h"
#endif
#if BSP_ESP8266_ENABLE
#include "bsp_esp8266.h"
#endif
#if BSP_IWDG_ENABLE
#include "iwdg.h"
#endif
#if PROTOCOL_FALL_ENABLE
#include "frame_proto.h"
#endif
#if APP_FALL_ENABLE
#include "app_fall.h"
#endif
#if APP_ALARM_ENABLE
#include "app_alarm.h"
#endif
#if APP_MQTT_ENABLE
#include "app_mqtt.h"
#endif
#if APP_WATCHDOG_ENABLE
#include "app_watchdog.h"
#endif
#if SVC_CONN_ENABLE && PLAT_SVCMGR_ENABLE
#include "plat_mgr.h"
#include "svc_conn.h"
#endif

#include "main.h"
#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "queue.h"

/* ==========================================================================
 *  RTOS 对象与任务句柄
 * ========================================================================== */
#if APP_FALL_ENABLE && PROTOCOL_FALL_ENABLE
#define FALL_FRAME_QUEUE_LEN  16U

static QueueHandle_t        s_fall_frame_queue;
static frame_proto_parser_t s_frame_parser;

static osThreadId_t s_frameRxTaskHandle;
static osThreadId_t s_fallJudgeTaskHandle;

static const osThreadAttr_t s_frameRxTask_attr = {
    .name       = "FrameRxTask",
    .stack_size = 512U * 4U,
    .priority   = (osPriority_t)osPriorityHigh,
};

static const osThreadAttr_t s_fallJudgeTask_attr = {
    .name       = "FallJudgeTask",
    .stack_size = 512U * 4U,
    .priority   = (osPriority_t)osPriorityHigh,
};
#endif /* APP_FALL_ENABLE && PROTOCOL_FALL_ENABLE */

#if APP_ALARM_ENABLE
static osThreadId_t s_alarmTaskHandle;
static const osThreadAttr_t s_alarmTask_attr = {
    .name       = "AlarmTask",
    .stack_size = ALARM_TASK_STACK_SIZE,
    .priority   = (osPriority_t)ALARM_TASK_PRIORITY,
};
#endif

#if APP_MQTT_ENABLE && BSP_ESP8266_ENABLE
static osThreadId_t s_mqttTaskHandle;
static const osThreadAttr_t s_mqttTask_attr = {
    .name       = "MqttTask",
    .stack_size = MQTT_TASK_STACK_SIZE,
    .priority   = (osPriority_t)MQTT_TASK_PRIORITY,
};
#endif

#if APP_WATCHDOG_ENABLE
static osThreadId_t s_watchdogTaskHandle;
static const osThreadAttr_t s_watchdogTask_attr = {
    .name       = "WatchdogTask",
    .stack_size = 512U * 4U,
    .priority   = (osPriority_t)osPriorityAboveNormal,
};
#endif

#if SVC_CONN_ENABLE && APP_MQTT_ENABLE
static const svc_conn_engine_t s_mqtt_conn_engine = {
    .is_connected = APP_MQTT_GetStatus,
    .publish      = APP_MQTT_Publish
};
#endif

/* ==========================================================================
 *  任务函数
 * ========================================================================== */
#if APP_FALL_ENABLE && PROTOCOL_FALL_ENABLE
static void APP_FrameRx_Task(void *argument)
{
    (void)argument;
    uint8_t      rx_buf[64];
    fall_frame_t frame;

    for (;;) {
#if BSP_UART1_ENABLE
        uint32_t n = BSP_UART1_Read(rx_buf, sizeof(rx_buf));
        for (uint32_t i = 0U; i < n; i++) {
            if (FrameProto_FeedByte(&s_frame_parser, rx_buf[i], &frame) != 0U) {
                (void)xQueueSend(s_fall_frame_queue, &frame, 0U);
            }
        }
#endif
#if APP_WATCHDOG_ENABLE
        Watchdog_Kick(WDT_TASK_FRAME_RX);
#endif
        osDelay(10U);
    }
}

static void APP_FallJudge_Task(void *argument)
{
    (void)argument;
    fall_frame_t frame;

    for (;;) {
        if (xQueueReceive(s_fall_frame_queue, &frame, pdMS_TO_TICKS(100U)) == pdPASS) {
            APP_FALL_ProcessFrame(&frame);
        }
        APP_FALL_OnTick();
#if APP_WATCHDOG_ENABLE
        Watchdog_Kick(WDT_TASK_FALL_JUDGE);
#endif
    }
}
#endif /* APP_FALL_ENABLE && PROTOCOL_FALL_ENABLE */

/* ==========================================================================
 *  生命周期入口（由 freertos.c 调用）
 * ========================================================================== */
void APP_Init(void)
{
#if BSP_IWDG_ENABLE
    MX_IWDG_Refresh();
#endif

#if BSP_UART1_ENABLE
    BSP_UART1_Init();
#endif
#if BSP_UART3_ENABLE
    BSP_UART3_Init();
#endif
#if BSP_BEEP_ENABLE
    BSP_BEEP_Init();
#endif
#if BSP_UART2_DEBUG_ENABLE
    BSP_UART2_Printf("[BOOT] Waiting ESP8266 power-on...\r\n");
#endif
#if BSP_ESP8266_ENABLE
    BSP_ESP8266_Init();
#endif
#if BSP_IWDG_ENABLE
    MX_IWDG_Refresh();
#endif

#if PROTOCOL_FALL_ENABLE
    FrameProto_Init(&s_frame_parser);
#endif
#if APP_FALL_ENABLE
    APP_FALL_Init();
#endif

#if SVC_CONN_ENABLE && PLAT_SVCMGR_ENABLE
    (void)svc_conn_register();
    (void)plat_svcmgr_init_all();
    (void)plat_svcmgr_start_all();
#if APP_MQTT_ENABLE
    svc_conn_set_engine(&s_mqtt_conn_engine);
#endif
#endif

#if BSP_UART2_DEBUG_ENABLE
    BSP_UART2_Printf("[BOOT] Fall detection system initialized\r\n");
#endif
}

void APP_TASKS_CreateObjects(void)
{
#if APP_FALL_ENABLE && PROTOCOL_FALL_ENABLE
    s_fall_frame_queue = xQueueCreate(FALL_FRAME_QUEUE_LEN, sizeof(fall_frame_t));
    if (s_fall_frame_queue == NULL) {
        Error_Handler();
    }
#endif
}

void APP_TASKS_CreateTasks(void)
{
#if APP_WATCHDOG_ENABLE
    s_watchdogTaskHandle = osThreadNew(APP_Watchdog_Task, NULL, &s_watchdogTask_attr);
    if (s_watchdogTaskHandle == NULL) {
#if BSP_UART2_DEBUG_ENABLE
        BSP_UART2_Printf("[FATAL] Watchdog task create failed\r\n");
#endif
        for (;;) {
            MX_IWDG_Refresh();
            HAL_Delay(1000U);
        }
    }
#endif

#if APP_FALL_ENABLE && PROTOCOL_FALL_ENABLE
    s_frameRxTaskHandle = osThreadNew(APP_FrameRx_Task, NULL, &s_frameRxTask_attr);
#if APP_WATCHDOG_ENABLE
    if (s_frameRxTaskHandle != NULL) { Watchdog_Register(WDT_TASK_FRAME_RX); }
#endif

    s_fallJudgeTaskHandle = osThreadNew(APP_FallJudge_Task, NULL, &s_fallJudgeTask_attr);
#if APP_WATCHDOG_ENABLE
    if (s_fallJudgeTaskHandle != NULL) { Watchdog_Register(WDT_TASK_FALL_JUDGE); }
#endif
#endif

#if APP_ALARM_ENABLE
    s_alarmTaskHandle = osThreadNew(APP_ALARM_Task, NULL, &s_alarmTask_attr);
#if APP_WATCHDOG_ENABLE
    if (s_alarmTaskHandle != NULL) { Watchdog_Register(WDT_TASK_ALARM); }
#endif
#endif

#if APP_MQTT_ENABLE && BSP_ESP8266_ENABLE
    s_mqttTaskHandle = osThreadNew(APP_MQTT_Task, NULL, &s_mqttTask_attr);
#if APP_WATCHDOG_ENABLE
    if (s_mqttTaskHandle != NULL) { Watchdog_Register(WDT_TASK_MQTT); }
#endif
#endif
}

#endif /* APP_TASKS_ENABLE */
