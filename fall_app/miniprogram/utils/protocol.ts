/**
 * FallDetect 物模型解析、坐标框归一化与本地时间格式化
 */

export const CAMERA_WIDTH = 640
export const CAMERA_HEIGHT = 480

export type DataSourceMode = 'cloud' | 'demo'
export type SafetySeverity = 'safe' | 'warn' | 'danger'

export interface ParsedBbox {
  raw: string
  hasBox: boolean
  x: number
  y: number
  w: number
  h: number
  leftPct: number
  topPct: number
  widthPct: number
  heightPct: number
  zoneLabel: string
  horizontalPose: boolean
}

export interface FallSnapshot {
  source: DataSourceMode
  online: boolean
  deviceStatus: string
  deviceStatusText: string
  fallState: number
  beepOn: boolean
  visionOffline: boolean
  confidence: number
  confidencePct: number
  bbox: ParsedBbox
  eventTimeRaw: string
  eventTimeText: string
  relativeTimeText: string
  severity: SafetySeverity
  heroTitle: string
  heroSubtitle: string
}

function clamp(val: number, min: number, max: number): number {
  return Math.max(min, Math.min(max, val))
}

function pad2(n: number): string {
  return n < 10 ? '0' + n : String(n)
}

export function formatLocalDateTime(date: Date): string {
  const m = pad2(date.getMonth() + 1)
  const d = pad2(date.getDate())
  const hh = pad2(date.getHours())
  const mm = pad2(date.getMinutes())
  const ss = pad2(date.getSeconds())
  return `${m}-${d} ${hh}:${mm}:${ss}`
}

export function formatShortClock(date: Date): string {
  return `${pad2(date.getHours())}:${pad2(date.getMinutes())}:${pad2(date.getSeconds())}`
}

/**
 * 解析华为云 IoTDA 上报时间 yyyyMMdd'T'HHmmss'Z'（UTC -> 本地时间）
 */
export function parseIotdaEventTime(raw: string): Date | null {
  if (!raw) {
    return null
  }
  const m = /^(\d{4})(\d{2})(\d{2})T(\d{2})(\d{2})(\d{2})Z$/i.exec(raw.trim())
  if (m) {
    const ms = Date.UTC(
      Number(m[1]),
      Number(m[2]) - 1,
      Number(m[3]),
      Number(m[4]),
      Number(m[5]),
      Number(m[6]),
    )
    return new Date(ms)
  }
  const parsed = new Date(raw)
  return Number.isNaN(parsed.getTime()) ? null : parsed
}

export function formatRelativeTime(date: Date | null, nowMs: number = Date.now()): string {
  if (!date) {
    return '暂无上报'
  }
  const diffSec = Math.max(0, Math.floor((nowMs - date.getTime()) / 1000))
  if (diffSec < 8) {
    return '刚刚同步'
  }
  if (diffSec < 60) {
    return `${diffSec}秒前`
  }
  const diffMin = Math.floor(diffSec / 60)
  if (diffMin < 60) {
    return `${diffMin}分钟前`
  }
  const diffHour = Math.floor(diffMin / 60)
  if (diffHour < 24) {
    return `${diffHour}小时前`
  }
  return `${Math.floor(diffHour / 24)}天前`
}

/**
 * 解析 bbox 字段（兼容字符串 "[86,76,355,257]" 与数组 [86,76,355,257]）
 */
