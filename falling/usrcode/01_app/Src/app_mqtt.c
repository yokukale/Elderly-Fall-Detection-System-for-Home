/**
 ******************************************************************************
 * @file    app_mqtt.c
 * @brief   MQTT 上云任务实现 — ESP-01S + Paho MQTT 连接华为云 IoTDA
 ******************************************************************************
 */
#include "module_cfg.h"

#if APP_MQTT_ENABLE && APP_TASKS_ENABLE && BSP_ESP8266_ENABLE

#include "app_mqtt.h"
#include "mqtt_port.h"
#include "bsp_esp8266.h"
#include "bsp_uart.h"
#if APP_FALL_ENABLE
#include "app_fall.h"
#endif
#if APP_WATCHDOG_ENABLE
#include "app_watchdog.h"
#endif
#include "cmsis_os.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#if APP_WATCHDOG_ENABLE
void BSP_ESP8266_WaitHook(void)
{
    Watchdog_Kick(WDT_TASK_MQTT);
}
#endif

/* ==========================================================================
 *  内部资源
 * ========================================================================== */
static MQTTClient      s_mqtt_client;
static Network         s_mqtt_network;
static unsigned char   s_mqtt_sendbuf[512];
static unsigned char   s_mqtt_readbuf[512];

static volatile uint8_t s_mqtt_status = 0U;  /* bit0=WiFi, bit1=TCP, bit2=MQTT */

static char    s_topic_pub[128];
static char    s_topic_sub[128];
static char    s_topic_resp[160];

static char    s_offline_buf[MQTT_OFFLINE_CACHE_MAX][384];
static uint16_t s_offline_len[MQTT_OFFLINE_CACHE_MAX];
static uint8_t s_offline_head  = 0U;
static uint8_t s_offline_tail  = 0U;
static uint8_t s_offline_count = 0U;

#define MQTT_EXT_PUB_QUEUE_MAX  8U
#define MQTT_EXT_PUB_LEN_MAX    384U
static uint8_t  s_ext_pub_buf[MQTT_EXT_PUB_QUEUE_MAX][MQTT_EXT_PUB_LEN_MAX];
static uint16_t s_ext_pub_len[MQTT_EXT_PUB_QUEUE_MAX];
static uint8_t  s_ext_pub_head  = 0U;
static uint8_t  s_ext_pub_tail  = 0U;
static uint8_t  s_ext_pub_count = 0U;

static uint32_t s_connect_fail_count = 0U;
static uint8_t  s_force_tcp_rebuild  = 0U;
#define MQTT_MAX_FAIL_BEFORE_RESET  5U
#define MQTT_RECONNECT_BASE_MS      1000U
#define MQTT_RECONNECT_MAX_MS       30000U

#define MQTT_REPLY_BUF_SIZE         128U
static uint8_t          s_reply_payload[MQTT_REPLY_BUF_SIZE];
static uint16_t         s_reply_len;
static volatile uint8_t s_reply_pending;
static char             s_reply_request_id[64];

#define MQTT_CMD_TOPIC_SIZE         128U
#define MQTT_CMD_PAYLOAD_SIZE       256U
static char             s_cmd_topic[MQTT_CMD_TOPIC_SIZE];
static char             s_cmd_payload[MQTT_CMD_PAYLOAD_SIZE];
static volatile uint8_t s_cmd_pending;

/* ==========================================================================
 *  离线缓存与辅助工具
 * ========================================================================== */
static void offline_cache_push(const char *payload, uint16_t len)
{
    if (len > sizeof(s_offline_buf[0])) {
        len = (uint16_t)sizeof(s_offline_buf[0]);
    }
    memcpy(s_offline_buf[s_offline_head], payload, len);
    s_offline_len[s_offline_head] = len;
    s_offline_head = (uint8_t)((s_offline_head + 1U) % MQTT_OFFLINE_CACHE_MAX);
    if (s_offline_count < MQTT_OFFLINE_CACHE_MAX) {
        s_offline_count++;
    } else {
        s_offline_tail = (uint8_t)((s_offline_tail + 1U) % MQTT_OFFLINE_CACHE_MAX);
    }
}

