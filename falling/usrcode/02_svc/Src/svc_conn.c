/**
 ******************************************************************************
 * @file    svc_conn.c
 * @brief   服务层（SVC）— 连接管理服务实现
 ******************************************************************************
 */
#include "module_cfg.h"
#if SVC_CONN_ENABLE && PLAT_SVCMGR_ENABLE && PLAT_CONN_ENABLE

#include "svc_conn.h"
#include "plat_mgr.h"
#include <stddef.h>

static const svc_conn_engine_t *s_engine = NULL;
static svc_conn_state_t         s_state  = SVC_CONN_ST_OFFLINE;

static uint8_t svc_conn_init(void)
{
    s_state = SVC_CONN_ST_OFFLINE;
    return 0U;
}

static uint8_t svc_conn_process(void)
{
    if ((s_engine != NULL) && (s_engine->is_connected != NULL)) {
        uint8_t st = s_engine->is_connected();
        if ((st & 0x04U) != 0U) {
            s_state = SVC_CONN_ST_MQTT_UP;
        } else if ((st & 0x01U) != 0U) {
            s_state = SVC_CONN_ST_NET_UP;
        } else {
            s_state = SVC_CONN_ST_OFFLINE;
        }
    }
    return 0U;
}

static const plat_ops_t s_conn_ops = {
    .init    = svc_conn_init,
    .start   = NULL,
    .process = svc_conn_process,
    .stop    = NULL,
    .deinit  = NULL
};

static plat_svc_t s_svc_conn = {
    .name      = "svc_conn",
    .id        = 1U,
    .ctx       = NULL,
    .ops       = &s_conn_ops,
    .state     = PLAT_STATE_NONE,
    .deps      = NULL,
    .dep_count = 0U,
    .next      = NULL
};

uint8_t svc_conn_register(void)
{
    return plat_svcmgr_register(&s_svc_conn);
}

void svc_conn_set_engine(const svc_conn_engine_t *engine)
{
    s_engine = engine;
}

svc_conn_state_t svc_conn_get_state(void)
{
    return s_state;
}

uint8_t svc_conn_publish(const uint8_t *payload, uint16_t len)
{
    if ((s_engine == NULL) || (s_engine->publish == NULL)) {
        return 1U;
    }
    return s_engine->publish(payload, len);
}

plat_svc_t *svc_conn_get_obj(void)
{
    return &s_svc_conn;
}

#endif /* SVC_CONN_ENABLE && PLAT_SVCMGR_ENABLE && PLAT_CONN_ENABLE */
