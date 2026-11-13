/**
 * 监护状态同步与事件边沿检测控制器
 *  - 统一调度“华为云实时模式”与“本地模拟演练模式”
 *  - 去重记录跌倒触发/解除、K230 掉线/恢复、设备上下线与远程控制历史
 */

import { CarePreferences, CloudConfig } from '../config'
import { applyDemoCommand, readDemoPayload } from './demo'
import { FallCommandName, getDeviceData, getDeviceStatus, sendCommand } from './iotda'
import { buildFallSnapshot, FallSnapshot } from './protocol'
import {
  addHistoryEvent,
  getCarePrefs,
  getCloudConfig,
  hasValidIamCredentials,
  updateLatestActiveFallStatus,
} from './storage'

export type SyncOutcomeKind = 'ok' | 'need_config' | 'error'

export interface SyncOutcome {
  kind: SyncOutcomeKind
  snapshot?: FallSnapshot
  fallJustTriggered: boolean
  shouldAlert: boolean
  errorMsg: string
}

interface MonitorTracker {
  initialized: boolean
  prevFallState: number
  prevVisionOffline: boolean
  prevOnline: boolean
  localAlarmSilenced: boolean
}

const tracker: MonitorTracker = {
  initialized: false,
  prevFallState: 0,
  prevVisionOffline: false,
  prevOnline: true,
  localAlarmSilenced: false,
}

export function resetMonitorTracker(): void {
  tracker.initialized = false
  tracker.prevFallState = 0
  tracker.prevVisionOffline = false
  tracker.prevOnline = true
  tracker.localAlarmSilenced = false
}

export function silenceLocalAlarm(): void {
  tracker.localAlarmSilenced = true
}

function recordTransitions(snap: FallSnapshot, prefs: CarePreferences): boolean {
  let fallJustTriggered = false
  const label = `${prefs.elderName}（${prefs.roomLabel}）`

  if (!tracker.initialized) {
    tracker.initialized = true
    tracker.prevFallState = snap.fallState
    tracker.prevVisionOffline = snap.visionOffline
    tracker.prevOnline = snap.online
    if (snap.fallState === 1) {
      fallJustTriggered = true
      tracker.localAlarmSilenced = false
      addHistoryEvent({
        category: 'fall',
        severity: 'danger',
        title: '检测到疑似跌倒',
        detail: `${label} · 置信度 ${snap.confidencePct}% · 位置：${snap.bbox.zoneLabel}`,
        episodeStatus: snap.beepOn ? 'active' : 'muted',
        confidencePct: snap.confidencePct,
        zoneLabel: snap.bbox.zoneLabel,
        source: snap.source,
      })
    }
    return fallJustTriggered
  }

  /* 1. 跌倒状态边沿 */
  if (tracker.prevFallState === 0 && snap.fallState === 1) {
    fallJustTriggered = true
    tracker.localAlarmSilenced = false
    addHistoryEvent({
      category: 'fall',
      severity: 'danger',
      title: '检测到疑似跌倒',
      detail: `${label} · 置信度 ${snap.confidencePct}% · 位置：${snap.bbox.zoneLabel}`,
      episodeStatus: snap.beepOn ? 'active' : 'muted',
      confidencePct: snap.confidencePct,
      zoneLabel: snap.bbox.zoneLabel,
      source: snap.source,
    })
  } else if (tracker.prevFallState === 1 && snap.fallState === 0) {
    tracker.localAlarmSilenced = false
    updateLatestActiveFallStatus('resolved', '报警已解除')
    addHistoryEvent({
      category: 'fall',
      severity: 'safe',
      title: '跌倒报警已解除',
      detail: `${label} 已恢复正常监测状态`,
      source: snap.source,
    })
  }

  /* 2. K230 视觉传感器掉线边沿 */
  if (!tracker.prevVisionOffline && snap.visionOffline) {
    addHistoryEvent({
      category: 'sensor',
      severity: 'warn',
      title: 'K230 视觉模块通信超时',
      detail: '超过 5 秒未收到视觉识别帧，请检查 K230 串口与供电',
      source: snap.source,
    })
  } else if (tracker.prevVisionOffline && !snap.visionOffline && snap.online) {
    addHistoryEvent({
      category: 'sensor',
      severity: 'info',
      title: 'K230 视觉模块恢复在线',
      detail: '视觉识别帧率与串口心跳已恢复正常',
      source: snap.source,
    })
  }

  /* 3. 设备上下线边沿 */
  if (tracker.prevOnline && !snap.online) {
    addHistoryEvent({
      category: 'sensor',
      severity: 'warn',
      title: '监护主机离线',
      detail: 'STM32/ESP-01S 与华为云连接断开，请检查 WiFi 与电源',
      source: snap.source,
    })
  } else if (!tracker.prevOnline && snap.online) {
    addHistoryEvent({
      category: 'sensor',
      severity: 'info',
      title: '监护主机重新上线',
      detail: '设备已恢复与华为云 IoTDA 通信',
      source: snap.source,
    })
  }

  tracker.prevFallState = snap.fallState
  tracker.prevVisionOffline = snap.visionOffline
  tracker.prevOnline = snap.online
  return fallJustTriggered
}

