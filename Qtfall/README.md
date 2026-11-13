# Qtfall 老人跌倒检测 Windows 监控终端

配套 **K230 视觉 AI 边缘端** + **STM32F407ZET6 主控主板** + **华为云 IoTDA** 的 PC 端集中监控、算法调试、数据仿真与云端联动终端。

---

## 快速导航
- 📄 **完整使用操作与功能规格文档**：请查阅 [Qtfall_使用操作与功能说明.md](file:///D:/13598/Desktop/falling/Qtfall/Qtfall_%E4%BD%BF%E7%94%A8%E6%93%8D%E4%BD%9C%E4%B8%8E%E5%8A%9F%E8%83%BD%E8%AF%B4%E6%98%8E.md)
- 🛠 **Qt qmake 工程文件**：[Qtfall.pro](file:///D:/13598/Desktop/falling/Qtfall/Qtfall.pro)
- 🎨 **Qt Designer 界面布局**：[falling.ui](file:///D:/13598/Desktop/falling/Qtfall/falling.ui)
- 🧪 **核心单元测试**：[tests/tst_qtfall.pro](file:///D:/13598/Desktop/falling/Qtfall/tests/tst_qtfall.pro) (25 项全通)
- 🖥 **UI 集成冒烟测试**：[tests/tst_ui.pro](file:///D:/13598/Desktop/falling/Qtfall/tests/tst_ui.pro) (5 项全通)

---

## 核心特性

1. **双协议智能流式解析**：自适应解析 K230 Legacy 文本帧 (`$x,y,w,h,State,score#`) 与 Robust 异或校验帧 (`$F,...*XOR\r\n`)，具备粘包/分包处理与坏帧自动隔离。
2. **STM32 日志同步监控**：捕获 `[FALL]`、`[ALARM]`、`[MQTT]` 日志，支持串口下发 `$MUTE#`（消音）与 `$RESET#`（复位）。
3. **640×480 实时视觉大屏**：等比映射包围框（红/黄/蓝/绿识别分类）、质心运动轨迹与离线遮罩。
4. **算法透明透视**：10 格滑动窗口槽位状态指示灯、动态置信度曲线（带阈值参考线）。
5. **对齐下位机核心判决**：10 帧滑窗 6 帧门限、`w > h` 几何过滤、侧卧 (`SideLying`) 可选策略、30s 锁存与 10s 冷却抑制。
6. **Win32 原生免库串口**：采用 Windows SetupAPI / Win32 原生实现，不依赖 `QtSerialPort` 模块，随处即编。
7. **华为云 IoTDA 双通道**：支持 MQTT 3.1.1 设备代报 (OneJSON `FallDetect`) 与北向应用 HTTPS 模式（IAM Token / 影子轮询 / 命令下发，不占设备 ClientId）。
8. **开发文档 §9 全场景仿真**：内置 T1 正常行走、T2 蹲下防抖、T3 快速跌倒、T4 跌倒后起身、T8 离线断连，可把 PC 作为虚拟 K230 从串口给硬件主板发数据。
9. **历史追溯与导出**：自动持久化 `alarm_history.json`，多条件检索过滤，一键导出 UTF-8 BOM 签名 CSV（Excel 打开不乱码）。

---

## 快速上手

### 1. 使用 Qt Creator 打开运行
1. 打开 Qt Creator，打开项目 `Qtfall.pro`。
2. 选择构建套件 `Desktop Qt 6.12.0 MinGW 64-bit`。
3. 点击 **运行 (Ctrl+R)** 即可。

### 2. 命令行多核编译
```powershell
$env:PATH = "D:\Qt\6.12.0\mingw_64\bin;D:\Qt\Tools\mingw1310_64\bin;" + $env:PATH
cd D:\13598\Desktop\falling\Qtfall
mkdir build\cli -Force
cd build\cli
qmake ..\..\Qtfall.pro -spec win32-g++ "CONFIG+=release"
mingw32-make -f Makefile.Release -j4
```

详细操作指南、界面使用手册与 FAQ 请直接阅读同级目录下的 [Qtfall_使用操作与功能说明.md](file:///D:/13598/Desktop/falling/Qtfall/Qtfall_%E4%BD%BF%E7%94%A8%E6%93%8D%E4%BD%9C%E4%B8%8E%E5%8A%9F%E8%83%BD%E8%AF%B4%E6%98%8E.md)。
