# -*- coding: utf-8 -*-
r"""
K230 跌倒检测 + UART 上报（自训练 4 类 AnchorBaseDet 模型）
------------------------------------------------------------------
基于嘉楠在线训练平台生成的部署包 mp_deployment_source：
  - /sdcard/mp_deployment_source/libs/PlatTasks.py  -> DetectionApp
  - /sdcard/mp_deployment_source/deploy_config.json -> 模型/类别/anchor 配置
  - /sdcard/mp_deployment_source/*.kmodel            -> 自训练模型

本文件做三件事：
  1. 复用官方 DetectionApp 完成 摄像头取帧 -> 推理 -> 后处理
  2. 把 4 类姿态(fall/side-lying/sitting/standing)映射成 跌倒/正常，并做本地去抖
  3. 通过 UART 发给 STM32F407（两种协议可切换，见 PROTOCOL_MODE）

【协议选择 / 与既有工程的衔接】
  legacy（默认）：沿用《K230-STM32老人跌倒检测_开发文档.md》第 4 章
      $x,y,w,h,State,score#   每帧发送，State = Fall / NoFall
      STM32 侧已有的 frame_proto.c 滑动窗口(6/10 帧)+w>h 判据完全不用改。
      K230 侧不额外抬高阈值，判决权仍留给 STM32。
  robust（可选）：K230 侧稳判 + 带 XOR 校验的状态帧
      $F,<evt>,<state>,<cls>,<score>,<cx>,<cy>,<seq>*<XOR>\r\n
      配套 stm32_fall_uart.c，状态变化立即上报 + 500ms 心跳，抗干扰更强。

放置位置：把本文件与 deploy_config.json、*.kmodel 一起放到
          此电脑\CanMV\sdcard\mp_deployment_source\
运行方式：CanMV IDE 打开本文件点运行；或【保存为 main.py】上电脱机自启
"""

import os
import gc
import time
from machine import UART, FPIOA
from libs.PlatTasks import DetectionApp
from libs.PipeLine import PipeLine

try:
    import ujson as _json
except ImportError:
    import json as _json


# ============================ 1. 可调配置 ============================

ROOT_PATH = "/sdcard/mp_deployment_source/"

# ---- 协议模式：'legacy' 兼容既有 STM32 工程 / 'robust' 用带校验的新帧 ----
PROTOCOL_MODE = "legacy"

# 显示模式：'lcd' / 'hdmi' / 'lt9611' / 'st7701' / 'hx8399'
DISPLAY_MODE = "lcd"

# 送给 AI 的图像尺寸（宽需 16 对齐）。
# 用 640x480 与既有开发文档的框坐标域一致，STM32 侧 w>h 几何判据可直接沿用。
# 模型输入 320x320 是方形，DetectionApp.config_preprocess() 会自动 letterbox。
RGB888P_SIZE = [640, 480]

# ---- 串口（3.3V TTL，必须与 STM32 共地）----
# 亚博 K230 豪华版（本工程）：UART1 -> GPIO9(TXD) / GPIO10(RXD)
#   GPIO9  -> STM32 PA10(USART1_RX)，GPIO10 <- PA9(USART1_TX)
# 庐山派 / 01Studio：        UART2 -> IO11(TXD)   / IO12(RXD)
# 01Studio 备选：            UART1 -> IO3(TXD)    / IO4(RXD)
UART_ID = 1
UART_TX_IO = 9
UART_RX_IO = 10
UART_BAUDRATE = 115200

# ---- 类别 id：严格按 deploy_config.json 的 categories 顺序 ----
# 0 fall  1 side-lying  2 sitting  3 stading(训练时拼写错误，原样保留)
# 注意：'side-lying' 里混入了零宽空格 U+200B、'stading' 拼错，
#       所以全程只用数字 id 判断，绝不用字符串比较。
CLS_FALL = 0
CLS_SIDE_LYING = 1
CLS_SITTING = 2
CLS_STANDING = 3
FALL_CLASSES = (CLS_FALL, CLS_SIDE_LYING)

# ---- 判定阈值 ----
# robust 模式：K230 侧自己稳判，业务层在模型 confidence_threshold=0.4 之上再收紧
FALL_TH = 0.55              # fall 类置信度门槛
SIDE_TH = 0.70              # side-lying 误报更多，门槛抬高
INCLUDE_SIDE_LYING = True   # 侧躺是否算跌倒；卧室/睡觉场景建议设 False

CONFIRM_FRAMES = 8          # 连续命中多少帧才置位本地报警（约 0.3s @25fps）
RELEASE_FRAMES = 30         # 连续多少帧未命中才解除
HEARTBEAT_MS = 500          # robust 模式心跳周期

# 进阶：要求「先出现站立/坐姿，之后才倒地」才算真跌倒，过滤静态躺卧
REQUIRE_TRANSITION = False
UPRIGHT_MEMORY_FRAMES = 90

