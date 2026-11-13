/**
 * 本地模拟演练状态机：在未连接真机或未配置 IAM 子账号时，完整模拟 FallDetect 上报与 MUTE/RESET 响应
 */

import { FallCommandName } from './iotda'

export type DemoScenario = 'normal' | 'fall' | 'vision_offline' | 'device_offline'

interface DemoInternalState {
  scenario: DemoScenario
  beep: number
  eventTime: string
}

function nowUtcCompact(): string {
  const d = new Date()
  const p = (n: number): string => (n < 10 ? '0' + n : String(n))
  return (
    d.getUTCFullYear().toString() +
    p(d.getUTCMonth() + 1) +
    p(d.getUTCDate()) +
    'T' +
    p(d.getUTCHours()) +
    p(d.getUTCMinutes()) +
    p(d.getUTCSeconds()) +
    'Z'
  )
}

const demoState: DemoInternalState = {
  scenario: 'normal',
  beep: 0,
  eventTime: nowUtcCompact(),
}

export function setDemoScenario(scenario: DemoScenario): void {
  demoState.scenario = scenario
  demoState.beep = scenario === 'fall' ? 1 : 0
  demoState.eventTime = nowUtcCompact()
}

export function getDemoScenario(): DemoScenario {
  return demoState.scenario
}

export function applyDemoCommand(commandName: FallCommandName): void {
  if (commandName === 'MUTE') {
    demoState.beep = 0
  } else if (commandName === 'RESET') {
    demoState.scenario = 'normal'
    demoState.beep = 0
  }
  demoState.eventTime = nowUtcCompact()
}

export function readDemoPayload(): {
  deviceStatus: string
  props: Record<string, unknown>
  eventTime: string
} {
  if (demoState.scenario === 'fall') {
    demoState.eventTime = nowUtcCompact()
    return {
      deviceStatus: 'ONLINE',
      props: {
        fall_state: 1,
        confidence: 0.89,
        bbox: '[116,184,336,194]',
        beep: demoState.beep,
        vision_offline: 0,
      },
      eventTime: demoState.eventTime,
    }
  }

  if (demoState.scenario === 'vision_offline') {
    demoState.eventTime = nowUtcCompact()
    return {
      deviceStatus: 'ONLINE',
      props: {
        fall_state: 0,
        confidence: 0.0,
        bbox: '[0,0,0,0]',
        beep: 0,
        vision_offline: 1,
      },
      eventTime: demoState.eventTime,
    }
  }

  if (demoState.scenario === 'device_offline') {
    return {
      deviceStatus: 'OFFLINE',
      props: {
        fall_state: 0,
        confidence: 0.0,
        bbox: '[0,0,0,0]',
        beep: 0,
        vision_offline: 0,
      },
      eventTime: demoState.eventTime,
    }
  }

  demoState.eventTime = nowUtcCompact()
  return {
    deviceStatus: 'ONLINE',
    props: {
      fall_state: 0,
      confidence: 0.12,
      bbox: '[0,0,0,0]',
      beep: 0,
      vision_offline: 0,
    },
    eventTime: demoState.eventTime,
  }
}
