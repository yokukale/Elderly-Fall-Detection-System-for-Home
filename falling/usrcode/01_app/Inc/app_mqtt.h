/**
 ******************************************************************************
 * @file    app_mqtt.h
 * @brief   MQTT 上云任务头文件 — ESP-01S + Paho MQTT 连接华为云 IoTDA
 *
 *  华为云主题：
 *    上报属性(发布): $oc/devices/{device_id}/sys/properties/report
 *    接收命令(订阅): $oc/devices/{device_id}/sys/commands/#
 *    命令响应(发布): $oc/devices/{device_id}/sys/commands/response/request_id={rid}
 *
 *  华为云物模型服务（FallDetect）：
 *    属性：fall_state(int), confidence(decimal), bbox(array[4]), beep(int), vision_offline(int)
 *    命令：MUTE{} / RESET{}
 ******************************************************************************
 */
#ifndef APP_MQTT_H
#define APP_MQTT_H

#include <stdint.h>

/* ==========================================================================
 *  ★ 用户配置区 —— 请根据实际 WiFi 与华为云 IoTDA 环境修改 ★
 * ========================================================================== */

/* --- WiFi --- */
#define MQTT_WIFI_SSID          "2"
#define MQTT_WIFI_PASSWORD      "11111111"

/* --- 华为云 IoTDA 接入参数 --- */
#define MQTT_BROKER_HOST        "f37725de82.st1.iotda-device.cn-north-4.myhuaweicloud.com"
#define MQTT_BROKER_PORT        1883U
#define MQTT_CLIENT_ID          "6af6a443cbb0cf6bb9970fe2_fall_stm32_dev01_0_0_2026111310"
#define MQTT_USERNAME           "6af6a443cbb0cf6bb9970fe2_fall_stm32_dev01"
#define MQTT_PASSWORD           "5964bf1041eacc72f2f5582a26d3b63c71001950dc0df9343d8f53f2b84f3053"
#define MQTT_KEEPALIVE_SEC      30U

#define HUAWEI_DEVICE_ID        "6af6a443cbb0cf6bb9970fe2_fall_stm32_dev01"
#define HUAWEI_SERVICE_ID       "FallDetect"

/* --- 运行参数 --- */
#define MQTT_QOS                1
#define MQTT_OFFLINE_CACHE_MAX  16U
#define MQTT_PUBLISH_PERIOD_MS  5000U   /*!< 5s 心跳上报周期（跌倒事件立即触发上报） */
#define MQTT_RECONNECT_DELAY_MS 1000U
#define MQTT_YIELD_TIMEOUT_MS   200

/* --- 任务参数 --- */
#define MQTT_TASK_STACK_SIZE    (1536U * 4U)
#define MQTT_TASK_PRIORITY      osPriorityNormal

/* ==========================================================================
 *  API
 * ========================================================================== */
void    APP_MQTT_Task(void *argument);
uint8_t APP_MQTT_GetStatus(void);
uint8_t APP_MQTT_Publish(const uint8_t *payload, uint16_t len);

#endif /* APP_MQTT_H */