DEBUG_PRINT = True          # 在 IDE 终端打印上报内容（走 REPL，不占 UART1）


# ============================ 2. 配置读取 ============================

def load_config(root):
    with open(root + "deploy_config.json", "r") as f:
        return _json.load(f)


cfg = load_config(ROOT_PATH)
kmodel_path = ROOT_PATH + cfg["kmodel_path"]
labels = cfg["categories"]
confidence_threshold = cfg["confidence_threshold"]
nms_threshold = cfg["nms_threshold"]
model_input_size = cfg["img_size"]
nms_option = cfg["nms_option"]
model_type = cfg["model_type"]

# AnchorBaseDet 专用：3 组 x 3 个 anchor 展平成 18 个数（官方部署包写法）
anchors = []
if model_type == "AnchorBaseDet":
    a = cfg["anchors"]
    anchors = a[0] + a[1] + a[2]

# legacy 模式不重复设阈值：K230 只做类别映射，
# 沿用 STM32 侧 FALL_SCORE_TH=0.50 + 10 帧滑窗判决，避免两级防抖叠加拖慢响应
if PROTOCOL_MODE == "legacy":
    FALL_TH = confidence_threshold
    SIDE_TH = confidence_threshold

print("[CFG] kmodel      :", kmodel_path)
print("[CFG] model_type  : %s  img_size=%s  nncase=%s" %
      (model_type, model_input_size, cfg.get("nncase_version", "?")))
print("[CFG] protocol    : %s  UART%d IO%d/%d @%d" %
      (PROTOCOL_MODE, UART_ID, UART_TX_IO, UART_RX_IO, UART_BAUDRATE))
print("[CFG] conf / nms  :", confidence_threshold, nms_threshold)
print("[CFG] anchors(%d) : %s" % (len(anchors), anchors))
for i, n in enumerate(labels):
    print("[CFG]  class %d   : %s (len=%d)" % (i, repr(n), len(n)))
# len('side-lying') 应为 10，打印 11 说明零宽空格确实存在；不影响 id 判断


# ============================ 3. UART 与帧构造 ============================

def uart_init():
    fpioa = FPIOA()
    tx_func = {1: FPIOA.UART1_TXD, 2: FPIOA.UART2_TXD}[UART_ID]
    rx_func = {1: FPIOA.UART1_RXD, 2: FPIOA.UART2_RXD}[UART_ID]
    uart_obj = {1: UART.UART1, 2: UART.UART2}[UART_ID]
    fpioa.set_function(UART_TX_IO, tx_func)
    fpioa.set_function(UART_RX_IO, rx_func)
    u = UART(uart_obj, baudrate=UART_BAUDRATE,
             bits=UART.EIGHTBITS, parity=UART.PARITY_NONE,
             stop=UART.STOPBITS_ONE)
    return u


uart = uart_init()
_seq = 0


def build_legacy_frame(box, cls, score):
    """既有工程协议：$x,y,w,h,State,score#（每帧发送）"""
    state = "Fall" if cls >= 0 else "NoFall"
    if box is None:
        x, y, w, h = 0, 0, 0, 0
    else:
        x, y, w, h = box
    s = score if cls >= 0 else 0.0
    return "$%d,%d,%d,%d,%s,%.2f#" % (x, y, w, h, state, s)


def build_robust_frame(evt, state, cls, score, cx, cy):
    """带 XOR 校验的状态帧：$F,...,<seq>*<XOR>\r\n"""
    global _seq
    body = "F,%d,%d,%d,%d,%d,%d,%d" % (
        evt, state, cls, int(score * 100), cx, cy, _seq)
    chk = 0
    for ch in body:
        chk ^= ord(ch)
    _seq = (_seq + 1) & 0xFF
    return "$%s*%02X\r\n" % (body, chk)


def uart_send(frame):
    uart.write(frame)
    if DEBUG_PRINT:
        print("[UART]", frame.strip())


# ============================ 4. 跌倒判定状态机 ============================

def pick_fall_candidate(res):
    """从 DetectionApp.run() 结果里挑出置信度最高的「倒地」目标。

    res 结构（训练平台 libs/PlatTasks.py 的 DetectionApp）：
        res["boxes"]  -> [[x_min, y_min, x_max, y_max], ...]   RGB888P_SIZE 坐标域
        res["idx"]    -> [class_id, ...]
        res["scores"] -> [confidence, ...]
    返回 (cls, score, cx, cy, box)；cls = -1 表示本帧无有效倒地目标。
    box 为 [x, y, w, h]，供 legacy 协议原样发出。
    """
    best_cls, best_score, cx, cy, box = -1, 0.0, -1, -1, None
    if not res or not res.get("boxes"):
        return best_cls, best_score, cx, cy, box

    boxes, idxs, scores = res["boxes"], res["idx"], res["scores"]
    for i in range(len(boxes)):
        c = int(idxs[i])
        if c not in FALL_CLASSES:
            continue
        if c == CLS_SIDE_LYING and not INCLUDE_SIDE_LYING:
            continue
        s = float(scores[i])
        th = FALL_TH if c == CLS_FALL else SIDE_TH
        if s < th or s <= best_score:
            continue
        b = boxes[i]
        box = [int(b[0]), int(b[1]), int(b[2] - b[0]), int(b[3] - b[1])]
        cx = int((b[0] + b[2]) * 50 / RGB888P_SIZE[0])
        cy = int((b[1] + b[3]) * 50 / RGB888P_SIZE[1])
        best_cls, best_score = c, s
    return best_cls, best_score, cx, cy, box


