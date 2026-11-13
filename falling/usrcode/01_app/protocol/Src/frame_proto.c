/**
 ******************************************************************************
 * @file    frame_proto.c
 * @brief   K230 跌倒检测串口帧解析状态机实现
 *
 *  解析策略：
 *    - '$' 起始 → 缓存帧体 → '#' 结束（legacy）或 '*XX\n' 结束（robust）
 *    - 长度超限直接丢弃并复位状态机，禁止裸 strcpy
 *    - legacy 帧按逗号切分 6 段：x, y, w, h, State, score
 *      State 使用 memcmp 比较 "Fall" / "NoFall"，score 使用 strtof 解析
 ******************************************************************************
 */
#include "module_cfg.h"

#if PROTOCOL_FALL_ENABLE

#include "frame_proto.h"
#include "stm32f4xx_hal.h"
#include <stdlib.h>
#include <string.h>

static uint8_t parse_u16(const char *s, uint16_t *out)
{
    char *endp = NULL;
    unsigned long v;

    if ((s == NULL) || (*s == '\0')) {
        return 0U;
    }
    if (*s == '-') {
        return 0U;
    }
    v = strtoul(s, &endp, 10);
    if ((endp == s) || (*endp != '\0') || (v > 65535UL)) {
        return 0U;
    }
    *out = (uint16_t)v;
    return 1U;
}

static uint8_t parse_score(const char *s, float *out)
{
    char *endp = NULL;
    float v;

    if ((s == NULL) || (*s == '\0')) {
        return 0U;
    }
    v = strtof(s, &endp);
    if ((endp == s) || (*endp != '\0')) {
        return 0U;
    }
    if ((v < 0.0f) || (v > 1.0f)) {
        return 0U;
    }
    *out = v;
    return 1U;
}

static uint8_t hex_nibble(char c)
{
    if ((c >= '0') && (c <= '9')) return (uint8_t)(c - '0');
    if ((c >= 'A') && (c <= 'F')) return (uint8_t)(c - 'A' + 10);
    if ((c >= 'a') && (c <= 'f')) return (uint8_t)(c - 'a' + 10);
    return 0xFFU;
}

static uint8_t parse_legacy_body(char *body, fall_frame_t *out)
{
    char *fields[6];
    uint8_t count = 0U;
    char *p = body;
    uint16_t x, y, w, h;
    float score;
    fall_state_t st;
    size_t slen;

    fields[count++] = p;
    while (*p != '\0') {
        if (*p == ',') {
            *p = '\0';
            if (count >= 6U) {
                return 0U;   /* 段数过多 */
            }
            fields[count++] = p + 1;
        }
        p++;
    }
    if (count != 6U) {
        return 0U;
    }

    if (!parse_u16(fields[0], &x) ||
        !parse_u16(fields[1], &y) ||
        !parse_u16(fields[2], &w) ||
        !parse_u16(fields[3], &h) ||
        !parse_score(fields[5], &score)) {
        return 0U;
    }

    slen = strlen(fields[4]);
    if ((slen == 4U) && (memcmp(fields[4], "Fall", 4U) == 0)) {
        st = FALL_STATE_FALL;
    } else if ((slen == 6U) && (memcmp(fields[4], "NoFall", 6U) == 0)) {
        st = FALL_STATE_NOFALL;
    } else {
        return 0U;
    }

    if (out != NULL) {
        out->x     = x;
        out->y     = y;
        out->w     = w;
        out->h     = h;
        out->state = st;
        memset(out->state_str, 0, sizeof(out->state_str));
        memcpy(out->state_str, fields[4], slen);
        out->score     = score;
        out->timestamp = HAL_GetTick();
    }
    return 1U;
}

