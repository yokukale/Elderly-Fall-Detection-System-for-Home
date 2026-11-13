// app.ts
App<IAppOption>({
  globalData: {},
  onLaunch() {
    // 保留本地守护配置与告警历史，不做无用启动请求
  },
})
