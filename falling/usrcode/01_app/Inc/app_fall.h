/**
 ******************************************************************************
 * @file    app_fall.h
 * @brief   跌倒判决与报警业务模块头文件（滑动窗口防抖 + 保持/冷却 + 消音/复位）
 ******************************************************************************
 */
#ifndef APP_FALL_H
#define APP_FALL_H

#include "module_cfg.h"

#if APP_FALL_ENABLE

#include <stdint.h>
#include "frame_proto.h"

/* ========== 判决参数（按开发文档 §4.3） ========== */
#define FALL_SCORE_TH           0.50f   /*!< score >= 阈值才计入有效 Fall 帧 */
#define FALL_WIN_LEN            10U     /*!< 滑动窗口长度（帧） */
#define FALL_HIT_TH             6U      /*!< 窗口内有效 Fall 帧 >= 6 判定跌倒 */
#define FALL_ALARM_HOLD_MS      30000U  /*!< 报警持续时长（ms），期间忽略 NoFall */
#define FALL_COOLDOWN_MS        10000U  /*!< 报警解除后冷却期（ms），防反复触发 */
#define FALL_K230_OFFLINE_MS    5000U   /*!< K230 无数据心跳超时（ms） */
#define FALL_GEOM_CHECK_ENABLE  1U      /*!< 1=启用几何合理性判据 (w > h) */

typedef enum {
    FALL_SYS_IDLE = 0,      /*!< 正常监测中 */
    FALL_SYS_ALARM,         /*!< 跌倒报警中 */
    FALL_SYS_COOLDOWN       /*!< 报警结束冷却期 */
} fall_sys_state_t;

typedef struct {
    fall_sys_state_t sys_state;
    uint8_t          fall_state;      /*!< 1=跌倒报警中, 0=正常 */
    uint8_t          beep_on;         /*!< 1=蜂鸣器应响, 0=静音 */
    uint8_t          vision_offline;  /*!< 1=K230通信超时掉线, 0=在线 */
    uint8_t          win_hits;        /*!< 当前滑动窗口命中数 */
    float            confidence;      /*!< 最近一次有效置信度 */
    uint16_t         x;
    uint16_t         y;
    uint16_t         w;
    uint16_t         h;
    uint32_t         alarm_count;     /*!< 累计报警次数 */
} fall_status_t;

void APP_FALL_Init(void);
void APP_FALL_ProcessFrame(const fall_frame_t *frame);
void APP_FALL_OnTick(void);
void APP_FALL_Mute(void);
void APP_FALL_Reset(void);
void APP_FALL_GetStatus(fall_status_t *out);
uint8_t APP_FALL_TakeCloudDirty(void);

#endif /* APP_FALL_ENABLE */
#endif /* APP_FALL_H */
