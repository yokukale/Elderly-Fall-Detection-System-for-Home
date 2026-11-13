/**
 ******************************************************************************
 * @file    svc_conn.h
 * @brief   服务层（SVC）— 连接管理服务（状态机 + 引擎注册）
 ******************************************************************************
 */
#ifndef SVC_CONN_H
#define SVC_CONN_H

#include "module_cfg.h"
#if SVC_CONN_ENABLE

#include <stdint.h>
#include "plat_obj.h"

typedef enum {
    SVC_CONN_ST_OFFLINE = 0,
    SVC_CONN_ST_NET_UP,
    SVC_CONN_ST_MQTT_UP
} svc_conn_state_t;

typedef struct {
    uint8_t (*is_connected)(void);
    uint8_t (*publish)(const uint8_t *payload, uint16_t len);
} svc_conn_engine_t;

uint8_t          svc_conn_register(void);
void             svc_conn_set_engine(const svc_conn_engine_t *engine);
svc_conn_state_t svc_conn_get_state(void);
uint8_t          svc_conn_publish(const uint8_t *payload, uint16_t len);
plat_svc_t      *svc_conn_get_obj(void);

#endif /* SVC_CONN_ENABLE */
#endif /* SVC_CONN_H */