static uint8_t mqtt_flush_offline_cache(MQTTClient *client)
{
    uint8_t sent = 0U;
    while (s_offline_count > 0U) {
        MQTTMessage msg;
        int rc;
        msg.qos        = (enum QoS)MQTT_QOS;
        msg.retained   = 0;
        msg.dup        = 0;
        msg.id         = 0;
        msg.payload    = s_offline_buf[s_offline_tail];
        msg.payloadlen = s_offline_len[s_offline_tail];
        rc = MQTTPublish(client, s_topic_pub, &msg);
        if (rc != 0) {
            break;
        }
        s_offline_tail = (uint8_t)((s_offline_tail + 1U) % MQTT_OFFLINE_CACHE_MAX);
        s_offline_count--;
        sent++;
    }
    if (sent > 0U) {
#if BSP_UART2_DEBUG_ENABLE
        BSP_UART2_Printf("[MQTT] Offline cache flushed: %u messages\r\n", (unsigned)sent);
#endif
    }
    return sent;
}

static void huawei_build_topics(void)
{
    snprintf(s_topic_pub, sizeof(s_topic_pub),
             "$oc/devices/%s/sys/properties/report", HUAWEI_DEVICE_ID);
    snprintf(s_topic_sub, sizeof(s_topic_sub),
             "$oc/devices/%s/sys/commands/#", HUAWEI_DEVICE_ID);
}

static uint8_t huawei_extract_request_id(const char *topic, char *out, uint16_t out_size)
{
    const char *p = strstr(topic, "request_id=");
    if (p == NULL) {
        return 0U;
    }
    p += strlen("request_id=");
    strncpy(out, p, out_size - 1U);
    out[out_size - 1U] = '\0';
    return 1U;
}

static uint8_t json_get_string(const char *json, const char *key, char *out, uint16_t out_size)
{
    char search[64];
    const char *p;
    const char *start;
    const char *end;
    uint16_t len;

    snprintf(search, sizeof(search), "\"%s\":\"", key);
    p = strstr(json, search);
    if (p == NULL) {
        return 0U;
    }
    start = p + strlen(search);
    end = strchr(start, '"');
    if (end == NULL) {
        return 0U;
    }
    len = (uint16_t)(end - start);
    if (len >= out_size) {
        len = out_size - 1U;
    }
    memcpy(out, start, len);
    out[len] = '\0';
    return 1U;
}

static void mqtt_queue_reply(const uint8_t *data, uint16_t len, const char *request_id)
{
    if ((data == NULL) || (len == 0U)) {
        return;
    }
    if (len > MQTT_REPLY_BUF_SIZE) {
        len = MQTT_REPLY_BUF_SIZE;
    }
    memcpy(s_reply_payload, data, len);
    s_reply_len = len;
    if (request_id != NULL) {
        strncpy(s_reply_request_id, request_id, sizeof(s_reply_request_id) - 1U);
        s_reply_request_id[sizeof(s_reply_request_id) - 1U] = '\0';
    }
    s_reply_pending = 1U;
}

static void mqtt_flush_reply(MQTTClient *client)
{
    MQTTMessage msg;

    if (s_reply_pending == 0U) {
        return;
    }
    s_reply_pending = 0U;

    snprintf(s_topic_resp, sizeof(s_topic_resp),
             "$oc/devices/%s/sys/commands/response/request_id=%s",
             HUAWEI_DEVICE_ID, s_reply_request_id);

    msg.qos        = (enum QoS)MQTT_QOS;
    msg.retained   = 0;
    msg.dup        = 0;
    msg.id         = 0;
    msg.payload    = s_reply_payload;
    msg.payloadlen = s_reply_len;

    (void)MQTTPublish(client, s_topic_resp, &msg);
}

static void mqtt_flush_ext_publish(MQTTClient *client)
{
    MQTTMessage msg;

    while (s_ext_pub_count > 0U) {
        msg.qos        = (enum QoS)MQTT_QOS;
        msg.retained   = 0;
        msg.dup        = 0;
        msg.id         = 0;
        msg.payload    = s_ext_pub_buf[s_ext_pub_tail];
        msg.payloadlen = s_ext_pub_len[s_ext_pub_tail];

        if (MQTTPublish(client, s_topic_pub, &msg) != 0) {
            offline_cache_push((const char *)s_ext_pub_buf[s_ext_pub_tail],
                               s_ext_pub_len[s_ext_pub_tail]);
            s_ext_pub_tail = (uint8_t)((s_ext_pub_tail + 1U) % MQTT_EXT_PUB_QUEUE_MAX);
            s_ext_pub_count--;
            return;
        }
        s_ext_pub_tail = (uint8_t)((s_ext_pub_tail + 1U) % MQTT_EXT_PUB_QUEUE_MAX);
        s_ext_pub_count--;
    }
}