export function parseBbox(raw: unknown): ParsedBbox {
  let nums: number[] = []
  let rawText = '[0,0,0,0]'

  if (Array.isArray(raw)) {
    nums = raw.slice(0, 4).map((item) => Number(item))
    rawText = `[${nums.join(',')}]`
  } else if (typeof raw === 'string' && raw.trim().length > 0) {
    rawText = raw.trim()
    const matches = rawText.match(/-?\d+(?:\.\d+)?/g)
    if (matches) {
      nums = matches.slice(0, 4).map((s) => Number(s))
    }
  }

  if (nums.length < 4 || nums.some((n) => Number.isNaN(n))) {
    return {
      raw: '[0,0,0,0]',
      hasBox: false,
      x: 0,
      y: 0,
      w: 0,
      h: 0,
      leftPct: 0,
      topPct: 0,
      widthPct: 0,
      heightPct: 0,
      zoneLabel: '暂无目标框',
      horizontalPose: false,
    }
  }

  const x = Math.max(0, Math.round(nums[0]))
  const y = Math.max(0, Math.round(nums[1]))
  const w = Math.max(0, Math.round(nums[2]))
  const h = Math.max(0, Math.round(nums[3]))
  const hasBox = w > 0 && h > 0

  if (!hasBox) {
    return {
      raw: rawText,
      hasBox: false,
      x: 0,
      y: 0,
      w: 0,
      h: 0,
      leftPct: 0,
      topPct: 0,
      widthPct: 0,
      heightPct: 0,
      zoneLabel: '当前画面安全无告警框',
      horizontalPose: false,
    }
  }

  const leftPct = clamp((x / CAMERA_WIDTH) * 100, 0, 96)
  const topPct = clamp((y / CAMERA_HEIGHT) * 100, 0, 96)
  const widthPct = clamp((w / CAMERA_WIDTH) * 100, 4, 100 - leftPct)
  const heightPct = clamp((h / CAMERA_HEIGHT) * 100, 4, 100 - topPct)

  const cx = x + w / 2
  const cy = y + h / 2
  const hPos = cx < CAMERA_WIDTH * 0.34 ? '左侧' : (cx > CAMERA_WIDTH * 0.66 ? '右侧' : '中部')
  const vPos = cy < CAMERA_HEIGHT * 0.35 ? '远景上方' : (cy > CAMERA_HEIGHT * 0.68 ? '近景下方' : '中央')
  const zoneLabel = hPos === '中部' && vPos === '中央' ? '画面正中央' : `画面${hPos}${vPos}`

  return {
    raw: `[${x},${y},${w},${h}]`,
    hasBox: true,
    x,
    y,
    w,
    h,
    leftPct: Number(leftPct.toFixed(1)),
    topPct: Number(topPct.toFixed(1)),
    widthPct: Number(widthPct.toFixed(1)),
    heightPct: Number(heightPct.toFixed(1)),
    zoneLabel,
    horizontalPose: w > h,
  }
}

export function statusTextOfDevice(status: string): string {
  switch (status) {
    case 'ONLINE':
      return '设备在线'
    case 'OFFLINE':
      return '设备离线'
    case 'INACTIVE':
      return '未激活'
    case 'ABNORMAL':
      return '状态异常'
    case 'FROZEN':
      return '已冻结'
    default:
      return status || '状态未知'
  }
}

export function buildFallSnapshot(
  deviceStatus: string,
  props: Record<string, unknown>,
  eventTimeRaw: string,
  source: DataSourceMode,
  nowMs: number = Date.now(),
): FallSnapshot {
  const online = deviceStatus === 'ONLINE'
  const fallState = Number(props.fall_state || 0) === 1 ? 1 : 0
  const beepOn = Number(props.beep || 0) === 1
  const visionOffline = Number(props.vision_offline || 0) === 1
  const rawConf = Number(props.confidence || 0)
  const confidence = Number.isNaN(rawConf) ? 0 : clamp(rawConf, 0, 1)
  const confidencePct = Math.round(confidence * 100)
  const bbox = parseBbox(props.bbox)
  const eventDate = parseIotdaEventTime(eventTimeRaw)
  const eventTimeText = eventDate ? formatLocalDateTime(eventDate) : '--'
  const relativeTimeText = formatRelativeTime(eventDate, nowMs)

  let severity: SafetySeverity = 'safe'
  let heroTitle = '居家安全 · 姿态正常'
  let heroSubtitle = '视觉监测正常运转中，暂未发现跌倒异常'

  if (fallState === 1) {
    severity = 'danger'
    heroTitle = '紧急告警 · 疑似发生跌倒'
    heroSubtitle = beepOn
      ? `家中蜂鸣器报警响铃中（可信度 ${confidencePct}%），请立即联系确认安全`
      : `蜂鸣器已远程静音（可信度 ${confidencePct}%），报警保持中，可确认无事后复位`
  } else if (!online) {
    severity = 'warn'
    heroTitle = '监护中断 · 设备当前离线'
    heroSubtitle = 'STM32/WiFi 模块暂未连接华为云，无法实时检测跌倒，请检查家里电源与网络'
  } else if (visionOffline) {
    severity = 'warn'
    heroTitle = '传感器注意 · K230 通信超时'
    heroSubtitle = '主控在线但超过 5 秒未收到 K230 视觉帧，请检查摄像头供电与串口连接'
  }

  return {
    source,
    online,
    deviceStatus,
    deviceStatusText: statusTextOfDevice(deviceStatus),
    fallState,
    beepOn,
    visionOffline,
    confidence,
    confidencePct,
    bbox,
    eventTimeRaw,
    eventTimeText,
    relativeTimeText,
    severity,
    heroTitle,
    heroSubtitle,
  }
}
