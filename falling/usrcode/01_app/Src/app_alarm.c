/**
 ******************************************************************************
 * @file    app_alarm.c
 * @brief   本地报警执行模块实现 — PA5 蜂鸣器随跌倒报警状态循环播放欢快旋律
 ******************************************************************************
 */
#include "module_cfg.h"

#if APP_ALARM_ENABLE && APP_TASKS_ENABLE

#include "app_alarm.h"
#if APP_FALL_ENABLE
#include "app_fall.h"
#endif
#if BSP_BEEP_ENABLE
#include "bsp_beep.h"
#endif
#if APP_WATCHDOG_ENABLE
#include "app_watchdog.h"
#endif
#if BSP_UART2_DEBUG_ENABLE
#include "bsp_uart.h"
#endif
#include "main.h"
#include "cmsis_os.h"

/* ========== C 大调常用音高频率（Hz） ========== */
#define NOTE_REST   0U
#define NOTE_C5     523U
#define NOTE_D5     587U
#define NOTE_E5     659U
#define NOTE_F5     698U
#define NOTE_G5     784U

/* ========== 节拍时长定义（欢快速度 ~187 BPM，四分音符 320ms） ========== */
#define BEAT_1_2    160U    /* 八分音符 */
#define BEAT_1      320U    /* 四分音符 */
#define BEAT_1_5    480U    /* 附点四分音符 */
#define BEAT_2      640U    /* 二分音符 */
#define BEAT_4      1280U   /* 全音符 */

#define NOTE_GAP_MIN_MS  35U
#define NOTE_GAP_MAX_MS  70U

typedef struct {
    uint16_t freq_hz;
    uint16_t dur_ms;
} alarm_note_t;

/* 《铃儿响叮当》(Jingle Bells) 欢快副歌循环（16 小节） */
static const alarm_note_t s_cheerful_melody[] = {
    /* 1-2 小节：3 3 3- | 3 3 3- */
    { NOTE_E5,   BEAT_1 }, { NOTE_E5, BEAT_1 }, { NOTE_E5, BEAT_2 },
    { NOTE_E5,   BEAT_1 }, { NOTE_E5, BEAT_1 }, { NOTE_E5, BEAT_2 },
    /* 3-4 小节：3 5 1. 2 | 3--- */
    { NOTE_E5,   BEAT_1 }, { NOTE_G5, BEAT_1 }, { NOTE_C5, BEAT_1_5 }, { NOTE_D5, BEAT_1_2 },
    { NOTE_E5,   BEAT_4 },
    /* 5-6 小节：4 4 4. 4 | 4 3 3 33 */
    { NOTE_F5,   BEAT_1 }, { NOTE_F5, BEAT_1 }, { NOTE_F5, BEAT_1_5 }, { NOTE_F5, BEAT_1_2 },
    { NOTE_F5,   BEAT_1 }, { NOTE_E5, BEAT_1 }, { NOTE_E5, BEAT_1 }, { NOTE_E5, BEAT_1_2 }, { NOTE_E5, BEAT_1_2 },
    /* 7-8 小节：3 2 2 3 | 2- 5- */
    { NOTE_E5,   BEAT_1 }, { NOTE_D5, BEAT_1 }, { NOTE_D5, BEAT_1 }, { NOTE_E5, BEAT_1 },
    { NOTE_D5,   BEAT_2 }, { NOTE_G5, BEAT_2 },
    /* 9-10 小节：3 3 3- | 3 3 3- */
    { NOTE_E5,   BEAT_1 }, { NOTE_E5, BEAT_1 }, { NOTE_E5, BEAT_2 },
    { NOTE_E5,   BEAT_1 }, { NOTE_E5, BEAT_1 }, { NOTE_E5, BEAT_2 },
    /* 11-12 小节：3 5 1. 2 | 3--- */
    { NOTE_E5,   BEAT_1 }, { NOTE_G5, BEAT_1 }, { NOTE_C5, BEAT_1_5 }, { NOTE_D5, BEAT_1_2 },
    { NOTE_E5,   BEAT_4 },
    /* 13-14 小节：4 4 4. 4 | 4 3 3 33 */
    { NOTE_F5,   BEAT_1 }, { NOTE_F5, BEAT_1 }, { NOTE_F5, BEAT_1_5 }, { NOTE_F5, BEAT_1_2 },
    { NOTE_F5,   BEAT_1 }, { NOTE_E5, BEAT_1 }, { NOTE_E5, BEAT_1 }, { NOTE_E5, BEAT_1_2 }, { NOTE_E5, BEAT_1_2 },
    /* 15-16 小节：5 5 4 2 | 1--- (末尾短休止换气) */
    { NOTE_G5,   BEAT_1 }, { NOTE_G5, BEAT_1 }, { NOTE_F5, BEAT_1 }, { NOTE_D5, BEAT_1 },
    { NOTE_C5,   BEAT_1_5 + BEAT_1 }, { NOTE_REST, BEAT_1_2 + BEAT_1 }
};