static void mqtt_message_handler(MessageData *md)
{
    int topic_len;
    int payload_len;

    if (s_cmd_pending != 0U) {
        return;
    }

    topic_len = (int)md->topicName->lenstring.len;
    if (topic_len >= (int)MQTT_CMD_TOPIC_SIZE) {
        topic_len = (int)MQTT_CMD_TOPIC_SIZE - 1;
    }
    memcpy(s_cmd_topic, md->topicName->lenstring.data, (size_t)topic_len);
    s_cmd_topic[topic_len] = '\0';

    payload_len = (int)md->message->payloadlen;
    if (payload_len >= (int)MQTT_CMD_PAYLOAD_SIZE) {
        payload_len = (int)MQTT_CMD_PAYLOAD_SIZE - 1;
    }
    memcpy(s_cmd_payload, md->message->payload, (size_t)payload_len);
    s_cmd_payload[payload_len] = '\0';

    s_cmd_pending = 1U;
}

static void mqtt_process_command(void)
{
    char     command_name[32];
    char     request_id[64];
    char     response[128];
    uint16_t resp_len;

    if (s_cmd_pending == 0U) {
        return;
    }
    s_cmd_pending = 0U;

    if (huawei_extract_request_id(s_cmd_topic, request_id, sizeof(request_id)) == 0U) {
        return;
    }
    if (json_get_string(s_cmd_payload, "command_name", command_name, sizeof(command_name)) == 0U) {
        return;
    }

#if BSP_UART2_DEBUG_ENABLE
    BSP_UART2_Printf("[MQTT-CMD] %s (request_id=%s)\r\n", command_name, request_id);
#endif

    if (strcmp(command_name, "MUTE") == 0) {
#if APP_FALL_ENABLE
        APP_FALL_Mute();
#endif
        resp_len = (uint16_t)snprintf(response, sizeof(response),
            "{\"result_code\":0,\"response_name\":\"COMMAND_RESPONSE\",\"paras\":{\"result\":\"muted\"}}");
        mqtt_queue_reply((uint8_t *)response, resp_len, request_id);
    } else if (strcmp(command_name, "RESET") == 0) {
#if APP_FALL_ENABLE
        APP_FALL_Reset();
#endif
        resp_len = (uint16_t)snprintf(response, sizeof(response),
            "{\"result_code\":0,\"response_name\":\"COMMAND_RESPONSE\",\"paras\":{\"result\":\"reset\"}}");
        mqtt_queue_reply((uint8_t *)response, resp_len, request_id);
    } else {
        resp_len = (uint16_t)snprintf(response, sizeof(response),
            "{\"result_code\":2,\"response_name\":\"COMMAND_RESPONSE\",\"paras\":{\"result\":\"unknown command\"}}");
        mqtt_queue_reply((uint8_t *)response, resp_len, request_id);
    }
}

uint8_t APP_MQTT_GetStatus(void)
{
    return s_mqtt_status;
}

uint8_t APP_MQTT_Publish(const uint8_t *payload, uint16_t len)
{
    int32_t lock;
    uint8_t idx;

    if ((payload == NULL) || (len == 0U)) {
        return 1U;
    }
    if (len > MQTT_EXT_PUB_LEN_MAX) {
        len = MQTT_EXT_PUB_LEN_MAX;
    }

    lock = osKernelLock();
    if (s_ext_pub_count >= MQTT_EXT_PUB_QUEUE_MAX) {
        if (lock >= 0) osKernelUnlock();
        return 3U;
    }
    idx = s_ext_pub_head;
    memcpy(s_ext_pub_buf[idx], payload, len);
    s_ext_pub_len[idx] = len;
    s_ext_pub_head = (uint8_t)((s_ext_pub_head + 1U) % MQTT_EXT_PUB_QUEUE_MAX);
    s_ext_pub_count++;
    if (lock >= 0) osKernelUnlock();
    return 0U;
}

static uint32_t mqtt_backoff_delay(void)
{
    uint32_t shift = (s_connect_fail_count < 5U) ? s_connect_fail_count : 5U;
    uint32_t delay = MQTT_RECONNECT_BASE_MS << shift;
    if (delay > MQTT_RECONNECT_MAX_MS) {
        delay = MQTT_RECONNECT_MAX_MS;
    }
    return delay;
}

