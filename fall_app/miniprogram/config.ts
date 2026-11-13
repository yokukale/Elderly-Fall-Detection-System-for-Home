/**
 * 华为云 IoTDA 接入配置与家人监护默认偏好
 *
 * 注：已预填当前 FallDetector（STM32 实例，华北-北京四 cn-north-4）已知参数；
 * 如已在华为云创建具备 IoTDA 权限的 IAM 子用户，可直接填写在这里或运行后在小程序“设置”页填写。
 */

export interface CloudConfig {
  iamDomain: string
  iamUser: string
  iamPassword: string
  projectId: string
  instanceId: string
  endpoint: string
  deviceId: string
  serviceId: string
  region: string
}

export interface CarePreferences {
  elderName: string
  roomLabel: string
  emergencyName: string
  emergencyPhone: string
  refreshMs: number
  autoRefresh: boolean
  soundAlert: boolean
  vibrateAlert: boolean
  demoMode: boolean
}

export const DEFAULT_CLOUD_CONFIG: CloudConfig = {
  iamDomain: 'hid_4cmd1am6hdsyxmj',
  iamUser: '',
  iamPassword: '',
  projectId: '01a06a5080727b108c476ff6b15543dc',
  instanceId: '29dfb402-485b-4907-b302-af761b8393a3',
  endpoint: 'https://f37725de82.st1.iotda-app.cn-north-4.myhuaweicloud.com',
  deviceId: '6af6a443cbb0cf6bb9970fe2_fall_stm32_dev01',
  serviceId: 'FallDetect',
  region: 'cn-north-4',
}

export const DEFAULT_CARE_PREFS: CarePreferences = {
  elderName: '家中长辈',
  roomLabel: '客厅监护区',
  emergencyName: '紧急联系人',
  emergencyPhone: '',
  refreshMs: 3000,
  autoRefresh: true,
  soundAlert: true,
  vibrateAlert: true,
  demoMode: false,
}
