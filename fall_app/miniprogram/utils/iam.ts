/**
 * 华为云 IAM Token 获取与缓存（有效期 24h，缓存 23h；配置变更自动失效重取）
 */

import { CloudConfig } from '../config'
import { getCloudConfig, hasValidIamCredentials } from './storage'

const CACHE_KEY = 'fall_guard_iam_token_v1'
const CACHE_MS = 23 * 3600 * 1000

interface CachedToken {
  token: string
  ts: number
  signature: string
}

function buildSignature(cfg: CloudConfig): string {
  return `${cfg.iamDomain}|${cfg.iamUser}|${cfg.projectId}|${cfg.region}`
}

export function clearToken(): void {
  try {
    wx.removeStorageSync(CACHE_KEY)
  } catch {
    // ignore
  }
}

export function requestToken(overrideCfg?: CloudConfig): Promise<string> {
  const cfg = overrideCfg || getCloudConfig()
  if (!hasValidIamCredentials(cfg)) {
    return Promise.reject(new Error('IAM_NOT_CONFIGURED'))
  }

  const iamUrl = `https://iam.${cfg.region}.myhuaweicloud.com/v3/auth/tokens`
  const signature = buildSignature(cfg)

  return new Promise((resolve, reject) => {
    wx.request({
      url: iamUrl,
      method: 'POST',
      timeout: 10000,
      header: { 'Content-Type': 'application/json' },
      data: {
        auth: {
          identity: {
            methods: ['password'],
            password: {
              user: {
                name: cfg.iamUser,
                password: cfg.iamPassword,
                domain: { name: cfg.iamDomain },
              },
            },
          },
          scope: { project: { id: cfg.projectId } },
        },
      },
      success(res) {
        if (res.statusCode === 201) {
          const headers = (res.header || {}) as Record<string, string>
          const token = headers['X-Subject-Token'] || headers['x-subject-token']
          if (token) {
            const payload: CachedToken = { token, ts: Date.now(), signature }
            try {
              wx.setStorageSync(CACHE_KEY, payload)
            } catch {
              // ignore
            }
            resolve(token)
            return
          }
          reject(new Error('IAM 响应缺少 X-Subject-Token 头'))
          return
        }
        if (res.statusCode === 400 || res.statusCode === 401) {
          reject(new Error(`IAM 认证被拒绝 (HTTP ${res.statusCode})：请核对账号名/子用户名/密码/项目ID`))
          return
        }
        reject(new Error(`IAM 请求失败 HTTP ${res.statusCode}`))
      },
      fail(err) {
        reject(new Error(`IAM 网络失败：${err.errMsg || '请检查网络或勾选不校验合法域名'}`))
      },
    })
  })
}

export function getToken(overrideCfg?: CloudConfig): Promise<string> {
  const cfg = overrideCfg || getCloudConfig()
  if (!hasValidIamCredentials(cfg)) {
    return Promise.reject(new Error('IAM_NOT_CONFIGURED'))
  }

  const signature = buildSignature(cfg)
  try {
    const cached = wx.getStorageSync(CACHE_KEY) as CachedToken | undefined
    if (
      cached &&
      cached.token &&
      cached.signature === signature &&
      Date.now() - cached.ts < CACHE_MS
    ) {
      return Promise.resolve(cached.token)
    }
  } catch {
    // ignore cache errors
  }

  return requestToken(cfg)
}
