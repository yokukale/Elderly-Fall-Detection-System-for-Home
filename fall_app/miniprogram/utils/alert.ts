/**
 * 手机端声震告警提示封装（支持冷却防叠音、支持随时停止）
 */

let audioCtx: WechatMiniprogram.InnerAudioContext | null = null
let lastCueAt = 0

function ensureAudio(): WechatMiniprogram.InnerAudioContext | null {
  try {
    if (!audioCtx && typeof wx !== 'undefined' && typeof wx.createInnerAudioContext === 'function') {
      audioCtx = wx.createInnerAudioContext()
      audioCtx.src = '/assets/audio/alert.mp3'
      audioCtx.obeyMuteSwitch = false
    }
    return audioCtx
  } catch {
    return null
  }
}

export function triggerAlertCue(options: {
  sound: boolean
  vibrate: boolean
  minGapMs?: number
}): void {
  const now = Date.now()
  const minGap = options.minGapMs !== undefined ? options.minGapMs : 2600
  if (now - lastCueAt < minGap) {
    return
  }
  lastCueAt = now

  if (options.vibrate && typeof wx !== 'undefined' && typeof wx.vibrateLong === 'function') {
    wx.vibrateLong({
      fail: () => {
        // 部分桌面模拟器不支持震动时静默忽略
      },
    })
  }

  if (options.sound) {
    const ctx = ensureAudio()
    if (ctx) {
      try {
        ctx.stop()
        ctx.seek(0)
        ctx.play()
      } catch {
        // 忽略播放时序错误
      }
    }
  }
}

export function stopAlertCue(): void {
  lastCueAt = 0
  if (audioCtx) {
    try {
      audioCtx.stop()
    } catch {
      // ignore
    }
  }
}
