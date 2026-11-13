/** pages/index/index.ts — 居家守护主页逻辑 **/

import { stopAlertCue, triggerAlertCue } from '../../utils/alert'
import { DemoScenario, getDemoScenario, setDemoScenario } from '../../utils/demo'
import {
  dispatchMonitorCommand,
  resetMonitorTracker,
  silenceLocalAlarm,
  syncMonitorSnapshot,
} from '../../utils/monitor'
import { buildFallSnapshot, FallSnapshot } from '../../utils/protocol'
import {
  addHistoryEvent,
  getCarePrefs,
  getCloudConfig,
  hasValidIamCredentials,
  saveCarePrefs,
} from '../../utils/storage'

const INITIAL_SNAPSHOT: FallSnapshot = buildFallSnapshot(
  'UNKNOWN',
  { fall_state: 0, confidence: 0, bbox: '[0,0,0,0]', beep: 0, vision_offline: 0 },
  '',
  'cloud',
)

Page({
  data: {
    elderName: '家中长辈',
    roomLabel: '客厅监护区',
    emergencyName: '紧急联系人',
    emergencyPhone: '',
    autoRefresh: true,
    refreshSec: 3,
    demoMode: false,
    demoScenario: 'normal' as DemoScenario,
    needConfig: false,
    loading: false,
    sending: false,
    localSilenced: false,
    errorMsg: '',
    snapshot: INITIAL_SNAPSHOT,
  },

  pollTimer: null as number | null,
  refreshMs: 3000,

  onLoad() {
    this.syncPrefsToView()
    this.refreshNow()
  },

  onShow() {
    this.syncPrefsToView()
    this.refreshNow()
    this.startPolling()
  },

  onHide() {
    this.stopPolling()
    stopAlertCue()
  },

  onUnload() {
    this.stopPolling()
    stopAlertCue()
  },

  onPullDownRefresh() {
    this.refreshNow().finally(() => {
      wx.stopPullDownRefresh()
    })
  },

  syncPrefsToView() {
    const prefs = getCarePrefs()
    const cfg = getCloudConfig()
    this.refreshMs = prefs.refreshMs
    this.setData({
      elderName: prefs.elderName,
      roomLabel: prefs.roomLabel,
      emergencyName: prefs.emergencyName,
      emergencyPhone: prefs.emergencyPhone,
      autoRefresh: prefs.autoRefresh,
      refreshSec: Math.round(prefs.refreshMs / 1000),
      demoMode: prefs.demoMode,
      demoScenario: getDemoScenario(),
      needConfig: !prefs.demoMode && !hasValidIamCredentials(cfg),
    })
  },

  startPolling() {
    this.stopPolling()
    if (!this.data.autoRefresh) {
      return
    }
    this.pollTimer = setInterval(() => {
      this.refreshNow()
    }, this.refreshMs) as unknown as number
  },

  stopPolling() {
    if (this.pollTimer !== null) {
      clearInterval(this.pollTimer)
      this.pollTimer = null
    }
  },

  async refreshNow() {
    if (this.data.loading) {
      return
    }
    const prefs = getCarePrefs()
    this.setData({ loading: true })

    const outcome = await syncMonitorSnapshot(prefs)
    if (outcome.kind === 'ok' && outcome.snapshot) {
      if (outcome.shouldAlert) {
        triggerAlertCue({
          sound: prefs.soundAlert,
          vibrate: prefs.vibrateAlert,
        })
      } else if (outcome.snapshot.fallState === 0) {
        stopAlertCue()
      }

      this.setData({
        loading: false,
        needConfig: false,
        errorMsg: '',
        snapshot: outcome.snapshot,
        localSilenced: outcome.snapshot.fallState === 1 ? !outcome.shouldAlert : false,
      })
      return
    }

    if (outcome.kind === 'need_config') {
      this.setData({
        loading: false,
        needConfig: true,
        errorMsg: '',
      })
      return
    }

    this.setData({
      loading: false,
      needConfig: false,
      errorMsg: outcome.errorMsg,
    })
  },

  onManualRefresh() {
    this.refreshNow()
  },

  onToggleAutoRefresh(e: WechatMiniprogram.SwitchChange) {
    const autoRefresh = Boolean(e.detail.value)
    saveCarePrefs({ autoRefresh })
    this.setData({ autoRefresh })
    if (autoRefresh) {
      this.startPolling()
    } else {
      this.stopPolling()
    }
  },

  onSilenceLocal() {
    silenceLocalAlarm()
    stopAlertCue()
    this.setData({ localSilenced: true })
    wx.showToast({ title: '已停本机提示音', icon: 'none' })
  },

  onCallEmergency() {
    const prefs = getCarePrefs()
    if (!prefs.emergencyPhone) {
      wx.showModal({
        title: '尚未设置紧急电话',
        content: '是否立即前往“守护设置”填入老人或家属联系电话？',
        confirmText: '去设置',
        success: (res) => {
          if (res.confirm) {
            wx.switchTab({ url: '/pages/settings/settings' })
          }
        },
      })
      return
    }

    addHistoryEvent({
      category: 'action',
      severity: 'info',
      title: `拨打紧急联系人（${prefs.emergencyName}）`,
      detail: `已呼出号码 ${prefs.emergencyPhone}`,
      source: prefs.demoMode ? 'demo' : 'cloud',
    })
    wx.makePhoneCall({
      phoneNumber: prefs.emergencyPhone,
      fail: () => {
        wx.showToast({ title: '无法拉起拨号界面', icon: 'none' })
      },
    })
  },

  onMuteDevice() {
    if (this.data.sending || !this.data.snapshot.online) {
      return
    }
    this.executeCommand('MUTE', '已远程关闭现场蜂鸣器')
  },

  onResetDevice() {
    if (this.data.sending || !this.data.snapshot.online) {
      return
    }
    wx.showModal({
      title: '确认复位监护状态？',
      content: '确认家中情况安全无误后，将通知设备停止报警并回到正常监测。',
      confirmText: '确认复位',
      confirmColor: '#0D9488',
      success: (res) => {
        if (res.confirm) {
          this.executeCommand('RESET', '设备已复位至正常监护')
        }
      },
    })
  },

  async executeCommand(cmd: 'MUTE' | 'RESET', okToast: string) {
    this.setData({ sending: true })
    try {
      await dispatchMonitorCommand(cmd)
      stopAlertCue()
      this.setData({
        sending: false,
        localSilenced: cmd === 'MUTE',
      })
      wx.showToast({ title: okToast, icon: 'success' })
      setTimeout(() => {
        this.refreshNow()
      }, 600)
    } catch (err: unknown) {
      const msg = err instanceof Error ? err.message : String(err)
      this.setData({ sending: false })
      wx.showToast({ title: msg.slice(0, 28), icon: 'none' })
    }
  },

  onEnableDemo() {
    saveCarePrefs({ demoMode: true })
    setDemoScenario('normal')
    resetMonitorTracker()
    this.syncPrefsToView()
    this.refreshNow()
    wx.showToast({ title: '已开启模拟演练', icon: 'success' })
  },

  onExitDemo() {
    saveCarePrefs({ demoMode: false })
    stopAlertCue()
    resetMonitorTracker()
    this.syncPrefsToView()
    this.refreshNow()
    wx.showToast({ title: '已切换回云端模式', icon: 'none' })
  },

  onPickDemoScene(e: WechatMiniprogram.TouchEvent) {
    const scene = String(e.currentTarget.dataset.scene || 'normal') as DemoScenario
    setDemoScenario(scene)
    this.setData({ demoScenario: scene })
    this.refreshNow()
  },

  onGoSettings() {
    wx.switchTab({ url: '/pages/settings/settings' })
  },
})
