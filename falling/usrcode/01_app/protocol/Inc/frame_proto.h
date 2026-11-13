/**
 ******************************************************************************
 * @file    frame_proto.h
 * @brief   K230 跌倒检测串口帧解析状态机头文件
 *
 *  支持协议格式：
 *    1. 默认标准帧（K230 官方/legacy 模式）：
 *       $<x>,<y>,<w>,<h>,<State>,<score>#
 *       示例：$483,60,156,386,Fall,0.72#
 *    2. 兼容校验帧（可选 robust 模式）：
 *       $F,<evt>,<state>,<cls>,<score>,<cx>,<cy>,<seq>*<XOR>\r\n
 ******************************************************************************
 */
#ifndef FRAME_PROTO_H
#define FRAME_PROTO_H

#include "module_cfg.h"

#if PROTOCOL_FALL_ENABLE

#include <stdint.h>

#define FRAME_PROTO_MAX_LEN   64U

typedef enum {
    FALL_STATE_NOFALL = 0,
    FALL_STATE_FALL   = 1
} fall_state_t;

typedef struct {
    uint16_t     x;
    uint16_t     y;
    uint16_t     w;
    uint16_t     h;
    fall_state_t state;
    char         state_str[8];  /*!< "Fall" / "NoFall" */
    float        score;         /*!< 置信度 0.00 ~ 1.00 */
    uint32_t     timestamp;     /*!< 收到帧时的系统 tick (ms) */
} fall_frame_t;

typedef struct {
    char     buf[FRAME_PROTO_MAX_LEN];
    uint16_t len;
    uint8_t  in_frame;
    uint8_t  is_robust;
    uint32_t ok_count;
    uint32_t err_count;
} frame_proto_parser_t;

/**
 * @brief  初始化帧解析器上下文
 */
void FrameProto_Init(frame_proto_parser_t *parser);

/**
 * @brief  逐字节喂入解析状态机
 * @param  parser  解析器实例
 * @param  byte    接收到的字节
 * @param  out     输出解析成功的帧（可为 NULL）
 * @retval 1=解析出一帧有效数据；0=尚需更多字节或坏帧已丢弃
 */
uint8_t FrameProto_FeedByte(frame_proto_parser_t *parser, uint8_t byte, fall_frame_t *out);

#endif /* PROTOCOL_FALL_ENABLE */
#endif /* FRAME_PROTO_H */
