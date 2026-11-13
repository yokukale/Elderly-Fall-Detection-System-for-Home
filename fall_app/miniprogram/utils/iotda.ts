/**
 * 华为云 IoTDA v5 REST 客户端（设备状态查询 / 设备影子读取 / 远程命令下发）
 */

import { CloudConfig } from '../config'
import { clearToken, getToken, requestToken } from './iam'
import { getCloudConfig } from './storage'

export type FallCommandName = 'MUTE' | 'RESET'

export interface DeviceShadowResult {
  props: Record<string, unknown>
  eventTime: string
}

interface ShadowItem {
  service_id?: string
  reported?: {
    properties?: Record<string, unknown>
    event_time?: string
  }
}

interface ShadowResponse {
  shadow?: ShadowItem[]
}

interface DeviceListResponse {
  devices?: Array<{ status?: string }>
  status?: string
}

function rawRequest(
  method: 'GET' | 'POST',
  path: string,
  body: Record<string, unknown> | undefined,
  token: string,
  cfg: CloudConfig,
): Promise<WechatMiniprogram.RequestSuccessCallbackResult<Record<string, unknown>>> {
  const base = cfg.endpoint.replace(/\/+$/, '')
  return new Promise((resolve, reject) => {
    wx.request({
      url: `${base}${path}`,
      method,
      timeout: 10000,
      header: {
        'Content-Type': 'application/json',
        'X-Auth-Token': token,
        'Instance-Id': cfg.instanceId,
      },
      data: body || {},
      success: (res) => resolve(res as WechatMiniprogram.RequestSuccessCallbackResult<Record<string, unknown>>),
      fail: (err) => {
        reject(new Error(`请求华为云失败：${err.errMsg || '网络异常'}（请在开发者工具勾选“不校验合法域名”）`))
      },
    })
  })
}

function unwrapResponse<T>(
  res: WechatMiniprogram.RequestSuccessCallbackResult<Record<string, unknown>>,
): T {
  if (res.statusCode >= 200 && res.statusCode < 300) {
    return (res.data || {}) as unknown as T
  }
  if (res.statusCode === 401) {
    clearToken()
    throw new Error('TOKEN_EXPIRED')
  }
  const d = (res.data || {}) as Record<string, unknown>
  const msg = String(d.error_msg || d.message || '')
  throw new Error(`HTTP ${res.statusCode}${msg ? ' ' + msg : ''}`)
}

function apiCall<T>(
  method: 'GET' | 'POST',
  path: string,
  body?: Record<string, unknown>,
  overrideCfg?: CloudConfig,
): Promise<T> {
  const cfg = overrideCfg || getCloudConfig()
  return getToken(cfg)
    .then((token) => rawRequest(method, path, body, token, cfg))
    .then((res) => unwrapResponse<T>(res))
    .catch((err: unknown) => {
      if (err instanceof Error && err.message === 'TOKEN_EXPIRED') {
        return requestToken(cfg)
          .then((token) => rawRequest(method, path, body, token, cfg))
          .then((res) => unwrapResponse<T>(res))
      }
      throw err
    })
}

export function getDeviceStatus(overrideCfg?: CloudConfig): Promise<string> {
  const cfg = overrideCfg || getCloudConfig()
  const path = `/v5/iot/${encodeURIComponent(cfg.projectId)}/devices?device_ids=${encodeURIComponent(cfg.deviceId)}`
  return apiCall<DeviceListResponse>('GET', path, undefined, cfg).then((data) => {
    const first = (data.devices || [])[0]
    return (first && first.status) || data.status || 'UNKNOWN'
  })
}

export function getDeviceData(overrideCfg?: CloudConfig): Promise<DeviceShadowResult> {
  const cfg = overrideCfg || getCloudConfig()
  const path = `/v5/iot/${encodeURIComponent(cfg.projectId)}/devices/${encodeURIComponent(cfg.deviceId)}/shadow`
  return apiCall<ShadowResponse>('GET', path, undefined, cfg).then((data) => {
    let props: Record<string, unknown> = {}
    let eventTime = ''
    const list = data.shadow || []
    for (let i = 0; i < list.length; i++) {
      const item = list[i]
      if (item.service_id === cfg.serviceId && item.reported) {
        props = item.reported.properties || {}
        eventTime = item.reported.event_time || ''
        break
      }
    }
    return { props, eventTime }
  })
}

export function sendCommand(
  commandName: FallCommandName,
  paras: Record<string, unknown> = {},
  overrideCfg?: CloudConfig,
): Promise<Record<string, unknown>> {
  const cfg = overrideCfg || getCloudConfig()
  const path = `/v5/iot/${encodeURIComponent(cfg.projectId)}/devices/${encodeURIComponent(cfg.deviceId)}/commands`
  return apiCall<Record<string, unknown>>(
    'POST',
    path,
    {
      service_id: cfg.serviceId,
      command_name: commandName,
      paras,
    },
    cfg,
  )
}
