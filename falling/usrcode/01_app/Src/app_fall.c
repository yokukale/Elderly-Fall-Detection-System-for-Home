/**
 ******************************************************************************
 * @file    app_fall.c
 * @brief   跌倒判决与报警业务模块实现
 *
 *  核心逻辑：
 *    1. 滑动窗口防抖：最近 10 帧中满足 (State==Fall && score>=0.50 && w>h) >= 6 帧才触发报警
 *    2. 报警保持期：触发后维持 30s（期间忽略 NoFall），同步触发蜂鸣器与云端立即上报
 *    3. 消音与复位：支持云端下发 MUTE 消音（停止蜂鸣器并回传 $MUTE# 给 K230）与 RESET 复位
 *    4. 冷却期：报警解除后进入 10s 冷却期，清空窗口防抖动反复触发
 *    5. K230 掉线检测：超过 5s 未收到任何有效帧置 vision_offline=1
 ******************************************************************************
 */
#include "module_cfg.h"

#if APP_FALL_ENABLE

#include "app_fall.h"
#include "bsp_uart.h"
#include "cmsis_os.h"
#include "stm32f4xx_hal.h"
#include <string.h>

static uint8_t          s_win[FALL_WIN_LEN];
static uint8_t          s_win_idx;
static uint8_t          s_win_count;

static fall_status_t    s_status;
static uint32_t         s_state_enter_tick;
static uint32_t         s_last_frame_tick;
static uint8_t          s_has_seen_frame;
static volatile uint8_t s_cloud_dirty;

static uint8_t calc_win_hits(void)
{
    uint8_t i;
    uint8_t hits = 0U;
    for (i = 0U; i < s_win_count; i++) {
        if (s_win[i] != 0U) {
            hits++;
        }
    }
    return hits;
}

static void clear_window(void)
{
    memset(s_win, 0, sizeof(s_win));
    s_win_idx   = 0U;
    s_win_count = 0U;
    s_status.win_hits = 0U;
}

void APP_FALL_Init(void)
{
    int32_t lock = osKernelLock();
    memset(&s_status, 0, sizeof(s_status));
    clear_window();
    s_state_enter_tick = HAL_GetTick();
    s_last_frame_tick  = HAL_GetTick();
    s_has_seen_frame   = 0U;
    s_cloud_dirty      = 0U;
    if (lock >= 0) osKernelUnlock();
}

void APP_FALL_ProcessFrame(const fall_frame_t *frame)
{
    uint32_t now;
    uint8_t  is_hit = 0U;
    int32_t  lock;

    if (frame == NULL) {
        return;
    }

    now = HAL_GetTick();

    if ((frame->state == FALL_STATE_FALL) && (frame->score >= FALL_SCORE_TH)) {
#if FALL_GEOM_CHECK_ENABLE
        if (frame->w > frame->h) {
            is_hit = 1U;
        }
#else
        is_hit = 1U;
#endif
    }

    lock = osKernelLock();

    s_last_frame_tick = now;
    s_has_seen_frame  = 1U;
    if (s_status.vision_offline != 0U) {
        s_status.vision_offline = 0U;
        s_cloud_dirty = 1U;
    }

    s_status.x          = frame->x;
    s_status.y          = frame->y;
    s_status.w          = frame->w;
    s_status.h          = frame->h;
    s_status.confidence = frame->score;

    if (s_status.sys_state == FALL_SYS_IDLE) {
        s_win[s_win_idx] = is_hit;
        s_win_idx = (uint8_t)((s_win_idx + 1U) % FALL_WIN_LEN);
        if (s_win_count < FALL_WIN_LEN) {
            s_win_count++;
        }
        s_status.win_hits = calc_win_hits();

        if (s_status.win_hits >= FALL_HIT_TH) {
            s_status.sys_state  = FALL_SYS_ALARM;
            s_status.fall_state = 1U;
            s_status.beep_on    = 1U;
            s_status.alarm_count++;
            s_state_enter_tick  = now;
            s_cloud_dirty       = 1U;
#if BSP_UART2_DEBUG_ENABLE
            {
                float    c = (s_status.confidence < 0.0f) ? 0.0f : s_status.confidence;
                unsigned cx100 = (unsigned)(c * 100.0f + 0.5f);
                BSP_UART2_Printf("[FALL] ALARM TRIGGERED! hits=%u/%u score=%u.%02u bbox=(%u,%u,%u,%u)\r\n",
                                 (unsigned)s_status.win_hits, (unsigned)FALL_WIN_LEN,
                                 cx100 / 100U, cx100 % 100U,
                                 (unsigned)s_status.x, (unsigned)s_status.y,
                                 (unsigned)s_status.w, (unsigned)s_status.h);
            }
#endif
        }
    }

    if (lock >= 0) osKernelUnlock();
}