static uint8_t mqtt_full_connect(void)
{
    int rc;
    MQTTPacket_connectData connect_data = MQTTPacket_connectData_initializer;
    char   ip_buf[16];
    int8_t tcp_ok;

    if (BSP_ESP8266_TestAT(2000U) != ESP8266_OK) {
        (void)BSP_ESP8266_Reset();
        osDelay(1500U);
        return 0U;
    }
    (void)BSP_ESP8266_SetEcho(0);

    if ((s_connect_fail_count == 0U) && (BSP_ESP8266_SetMode(ESP8266_MODE_STA) != ESP8266_OK)) {
        return 0U;
    }
    if (BSP_ESP8266_GetIP(ip_buf, sizeof(ip_buf)) == ESP8266_OK) {
        s_mqtt_status |= 0x01U;
    } else {
        if (BSP_ESP8266_SetMode(ESP8266_MODE_STA) != ESP8266_OK) {
            return 0U;
        }
        if (BSP_ESP8266_JoinAP(MQTT_WIFI_SSID, MQTT_WIFI_PASSWORD) != ESP8266_OK) {
            return 0U;
        }
        osDelay(300U);
        if (BSP_ESP8266_GetIP(ip_buf, sizeof(ip_buf)) != ESP8266_OK) {
            return 0U;
        }
        s_mqtt_status |= 0x01U;
    }

    tcp_ok = BSP_ESP8266_IsTCPConnected();
    if ((tcp_ok == 1) && (s_force_tcp_rebuild == 0U)) {
        s_mqtt_status |= 0x02U;
        BSP_ESP8266_ClearClosedFlag();
        NetworkInit(&s_mqtt_network);
        BSP_ESP8266_EnterMQTTMode();
    } else {
        if (s_force_tcp_rebuild != 0U) {
            (void)BSP_ESP8266_TCPClose();
            s_force_tcp_rebuild = 0U;
            osDelay(200U);
        }
        NetworkInit(&s_mqtt_network);
        if (NetworkConnect(&s_mqtt_network, (char *)MQTT_BROKER_HOST, (int)MQTT_BROKER_PORT) != 0) {
            return 0U;
        }
        s_mqtt_status |= 0x02U;
        BSP_ESP8266_ClearClosedFlag();
    }

    MQTTClientInit(&s_mqtt_client, &s_mqtt_network,
                   3000U,
                   s_mqtt_sendbuf, sizeof(s_mqtt_sendbuf),
                   s_mqtt_readbuf, sizeof(s_mqtt_readbuf));

    connect_data.MQTTVersion       = 4;
    connect_data.clientID.cstring  = (char *)MQTT_CLIENT_ID;
    connect_data.keepAliveInterval = MQTT_KEEPALIVE_SEC;
    connect_data.cleansession      = 1;
    connect_data.username.cstring  = (char *)MQTT_USERNAME;
    connect_data.password.cstring  = (char *)MQTT_PASSWORD;

    rc = MQTTConnect(&s_mqtt_client, &connect_data);
    if (rc != 0) {
        (void)BSP_ESP8266_TCPClose();
        s_force_tcp_rebuild = 1U;
        return 0U;
    }
    s_force_tcp_rebuild = 0U;
    s_mqtt_status |= 0x04U;

    huawei_build_topics();
    (void)MQTTSubscribe(&s_mqtt_client, s_topic_sub, (enum QoS)MQTT_QOS, mqtt_message_handler);

#if BSP_UART2_DEBUG_ENABLE
    BSP_UART2_Printf("[MQTT] Connected to Huawei IoTDA (device=%s)\r\n", HUAWEI_DEVICE_ID);
#endif
    return 1U;
}

static int mqtt_build_payload(char *payload, size_t buf_size)
{
    fall_status_t st;
    float         conf;
    unsigned      conf_x100;
    int           len;

    memset(&st, 0, sizeof(st));
#if APP_FALL_ENABLE
    APP_FALL_GetStatus(&st);
#endif

    conf      = (st.confidence < 0.0f) ? 0.0f : st.confidence;
    conf_x100 = (unsigned)(conf * 100.0f + 0.5f);

    len = snprintf(payload, buf_size,
                   "{\"services\":[{\"service_id\":\"%s\",\"properties\":{"
                   "\"fall_state\":%u,"
                   "\"confidence\":%u.%02u,"
                   "\"bbox\":\"[%u,%u,%u,%u]\","
                   "\"beep\":%u,"
                   "\"vision_offline\":%u"
                   "}}]}",
                   HUAWEI_SERVICE_ID,
                   (unsigned)st.fall_state,
                   conf_x100 / 100U, conf_x100 % 100U,
                   (unsigned)st.x, (unsigned)st.y, (unsigned)st.w, (unsigned)st.h,
                   (unsigned)st.beep_on,
                   (unsigned)st.vision_offline);

    if ((len <= 0) || ((size_t)len >= buf_size)) {
        return -1;
    }
    return len;
}