def has_upright(res):
    """本帧是否出现站立/坐姿，用于「站立->倒地」转移判定。"""
    if not res or not res.get("idx"):
        return False
    for c in res["idx"]:
        if int(c) in (CLS_SITTING, CLS_STANDING):
            return True
    return False


class FallFSM:
    """本地去抖。legacy 模式只用于 OSD 显示，上报仍按本帧结果逐帧发。"""

    def __init__(self):
        self.state = 0              # 0=正常 1=跌倒报警
        self.hit = 0
        self.miss = 0
        self.last_cls = -1
        self.last_score = 0.0
        self.last_cx = -1
        self.last_cy = -1
        self.upright_age = 0xFFFF

    def update(self, cls, score, cx, cy, upright):
        self.upright_age = 0 if upright else self.upright_age + 1

        if cls >= 0:
            self.hit += 1
            self.miss = 0
            self.last_cls, self.last_score = cls, score
            self.last_cx, self.last_cy = cx, cy
        else:
            self.miss += 1
            if self.miss >= RELEASE_FRAMES:
                self.hit = 0

        allow = (not REQUIRE_TRANSITION) or (self.upright_age <= UPRIGHT_MEMORY_FRAMES)
        changed = False
        if self.state == 0 and self.hit >= CONFIRM_FRAMES and allow:
            self.state = 1
            changed = True
        elif self.state == 1 and self.miss >= RELEASE_FRAMES:
            self.state = 0
            changed = True
            self.hit = 0
            self.last_cls, self.last_score = -1, 0.0
            self.last_cx, self.last_cy = -1, -1
        return changed


# ============================ 5. 主流程 ============================

pl = PipeLine(rgb888p_size=RGB888P_SIZE, display_mode=DISPLAY_MODE)
pl.create()
display_size = pl.get_display_size()

det_app = DetectionApp(
    "video",                    # inference_mode
    kmodel_path,
    labels,
    model_input_size,
    anchors,
    model_type,
    confidence_threshold,
    nms_threshold,
    RGB888P_SIZE,
    display_size,
    debug_mode=0
)
det_app.config_preprocess()

fsm = FallFSM()
last_hb = time.ticks_ms()


def draw_state_bar():
    """在 OSD 上画状态条，必须在 det_app.draw_result() 之后调用。"""
    if fsm.state == 1:
        text, color = "FALL DETECTED  cls=%d  %.2f" % (fsm.last_cls, fsm.last_score), (255, 255, 0, 0)
    else:
        text, color = "NORMAL", (255, 0, 255, 0)
    pl.osd_img.draw_rectangle(0, 0, min(display_size[0] - 1, 460), 40,
                              color=(255, 0, 0, 0), thickness=2)
    pl.osd_img.draw_string_advanced(8, 6, 28, text, color=color)


try:
    # 开机首帧，便于 STM32 / 串口助手确认链路已建立
    if PROTOCOL_MODE == "legacy":
        uart_send(build_legacy_frame(None, -1, 0.0))
    else:
        uart_send(build_robust_frame(1, 0, -1, 0.0, -1, -1))

    while True:
        os.exitpoint()

        img = pl.get_frame()
        res = det_app.run(img)

        cls, score, cx, cy, box = pick_fall_candidate(res)
        changed = fsm.update(cls, score, cx, cy, has_upright(res))

        if PROTOCOL_MODE == "legacy":
            # 逐帧透传本帧结果，判决交给 STM32 侧已有滑动窗口
            uart_send(build_legacy_frame(box, cls, score))
        else:
            now = time.ticks_ms()
            if changed or time.ticks_diff(now, last_hb) >= HEARTBEAT_MS:
                uart_send(build_robust_frame(1 if changed else 0,
                                             fsm.state, fsm.last_cls, fsm.last_score,
                                             fsm.last_cx, fsm.last_cy))
                last_hb = now

        det_app.draw_result(pl.osd_img, res)
        draw_state_bar()
        pl.show_image()
        gc.collect()

except Exception as e:
    import sys
    sys.print_exception(e)
finally:
    # 顺序不能反：先释放模型，再释放管线，最后关串口
    try:
        det_app.deinit()
    except Exception:
        pass
    try:
        pl.destroy()
    except Exception:
        pass
    try:
        uart.deinit()
    except Exception:
        pass
    gc.collect()
    print("[SYS] released")