static uint8_t parse_robust_body(char *body, uint16_t len, fall_frame_t *out)
{
    /* 格式：F,<evt>,<state>,<cls>,<score_x100>,<cx>,<cy>,<seq>*<XOR> */
    char *star;
    uint16_t body_len;
    uint8_t chk = 0U;
    uint8_t hi, lo;
    uint16_t i;
    char *fields[8];
    uint8_t count = 0U;
    char *p;
    long state_v, score_v, cx_v, cy_v;

    star = strchr(body, '*');
    if (star == NULL) {
        return 0U;
    }
    body_len = (uint16_t)(star - body);
    if ((body_len < 3U) || ((uint16_t)(body_len + 3U) > len)) {
        return 0U;
    }
    for (i = 0U; i < body_len; i++) {
        chk ^= (uint8_t)body[i];
    }
    hi = hex_nibble(star[1]);
    lo = hex_nibble(star[2]);
    if ((hi > 15U) || (lo > 15U) || (chk != (uint8_t)((hi << 4U) | lo))) {
        return 0U;
    }

    *star = '\0';
    p = body;
    fields[count++] = p;
    while (*p != '\0') {
        if (*p == ',') {
            *p = '\0';
            if (count >= 8U) {
                return 0U;
            }
            fields[count++] = p + 1;
        }
        p++;
    }
    if ((count != 8U) || (fields[0][0] != 'F') || (fields[0][1] != '\0')) {
        return 0U;
    }

    state_v = strtol(fields[2], NULL, 10);
    score_v = strtol(fields[4], NULL, 10);
    cx_v    = strtol(fields[5], NULL, 10);
    cy_v    = strtol(fields[6], NULL, 10);

    if (score_v < 0L) score_v = 0L;
    if (score_v > 100L) score_v = 100L;

    if (out != NULL) {
        out->x     = (cx_v >= 0L) ? (uint16_t)((cx_v * 640L) / 100L) : 0U;
        out->y     = (cy_v >= 0L) ? (uint16_t)((cy_v * 480L) / 100L) : 0U;
        out->w     = (state_v != 0L) ? 200U : 100U;
        out->h     = (state_v != 0L) ? 120U : 200U;
        out->state = (state_v != 0L) ? FALL_STATE_FALL : FALL_STATE_NOFALL;
        memset(out->state_str, 0, sizeof(out->state_str));
        memcpy(out->state_str, (state_v != 0L) ? "Fall" : "NoFall",
               (state_v != 0L) ? 4U : 6U);
        out->score     = (float)score_v / 100.0f;
        out->timestamp = HAL_GetTick();
    }
    return 1U;
}

void FrameProto_Init(frame_proto_parser_t *parser)
{
    if (parser == NULL) {
        return;
    }
    memset(parser, 0, sizeof(*parser));
}

uint8_t FrameProto_FeedByte(frame_proto_parser_t *parser, uint8_t byte, fall_frame_t *out)
{
    char c;

    if (parser == NULL) {
        return 0U;
    }
    c = (char)byte;

    if (c == '$') {
        parser->in_frame  = 1U;
        parser->is_robust = 0U;
        parser->len       = 0U;
        return 0U;
    }

    if (parser->in_frame == 0U) {
        return 0U;
    }

    if (parser->len == 0U && c == 'F') {
        parser->is_robust = 1U;
    }

    if ((parser->is_robust == 0U) && (c == '#')) {
        uint8_t ok;
        parser->buf[parser->len] = '\0';
        ok = parse_legacy_body(parser->buf, out);
        parser->in_frame = 0U;
        parser->len      = 0U;
        if (ok != 0U) {
            parser->ok_count++;
        } else {
            parser->err_count++;
        }
        return ok;
    }

    if ((parser->is_robust != 0U) && (c == '\n')) {
        uint8_t ok;
        if ((parser->len > 0U) && (parser->buf[parser->len - 1U] == '\r')) {
            parser->len--;
        }
        parser->buf[parser->len] = '\0';
        ok = parse_robust_body(parser->buf, parser->len, out);
        parser->in_frame  = 0U;
        parser->is_robust = 0U;
        parser->len       = 0U;
        if (ok != 0U) {
            parser->ok_count++;
        } else {
            parser->err_count++;
        }
        return ok;
    }

    if (parser->len >= (FRAME_PROTO_MAX_LEN - 1U)) {
        /* 长度超限，直接丢帧 */
        parser->in_frame  = 0U;
        parser->is_robust = 0U;
        parser->len       = 0U;
        parser->err_count++;
        return 0U;
    }

    parser->buf[parser->len++] = c;
    return 0U;
}

#endif /* PROTOCOL_FALL_ENABLE */
