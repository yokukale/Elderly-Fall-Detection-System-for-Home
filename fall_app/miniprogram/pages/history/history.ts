/** pages/history/history.ts — 告警与守护历史记录逻辑 **/

import { resetMonitorTracker, syncMonitorSnapshot } from '../../utils/monitor'
import { formatRelativeTime } from '../../utils/protocol'
import {
  clearHistoryList,
  FallEpisodeStatus,
  getCarePrefs,
  getCloudConfig,
  getHistoryList,
  hasValidIamCredentials,
  HistoryCategory,
  HistoryEvent,
} from '../../utils/storage'

type FilterKey = 'all' | HistoryCategory

interface FilterTabItem {
  key: FilterKey
  label: string
  count: number
}

interface HistoryViewItem extends HistoryEvent {
  relativeText: string
  episodeLabel: string
  episodeClass: string
  sourceLabel: string
  sourceClass: string
}

function episodeBadge(status?: FallEpisodeStatus): { label: string; cls: string } {
  if (status === 'active') {
    return { label: '报警中', cls: 'pill-danger' }
  }
  if (status === 'muted') {
    return { label: '已消音', cls: 'pill-warn' }
  }
  if (status === 'resolved') {
    return { label: '已解除', cls: 'pill-safe' }
  }
  return { label: '', cls: '' }
}

function sourceBadge(source: HistoryEvent['source']): { label: string; cls: string } {
  if (source === 'demo') {
    return { label: '演练模拟', cls: 'pill-info' }
  }
  return { label: '云端同步', cls: 'pill-safe' }
}

function isSameLocalDay(tsMs: number, now: Date): boolean {
  const d = new Date(tsMs)
  return (
    d.getFullYear() === now.getFullYear() &&
    d.getMonth() === now.getMonth() &&
    d.getDate() === now.getDate()
  )
}

function emptyCopyOf(filter: FilterKey): { title: string; desc: string } {
  if (filter === 'fall') {
    return {
      title: '暂无跌倒告警记录',
      desc: '长辈日常安全平稳，或在首页“模拟演练”中体验告警联动流程',
    }
  }
  if (filter === 'sensor') {
    return {
      title: '暂无设备与视觉变更记录',
      desc: '设备上线、离线及 K230 视觉断连事件会自动记录于此',
    }
  }
  if (filter === 'action') {
    return {
      title: '暂无家人处置记录',
      desc: '远程消音、解除告警、拨打紧急联系人和本机静音操作均会留痕',
    }
  }
  return {
    title: '暂无守护事件记录',
    desc: '当发生跌倒告警、设备离线或家人远程控制时，系统会自动保存时间轴',
  }
}

Page({
  data: {
    elderName: '家中长辈',
    roomLabel: '客厅监护区',
    activeFilter: 'all' as FilterKey,
    totalCount: 0,
    todayFallCount: 0,
    activeFallCount: 0,
    filterTabs: [
      { key: 'all', label: '全部', count: 0 },
      { key: 'fall', label: '跌倒', count: 0 },
      { key: 'sensor', label: '设备', count: 0 },
      { key: 'action', label: '操作', count: 0 },
    ] as FilterTabItem[],
    visibleItems: [] as HistoryViewItem[],
    emptyTitle: '暂无守护事件记录',
    emptyDesc: '当发生跌倒告警、设备离线或家人远程控制时，系统会自动保存时间轴',
  },

  onShow() {
    this.reloadHistory()
  },

  onPullDownRefresh() {
    const prefs = getCarePrefs()
    const cfg = getCloudConfig()
    if (!prefs.demoMode && !hasValidIamCredentials(cfg)) {
      this.reloadHistory()
      wx.stopPullDownRefresh()
      return
    }
    syncMonitorSnapshot(prefs, cfg)
      .catch(() => undefined)
      .finally(() => {
        this.reloadHistory()
        wx.stopPullDownRefresh()
      })
  },

  reloadHistory() {
    const prefs = getCarePrefs()
    const list = getHistoryList()
    const now = new Date()
    const activeFilter = this.data.activeFilter

    let todayFallCount = 0
    let activeFallCount = 0
    let fallCount = 0
    let sensorCount = 0
    let actionCount = 0

    for (let i = 0; i < list.length; i++) {
      const item = list[i]
      if (item.category === 'fall') {
        fallCount++
        if (isSameLocalDay(item.timestampMs, now)) {
          todayFallCount++
        }
        if (item.episodeStatus === 'active') {
          activeFallCount++
        }
      } else if (item.category === 'sensor') {
        sensorCount++
      } else if (item.category === 'action') {
        actionCount++
      }
    }

    const filtered = activeFilter === 'all' ? list : list.filter((it) => it.category === activeFilter)
    const visibleItems: HistoryViewItem[] = filtered.map((it) => {
      const ep = episodeBadge(it.episodeStatus)
      const src = sourceBadge(it.source)
      return {
        ...it,
        relativeText: formatRelativeTime(new Date(it.timestampMs)),
        episodeLabel: ep.label,
        episodeClass: ep.cls,
        sourceLabel: src.label,
        sourceClass: src.cls,
      }
    })

    const copy = emptyCopyOf(activeFilter)
    this.setData({
      elderName: prefs.elderName,
      roomLabel: prefs.roomLabel,
      totalCount: list.length,
      todayFallCount,
      activeFallCount,
      filterTabs: [
        { key: 'all', label: '全部', count: list.length },
        { key: 'fall', label: '跌倒', count: fallCount },
        { key: 'sensor', label: '设备', count: sensorCount },
        { key: 'action', label: '操作', count: actionCount },
      ],
      visibleItems,
      emptyTitle: copy.title,
      emptyDesc: copy.desc,
    })
  },

  onFilterTap(e: WechatMiniprogram.TouchEvent) {
    const key = e.currentTarget.dataset.key as FilterKey | undefined
    if (!key || key === this.data.activeFilter) {
      return
    }
    this.setData({ activeFilter: key }, () => {
      this.reloadHistory()
    })
  },

  onResetFilterTap() {
    this.setData({ activeFilter: 'all' }, () => {
      this.reloadHistory()
    })
  },

  onClearTap() {
    if (this.data.totalCount === 0) {
      return
    }
    wx.showModal({
      title: '清空守护记录',
      content: '确认删除本机保存的所有跌倒告警与设备操作时间轴吗？删除后无法恢复。',
      confirmText: '确认清空',
      confirmColor: '#e11d48',
      success: (res) => {
        if (res.confirm) {
          clearHistoryList()
          resetMonitorTracker()
          this.reloadHistory()
          wx.showToast({ title: '已清空记录', icon: 'success' })
        }
      },
    })
  },
})