void APP_FALL_OnTick(void)
{
    uint32_t now = HAL_GetTick();
    int32_t  lock = osKernelLock();

    if ((s_has_seen_frame != 0U) &&
        ((now - s_last_frame_tick) >= FALL_K230_OFFLINE_MS)) {
        if (s_status.vision_offline == 0U) {
            s_status.vision_offline = 1U;
            s_cloud_dirty = 1U;
#if BSP_UART2_DEBUG_ENABLE
            BSP_UART2_Printf("[FALL] K230 vision offline timeout (%ums)\r\n",
                             (unsigned)FALL_K230_OFFLINE_MS);
#endif
        }
    }

    if (s_status.sys_state == FALL_SYS_ALARM) {
        if ((now - s_state_enter_tick) >= FALL_ALARM_HOLD_MS) {
            s_status.sys_state  = FALL_SYS_COOLDOWN;
            s_status.fall_state = 0U;
            s_status.beep_on    = 0U;
            s_state_enter_tick  = now;
            clear_window();
            s_cloud_dirty       = 1U;
#if BSP_UART2_DEBUG_ENABLE
            BSP_UART2_Printf("[FALL] Alarm hold expired, entering cooldown (%ums)\r\n",
                             (unsigned)FALL_COOLDOWN_MS);
#endif
        }
    } else if (s_status.sys_state == FALL_SYS_COOLDOWN) {
        if ((now - s_state_enter_tick) >= FALL_COOLDOWN_MS) {
            s_status.sys_state = FALL_SYS_IDLE;
            clear_window();
#if BSP_UART2_DEBUG_ENABLE
            BSP_UART2_Printf("[FALL] Cooldown finished, back to IDLE\r\n");
#endif
        }
    }

    if (lock >= 0) osKernelUnlock();
}

void APP_FALL_Mute(void)
{
    int32_t lock = osKernelLock();
    if ((s_status.beep_on != 0U) || (s_status.fall_state != 0U)) {
        s_status.beep_on = 0U;
        s_cloud_dirty    = 1U;
    }
    if (lock >= 0) osKernelUnlock();

#if BSP_UART1_ENABLE
    BSP_UART1_SendString("$MUTE#");
#endif
#if BSP_UART2_DEBUG_ENABLE
    BSP_UART2_Printf("[FALL] Muted by command\r\n");
#endif
}

void APP_FALL_Reset(void)
{
    int32_t lock = osKernelLock();
    s_status.sys_state  = FALL_SYS_IDLE;
    s_status.fall_state = 0U;
    s_status.beep_on    = 0U;
    clear_window();
    s_state_enter_tick  = HAL_GetTick();
    s_cloud_dirty       = 1U;
    if (lock >= 0) osKernelUnlock();

#if BSP_UART1_ENABLE
    BSP_UART1_SendString("$RESET#");
#endif
#if BSP_UART2_DEBUG_ENABLE
    BSP_UART2_Printf("[FALL] Reset to IDLE by command\r\n");
#endif
}

void APP_FALL_GetStatus(fall_status_t *out)
{
    int32_t lock;
    if (out == NULL) {
        return;
    }
    lock = osKernelLock();
    *out = s_status;
    if (lock >= 0) osKernelUnlock();
}

uint8_t APP_FALL_TakeCloudDirty(void)
{
    uint8_t dirty;
    int32_t lock = osKernelLock();
    dirty = s_cloud_dirty;
    s_cloud_dirty = 0U;
    if (lock >= 0) osKernelUnlock();
    return dirty;
}

#endif /* APP_FALL_ENABLE */
