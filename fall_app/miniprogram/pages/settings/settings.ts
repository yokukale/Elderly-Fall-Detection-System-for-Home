/** pages/settings/settings.ts — 守护设置与华为云 IoTDA 配置页面 **/

import { CarePreferences, CloudConfig } from '../../config'
import { triggerAlertCue } from '../../utils/alert'
import { clearToken, requestToken } from '../../utils/iam'
import { getDeviceData, getDeviceStatus } from '../../utils/iotda'
import { resetMonitorTracker } from '../../utils/monitor'
import { buildFallSnapshot } from '../../utils/protocol'
import {
  getCarePrefs,
  getCloudConfig,
  hasValidIamCredentials,
  resetCloudConfig,
  saveCarePrefs,
  saveCloudConfig,
} from '../../utils/storage'

interface RefreshOption {
  ms: number
  label: string
}

type TestStatus = 'idle' | 'ok' | 'err'

const REFRESH_OPTIONS: RefreshOption[] = [
  { ms: 2000, label: '2 秒' },
  { ms: 3000, label: '3 秒' },
  { ms: 5000, label: '5 秒' },
]

function trimCloudDraft(raw: CloudConfig): CloudConfig {
  return {
    iamDomain: raw.iamDomain.trim(),
    iamUser: raw.iamUser.trim(),
    iamPassword: raw.iamPassword,
    projectId: raw.projectId.trim(),
    instanceId: raw.instanceId.trim(),
    endpoint: raw.endpoint.trim().replace(/\/+$/, ''),
    deviceId: raw.deviceId.trim(),
    serviceId: raw.serviceId.trim(),
    region: raw.region.trim(),
  }
}

Page({
  data: {
    prefs: getCarePrefs(),
    cloud: getCloudConfig(),
    cloudConfigured: false,
    showPassword: false,
    showAdvanced: false,
    testing: false,
    testStatus: 'idle' as TestStatus,
    testTitle: '',
    testDetail: '',
    refreshOptions: REFRESH_OPTIONS,
  },

  onShow() {
    const prefs = getCarePrefs()
    const cloud = getCloudConfig()
    this.setData({
      prefs,
      cloud,
      cloudConfigured: hasValidIamCredentials(cloud),
    })
  },

  onPrefInput(e: WechatMiniprogram.Input) {
    const key = e.currentTarget.dataset.key as keyof CarePreferences | undefined
    if (!key) {
      return
    }
    this.setData({
      [`prefs.${key}`]: e.detail.value,
    })
  },

  onPrefSwitch(e: WechatMiniprogram.SwitchChange) {
    const key = e.currentTarget.dataset.key as keyof CarePreferences | undefined
    if (!key) {
      return
    }
    const updated = saveCarePrefs({ [key]: e.detail.value } as Partial<CarePreferences>)
    this.setData({ prefs: updated })
  },

  onRefreshFreqTap(e: WechatMiniprogram.TouchEvent) {
    const ms = Number(e.currentTarget.dataset.ms)
    if (!ms) {
      return
    }
    const updated = saveCarePrefs({ refreshMs: ms })
    this.setData({ prefs: updated })
  },

  onSavePrefsTap() {
    const draft = this.data.prefs
    const updated = saveCarePrefs({
      elderName: draft.elderName.trim() || '家中长辈',
      roomLabel: draft.roomLabel.trim() || '客厅监护区',
      emergencyName: draft.emergencyName.trim() || '紧急联系人',
      emergencyPhone: draft.emergencyPhone.trim(),
    })
    this.setData({ prefs: updated })
    wx.showToast({ title: '已保存看护档案', icon: 'success' })
  },

  onTestCueTap() {
    triggerAlertCue({ sound: true, vibrate: true, minGapMs: 0 })
    wx.showToast({ title: '已触发提示音与震动', icon: 'none' })
  },

  onCloudInput(e: WechatMiniprogram.Input) {
    const key = e.currentTarget.dataset.key as keyof CloudConfig | undefined
    if (!key) {
      return
    }
    this.setData({
      [`cloud.${key}`]: e.detail.value,
    })
  },

  onTogglePasswordTap() {
    this.setData({ showPassword: !this.data.showPassword })
  },

  onToggleAdvancedTap() {
    this.setData({ showAdvanced: !this.data.showAdvanced })
  },

  onSaveCloudTap() {
    const cleaned = trimCloudDraft(this.data.cloud)
    const saved = saveCloudConfig(cleaned)
    clearToken()
    resetMonitorTracker()
    const valid = hasValidIamCredentials(saved)
    this.setData({
      cloud: saved,
      cloudConfigured: valid,
    })

    if (valid && this.data.prefs.demoMode) {
      wx.showModal({
        title: '配置已保存',
        content: '当前处于“本地模拟演练模式”，是否立即关闭模拟模式并切换为连接真实硬件？',
        confirmText: '连接真机',
        cancelText: '暂不切换',
        success: (res) => {
          if (res.confirm) {
            const nextPrefs = saveCarePrefs({ demoMode: false })
            this.setData({ prefs: nextPrefs })
          }
        },
      })
      return
    }

    wx.showToast({ title: '云端配置已保存', icon: 'success' })
  },

  onTestCloudTap() {
    if (this.data.testing) {
      return
    }
    const draft = trimCloudDraft(this.data.cloud)
    if (!hasValidIamCredentials(draft)) {
      this.setData({
        testStatus: 'err',
        testTitle: '参数不完整',
        testDetail: '请先填写租户账号名 (iamDomain)、IAM 子用户名 (iamUser) 和密码 (iamPassword)。',
      })
      return
    }

    this.setData({
      testing: true,
      testStatus: 'idle',
      testTitle: '',
      testDetail: '',
    })

    requestToken(draft)
      .then(() => Promise.all([getDeviceStatus(draft), getDeviceData(draft)]))
      .then(([deviceStatus, shadow]) => {
        const snap = buildFallSnapshot(deviceStatus, shadow.props, shadow.eventTime, 'cloud')
        const timeInfo = snap.eventTimeText !== '--' ? snap.eventTimeText : '尚无上报时间'
        this.setData({
          testing: false,
          testStatus: 'ok',
          testTitle: `连接成功 · ${snap.deviceStatusText}`,
          testDetail: `${snap.heroTitle}｜置信度 ${snap.confidencePct}%｜最近上报：${timeInfo}`,
        })
      })
      .catch((err: unknown) => {
        const msg = err instanceof Error ? err.message : '连接测试失败'
        this.setData({
          testing: false,
          testStatus: 'err',
          testTitle: '连接华为云失败',
          testDetail: msg,
        })
      })
  },

  onClearTokenTap() {
    clearToken()
    wx.showToast({ title: '已清除 Token 缓存', icon: 'none' })
  },

  onResetCloudTap() {
    wx.showModal({
      title: '恢复默认设备参数',
      content: '将把实例 ID、设备 ID 和物模型参数恢复为默认华为云配置（需重新填写 IAM 子账号密码）。',
      confirmText: '确认恢复',
      confirmColor: '#e11d48',
      success: (res) => {
        if (res.confirm) {
          const resetCfg = resetCloudConfig()
          clearToken()
          resetMonitorTracker()
          this.setData({
            cloud: resetCfg,
            cloudConfigured: false,
            testStatus: 'idle',
          })
          wx.showToast({ title: '已恢复默认参数', icon: 'success' })
        }
      },
    })
  },
})