#define MELODY_NOTE_COUNT  (sizeof(s_cheerful_melody) / sizeof(s_cheerful_melody[0]))

static uint16_t calc_note_gap_ms(uint16_t dur_ms)
{
    uint16_t gap = (uint16_t)(dur_ms / 8U);
    if (gap < NOTE_GAP_MIN_MS) {
        gap = NOTE_GAP_MIN_MS;
    } else if (gap > NOTE_GAP_MAX_MS) {
        gap = NOTE_GAP_MAX_MS;
    }
    return (dur_ms > gap) ? gap : 0U;
}

static void alarm_start_note(uint32_t idx, uint8_t *in_gap, uint32_t *phase_end_tick)
{
    const alarm_note_t *n = &s_cheerful_melody[idx];
    uint16_t gap_ms = calc_note_gap_ms(n->dur_ms);
    uint16_t play_ms = (uint16_t)(n->dur_ms - gap_ms);

    if (n->freq_hz == NOTE_REST) {
#if BSP_BEEP_ENABLE
        BSP_BEEP_Off();
#endif
        *in_gap = 1U;
        *phase_end_tick = HAL_GetTick() + (uint32_t)n->dur_ms;
        return;
    }

#if BSP_BEEP_ENABLE
    BSP_BEEP_SetTone(n->freq_hz);
#endif
    *in_gap = 0U;
    *phase_end_tick = HAL_GetTick() + (uint32_t)play_ms;
}

void APP_ALARM_Task(void *argument)
{
    (void)argument;
    uint8_t  prev_beep = 0U;
    uint32_t note_idx = 0U;
    uint8_t  in_gap = 0U;
    uint32_t phase_end_tick = 0U;

#if BSP_BEEP_ENABLE
    BSP_BEEP_Off();
#endif

    for (;;) {
        fall_status_t st;
        APP_FALL_GetStatus(&st);

        if (st.beep_on != prev_beep) {
            prev_beep = st.beep_on;
            if (prev_beep != 0U) {
                note_idx = 0U;
                alarm_start_note(note_idx, &in_gap, &phase_end_tick);
            } else {
#if BSP_BEEP_ENABLE
                BSP_BEEP_Off();
#endif
            }
#if BSP_UART2_DEBUG_ENABLE
            BSP_UART2_Printf("[ALARM] BEEP %s\r\n", (prev_beep != 0U) ? "ON" : "OFF");
#endif
        } else if (prev_beep != 0U) {
            uint32_t now = HAL_GetTick();
            if ((int32_t)(now - phase_end_tick) >= 0) {
                if (in_gap == 0U) {
                    uint16_t gap_ms = calc_note_gap_ms(s_cheerful_melody[note_idx].dur_ms);
#if BSP_BEEP_ENABLE
                    BSP_BEEP_Off();
#endif
                    if (gap_ms > 0U) {
                        in_gap = 1U;
                        phase_end_tick = now + (uint32_t)gap_ms;
                    } else {
                        note_idx = (note_idx + 1U) % MELODY_NOTE_COUNT;
                        alarm_start_note(note_idx, &in_gap, &phase_end_tick);
                    }
                } else {
                    note_idx = (note_idx + 1U) % MELODY_NOTE_COUNT;
                    alarm_start_note(note_idx, &in_gap, &phase_end_tick);
                }
            }
        }

#if APP_WATCHDOG_ENABLE
        Watchdog_Kick(WDT_TASK_ALARM);
#endif
        osDelay((prev_beep != 0U) ? 10U : 50U);
    }
}

#endif /* APP_ALARM_ENABLE && APP_TASKS_ENABLE */