export async function syncMonitorSnapshot(
  overridePrefs?: CarePreferences,
  overrideCfg?: CloudConfig,
): Promise<SyncOutcome> {
  const prefs = overridePrefs || getCarePrefs()
  const cfg = overrideCfg || getCloudConfig()

  if (prefs.demoMode) {
    const demo = readDemoPayload()
    const snap = buildFallSnapshot(demo.deviceStatus, demo.props, demo.eventTime, 'demo')
    const fallJustTriggered = recordTransitions(snap, prefs)
    const shouldAlert = snap.fallState === 1 && !tracker.localAlarmSilenced
    return {
      kind: 'ok',
      snapshot: snap,
      fallJustTriggered,
      shouldAlert,
      errorMsg: '',
    }
  }

  if (!hasValidIamCredentials(cfg)) {
    return {
      kind: 'need_config',
      fallJustTriggered: false,
      shouldAlert: false,
      errorMsg: '尚未配置华为云 IAM 子用户名与密码，可开启“本地模拟演练”或在设置页填入',
    }
  }

  try {
    const [status, shadow] = await Promise.all([getDeviceStatus(cfg), getDeviceData(cfg)])
    const snap = buildFallSnapshot(status, shadow.props, shadow.eventTime, 'cloud')
    const fallJustTriggered = recordTransitions(snap, prefs)
    const shouldAlert = snap.fallState === 1 && !tracker.localAlarmSilenced
    return {
      kind: 'ok',
      snapshot: snap,
      fallJustTriggered,
      shouldAlert,
      errorMsg: '',
    }
  } catch (err: unknown) {
    const msg = err instanceof Error ? err.message : String(err)
    return {
      kind: 'error',
      fallJustTriggered: false,
      shouldAlert: false,
      errorMsg: msg,
    }
  }
}

export async function dispatchMonitorCommand(
  cmd: FallCommandName,
  overridePrefs?: CarePreferences,
  overrideCfg?: CloudConfig,
): Promise<void> {
  const prefs = overridePrefs || getCarePrefs()
  const cfg = overrideCfg || getCloudConfig()
  const source = prefs.demoMode ? 'demo' : 'cloud'

  if (prefs.demoMode) {
    applyDemoCommand(cmd)
  } else {
    await sendCommand(cmd, {}, cfg)
  }

  if (cmd === 'MUTE') {
    tracker.localAlarmSilenced = true
    updateLatestActiveFallStatus('muted', '家人已远程消音')
    addHistoryEvent({
      category: 'action',
      severity: 'info',
      title: '远程下发消音 (MUTE)',
      detail: '已通知设备关闭现场蜂鸣器报警音',
      source,
    })
  } else if (cmd === 'RESET') {
    tracker.localAlarmSilenced = false
    updateLatestActiveFallStatus('resolved', '家人已确认并复位')
    addHistoryEvent({
      category: 'action',
      severity: 'info',
      title: '远程复位监护 (RESET)',
      detail: '已通知 STM32 清除当前报警状态并回到正常监测',
      source,
    })
  }
}
