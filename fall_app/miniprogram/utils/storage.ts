/**
 * 本地持久化管理：云端配置、家人守护偏好、告警事件历史
 * 兼容无 wx 全局环境的 Node 单元测试运行。
 */

import {
  CarePreferences,
  CloudConfig,
  DEFAULT_CARE_PREFS,
  DEFAULT_CLOUD_CONFIG,
} from '../config'
import { DataSourceMode, formatLocalDateTime } from './protocol'

const KEY_CLOUD_CONFIG = 'fall_guard_cloud_config_v1'
const KEY_CARE_PREFS = 'fall_guard_care_prefs_v1'
const KEY_HISTORY = 'fall_guard_history_v1'
const MAX_HISTORY_COUNT = 100

const memoryFallback = new Map<string, unknown>()

export type HistoryCategory = 'fall' | 'sensor' | 'action'
export type HistorySeverity = 'danger' | 'warn' | 'info' | 'safe'
export type FallEpisodeStatus = 'active' | 'muted' | 'resolved'

export interface HistoryEvent {
  id: string
  category: HistoryCategory
  severity: HistorySeverity
  title: string
  detail: string
  timestampMs: number
  timeText: string
  episodeStatus?: FallEpisodeStatus
  confidencePct?: number
  zoneLabel?: string
  source: DataSourceMode
}

function readStorage<T>(key: string): T | null {
  try {
    if (typeof wx !== 'undefined' && typeof wx.getStorageSync === 'function') {
      const val = wx.getStorageSync(key)
      return (val !== undefined && val !== '') ? (val as T) : null
    }
    return memoryFallback.has(key) ? (memoryFallback.get(key) as T) : null
  } catch {
    return null
  }
}

function writeStorage<T>(key: string, value: T): void {
  try {
    if (typeof wx !== 'undefined' && typeof wx.setStorageSync === 'function') {
      wx.setStorageSync(key, value)
      return
    }
    memoryFallback.set(key, value)
  } catch {
    // 忽略存储配额等非致命错误
  }
}

function removeStorage(key: string): void {
  try {
    if (typeof wx !== 'undefined' && typeof wx.removeStorageSync === 'function') {
      wx.removeStorageSync(key)
      return
    }
    memoryFallback.delete(key)
  } catch {
    // 忽略异常
  }
}

export function getCloudConfig(): CloudConfig {
  const saved = readStorage<Partial<CloudConfig>>(KEY_CLOUD_CONFIG)
  return {
    ...DEFAULT_CLOUD_CONFIG,
    ...(saved || {}),
  }
}

export function saveCloudConfig(partial: Partial<CloudConfig>): CloudConfig {
  const merged: CloudConfig = {
    ...getCloudConfig(),
    ...partial,
  }
  merged.endpoint = merged.endpoint.trim().replace(/\/+$/, '')
  merged.iamDomain = merged.iamDomain.trim()
  merged.iamUser = merged.iamUser.trim()
  merged.projectId = merged.projectId.trim()
  merged.instanceId = merged.instanceId.trim()
  merged.deviceId = merged.deviceId.trim()
  merged.serviceId = merged.serviceId.trim() || 'FallDetect'
  merged.region = merged.region.trim() || 'cn-north-4'
  writeStorage(KEY_CLOUD_CONFIG, merged)
  return merged
}

export function resetCloudConfig(): CloudConfig {
  removeStorage(KEY_CLOUD_CONFIG)
  return { ...DEFAULT_CLOUD_CONFIG }
}

export function hasValidIamCredentials(cfg: CloudConfig): boolean {
  return Boolean(
    cfg.iamDomain &&
    cfg.iamUser &&
    cfg.iamPassword &&
    cfg.projectId &&
    cfg.endpoint &&
    cfg.deviceId,
  )
}

export function getCarePrefs(): CarePreferences {
  const saved = readStorage<Partial<CarePreferences>>(KEY_CARE_PREFS)
  return {
    ...DEFAULT_CARE_PREFS,
    ...(saved || {}),
  }
}

export function saveCarePrefs(partial: Partial<CarePreferences>): CarePreferences {
  const merged: CarePreferences = {
    ...getCarePrefs(),
    ...partial,
  }
  merged.elderName = merged.elderName.trim() || DEFAULT_CARE_PREFS.elderName
  merged.roomLabel = merged.roomLabel.trim() || DEFAULT_CARE_PREFS.roomLabel
  merged.emergencyName = merged.emergencyName.trim() || DEFAULT_CARE_PREFS.emergencyName
  merged.emergencyPhone = merged.emergencyPhone.trim()
  if (merged.refreshMs < 1000 || Number.isNaN(merged.refreshMs)) {
    merged.refreshMs = DEFAULT_CARE_PREFS.refreshMs
  }
  writeStorage(KEY_CARE_PREFS, merged)
  return merged
}

export function getHistoryList(): HistoryEvent[] {
  const list = readStorage<HistoryEvent[]>(KEY_HISTORY)
  return Array.isArray(list) ? list : []
}

export interface NewHistoryInput {
  category: HistoryCategory
  severity: HistorySeverity
  title: string
  detail: string
  source: DataSourceMode
  episodeStatus?: FallEpisodeStatus
  confidencePct?: number
  zoneLabel?: string
  timestampMs?: number
}

export function addHistoryEvent(input: NewHistoryInput): HistoryEvent {
  const ts = input.timestampMs || Date.now()
  const item: HistoryEvent = {
    id: `${ts}_${Math.random().toString(36).slice(2, 7)}`,
    category: input.category,
    severity: input.severity,
    title: input.title,
    detail: input.detail,
    timestampMs: ts,
    timeText: formatLocalDateTime(new Date(ts)),
    episodeStatus: input.episodeStatus,
    confidencePct: input.confidencePct,
    zoneLabel: input.zoneLabel,
    source: input.source,
  }
  const list = getHistoryList()
  list.unshift(item)
  if (list.length > MAX_HISTORY_COUNT) {
    list.length = MAX_HISTORY_COUNT
  }
  writeStorage(KEY_HISTORY, list)
  return item
}

export function updateLatestActiveFallStatus(
  status: FallEpisodeStatus,
  appendDetail?: string,
): void {
  const list = getHistoryList()
  const idx = list.findIndex(
    (item) => item.category === 'fall' && (item.episodeStatus === 'active' || item.episodeStatus === 'muted'),
  )
  if (idx < 0) {
    return
  }
  list[idx] = {
    ...list[idx],
    episodeStatus: status,
    detail: appendDetail ? `${list[idx].detail} · ${appendDetail}` : list[idx].detail,
  }
  writeStorage(KEY_HISTORY, list)
}

export function clearHistoryList(): void {
  removeStorage(KEY_HISTORY)
}