static uint8_t mqtt_publish_once(void)
{
    char payload[384];
    int  len;
    MQTTMessage msg;
    int rc;

    len = mqtt_build_payload(payload, sizeof(payload));
    if (len <= 0) {
        return 0U;
    }

    msg.qos        = (enum QoS)MQTT_QOS;
    msg.retained   = 0;
    msg.dup        = 0;
    msg.id         = 0;
    msg.payload    = payload;
    msg.payloadlen = (size_t)len;

    rc = MQTTPublish(&s_mqtt_client, s_topic_pub, &msg);
    if (rc != 0) {
        offline_cache_push(payload, (uint16_t)len);
        return 0U;
    }

#if BSP_UART2_DEBUG_ENABLE
    BSP_UART2_Printf("[MQTT] Published: %s\r\n", payload);
#endif
    return 1U;
}

void APP_MQTT_Task(void *argument)
{
    (void)argument;
    uint8_t connected = 0U;

    osDelay(500U);

    for (;;) {
#if APP_WATCHDOG_ENABLE
        Watchdog_Kick(WDT_TASK_MQTT);
#endif

        if (connected == 0U) {
            uint32_t backoff;

            s_mqtt_status = 0U;
            if (s_connect_fail_count >= MQTT_MAX_FAIL_BEFORE_RESET) {
                (void)BSP_ESP8266_Reset();
#if APP_WATCHDOG_ENABLE
                Watchdog_DelayWithKick(WDT_TASK_MQTT, 2000U);
#else
                osDelay(2000U);
#endif
                s_connect_fail_count = 0U;
            }

            if (mqtt_full_connect() != 0U) {
                connected = 1U;
                s_connect_fail_count = 0U;
#if APP_FALL_ENABLE
                (void)APP_FALL_TakeCloudDirty();
#endif
                (void)mqtt_publish_once();
                (void)mqtt_flush_offline_cache(&s_mqtt_client);
            } else {
                s_connect_fail_count++;
                backoff = mqtt_backoff_delay();
#if APP_WATCHDOG_ENABLE
                Watchdog_DelayWithKick(WDT_TASK_MQTT, backoff);
#else
                osDelay(backoff);
#endif
                continue;
            }
        }

        if (mqtt_publish_once() == 0U) {
            osDelay(200U);
            if (mqtt_publish_once() == 0U) {
                connected = 0U;
                (void)BSP_ESP8266_TCPClose();
                s_connect_fail_count++;
#if APP_WATCHDOG_ENABLE
                Watchdog_DelayWithKick(WDT_TASK_MQTT, mqtt_backoff_delay());
#else
                osDelay(mqtt_backoff_delay());
#endif
                continue;
            }
        }

        {
            uint32_t waited = 0U;
            uint32_t probe_timer = 0U;
            int rc;
            while (waited < MQTT_PUBLISH_PERIOD_MS) {
                probe_timer += (uint32_t)MQTT_YIELD_TIMEOUT_MS;
                if (probe_timer >= 10000U) {
                    probe_timer = 0U;
                    if (BSP_ESP8266_IsTCPConnected() == 0) {
                        connected = 0U;
                        (void)BSP_ESP8266_TCPClose();
                        s_connect_fail_count++;
                        break;
                    }
                }
                rc = MQTTYield(&s_mqtt_client, MQTT_YIELD_TIMEOUT_MS);
                if (rc != 0) {
                    connected = 0U;
                    (void)BSP_ESP8266_TCPClose();
                    s_connect_fail_count++;
                    break;
                }
                mqtt_process_command();
                mqtt_flush_reply(&s_mqtt_client);
                mqtt_flush_ext_publish(&s_mqtt_client);

#if APP_FALL_ENABLE
                /* 跌倒触发/解除/消音/掉线事件：立即上报，不等 5s 心跳 */
                if (APP_FALL_TakeCloudDirty() != 0U) {
                    (void)mqtt_publish_once();
                    waited = 0U;
                }
#endif
#if APP_WATCHDOG_ENABLE
                Watchdog_Kick(WDT_TASK_MQTT);
#endif
                waited += (uint32_t)MQTT_YIELD_TIMEOUT_MS;
            }
        }
    }
}

#endif /* APP_MQTT_ENABLE && APP_TASKS_ENABLE && BSP_ESP8266_ENABLE */
