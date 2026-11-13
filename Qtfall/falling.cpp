#include "falling.h"
#include "ui_falling.h"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QTableWidgetItem>

using namespace qtfall;

namespace {
QString settingsFilePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/qtfall_settings.ini");
}
} // namespace

falling::falling(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::falling)
    , m_serial(new WinSerialPort(this))
    , m_parser(new FallFrameParser(this))
    , m_judge(new FallJudgeEngine(this))
    , m_simulator(new FallSimulator(this))
    , m_cloud(new HuaweiIotdaClient(this))
    , m_logger(new EventLogger(this))
    , m_sound(new SoundAlert(this))
    , m_tickTimer(new QTimer(this))
    , m_fpsTimer(new QTimer(this))
    , m_shadowPollTimer(new QTimer(this))
{
    ui->setupUi(this);

    ui->txtSerialMonitor->setMaximumBlockCount(1500);
    ui->txtCloudPayload->setMaximumBlockCount(500);
    ui->txtSystemLog->setMaximumBlockCount(1000);
    ui->splitterMonitor->setStretchFactor(0, 5);
    ui->splitterMonitor->setStretchFactor(1, 6);
    ui->tableRecentEvents->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui->tableRecentEvents->horizontalHeader()->setStretchLastSection(true);
    ui->tableHistory->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui->tableHistory->horizontalHeader()->setStretchLastSection(true);

    m_tickTimer->setInterval(200);
    m_fpsTimer->setInterval(1000);
    m_shadowPollTimer->setInterval(5000);

    setupConnections();
    refreshSerialPorts();
    loadSettings();

    m_logger->loadJson(historyFilePath());
    refreshHistoryTables();

    updateStatusHeaderUi(m_judge->status());
    ui->widgetWindowSlots->setWindowState({}, m_judge->config().windowLength, m_judge->config().hitThreshold);
    ui->chartConfidence->setThreshold(m_judge->config().scoreThreshold);

    m_tickTimer->start();
    m_fpsTimer->start();

    appendSystemLog(QStringLiteral("Qtfall 监控终端启动就绪（支持 K230 串口直连 / STM32 日志监听 / 华为云 IoTDA / 离线仿真）"));
}

falling::~falling()
{
    delete ui;
}

void falling::closeEvent(QCloseEvent *event)
{
    m_tickTimer->stop();
    m_fpsTimer->stop();
    m_shadowPollTimer->stop();
    m_simulator->stop();
    m_sound->setAlarmPlaying(false);
    m_cloud->disconnectMqtt();
    m_serial->closePort();
    saveSettings();
    m_logger->saveJson(historyFilePath());
    QMainWindow::closeEvent(event);
}

QString falling::historyFilePath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/alarm_history.json");
}

void falling::setupConnections()
{
    // 顶部控制与菜单
    connect(ui->btnQuickMute, &QPushButton::clicked, this, &falling::onMuteClicked);
    connect(ui->btnQuickReset, &QPushButton::clicked, this, &falling::onResetClicked);
    connect(ui->chkSoundAlert, &QCheckBox::toggled, this, &falling::onSoundAlertToggled);
    connect(ui->actionMute, &QAction::triggered, this, &falling::onMuteClicked);
    connect(ui->actionReset, &QAction::triggered, this, &falling::onResetClicked);
    connect(ui->actionTestSound, &QAction::triggered, this, &falling::onTestSound);
    connect(ui->actionImportAppMqtt, &QAction::triggered, this, &falling::onImportAppMqtt);
    connect(ui->actionExportCsv, &QAction::triggered, this, &falling::onExportCsv);
    connect(ui->actionExit, &QAction::triggered, this, &QWidget::close);
    connect(ui->actionAbout, &QAction::triggered, this, &falling::onAbout);

    // 串口
    connect(ui->btnRefreshPorts, &QPushButton::clicked, this, &falling::refreshSerialPorts);
    connect(ui->btnConnectSerial, &QPushButton::clicked, this, &falling::onConnectSerialClicked);
    connect(ui->btnClearSerialMonitor, &QPushButton::clicked, this, &falling::onClearSerialMonitor);
    connect(ui->btnSendSerialTx, &QPushButton::clicked, this, &falling::onSendManualSerial);
    connect(ui->editSerialTx, &QLineEdit::returnPressed, this, &falling::onSendManualSerial);
    connect(ui->btnTxMute, &QPushButton::clicked, this, &falling::onMuteClicked);
    connect(ui->btnTxReset, &QPushButton::clicked, this, &falling::onResetClicked);
    connect(m_serial, &WinSerialPort::bytesReceived, this, &falling::onSerialBytesReceived);
    connect(m_serial, &WinSerialPort::portOpened, this, [this](const QString &port, quint32 baud) {
        ui->btnConnectSerial->setText(QStringLiteral("关闭串口"));
        ui->lblSerialPill->setText(QStringLiteral("串口: %1 (%2)").arg(port).arg(baud));
        ui->lblSerialPill->setStyleSheet(QStringLiteral("background-color:#064E3B;border:1px solid #10B981;border-radius:12px;padding:4px 10px;color:#ECFDF5;"));
        appendSystemLog(QStringLiteral("串口已打开: %1 @ %2 bps").arg(port).arg(baud));
    });
    connect(m_serial, &WinSerialPort::portClosed, this, [this]() {
        ui->btnConnectSerial->setText(QStringLiteral("打开串口"));
        ui->lblSerialPill->setText(QStringLiteral("串口: 未连接"));
        ui->lblSerialPill->setStyleSheet(QString());
        appendSystemLog(QStringLiteral("串口已关闭"));
    });
    connect(m_serial, &WinSerialPort::errorOccurred, this, [this](const QString &msg) {
        appendSystemLog(QStringLiteral("[串口错误] %1").arg(msg));
        statusBar()->showMessage(msg, 4000);
    });

    // 协议解析与判决引擎
    connect(m_parser, &FallFrameParser::frameParsed, this, &falling::onFrameParsed);
    connect(m_parser, &FallFrameParser::stm32LogParsed, this, &falling::onStm32LogParsed);
    connect(m_parser, &FallFrameParser::frameParseError, this, [this](const QString &frag) {
        appendSystemLog(QStringLiteral("[坏帧丢弃] %1").arg(frag));
    });

    connect(m_judge, &FallJudgeEngine::statusChanged, this, &falling::onJudgeStatusChanged);
    connect(m_judge, &FallJudgeEngine::alarmTriggered, this, &falling::onAlarmTriggered);
    connect(m_judge, &FallJudgeEngine::alarmCleared, this, &falling::onAlarmCleared);
    connect(m_judge, &FallJudgeEngine::visionOfflineChanged, this, &falling::onVisionOfflineChanged);
    connect(m_judge, &FallJudgeEngine::cloudReportNeeded, this, &falling::onCloudReportNeeded);

    // 华为云 IoTDA
    connect(ui->btnImportAppMqtt, &QPushButton::clicked, this, &falling::onImportAppMqtt);
    connect(ui->btnConnectMqtt, &QPushButton::clicked, this, &falling::onConnectMqttClicked);
    connect(ui->btnPublishNow, &QPushButton::clicked, this, &falling::onPublishNowClicked);
    connect(ui->btnFetchToken, &QPushButton::clicked, this, [this]() {
        m_cloud->setConfig(readCloudConfigFromUi());
        m_cloud->fetchIamToken();
    });
    connect(ui->btnQueryShadow, &QPushButton::clicked, this, [this]() {
        m_cloud->setConfig(readCloudConfigFromUi());
        m_cloud->queryDeviceShadow();
    });
    connect(ui->btnNbMute, &QPushButton::clicked, this, [this]() {
        m_cloud->setConfig(readCloudConfigFromUi());
        m_cloud->sendNorthboundCommand(QStringLiteral("MUTE"));
    });
    connect(ui->btnNbReset, &QPushButton::clicked, this, [this]() {
        m_cloud->setConfig(readCloudConfigFromUi());
        m_cloud->sendNorthboundCommand(QStringLiteral("RESET"));
    });
    connect(ui->chkAutoPollShadow, &QCheckBox::toggled, this, [this](bool on) {
        if (on) {
            m_cloud->setConfig(readCloudConfigFromUi());
            m_shadowPollTimer->start();
            m_cloud->queryDeviceShadow();
        } else {
            m_shadowPollTimer->stop();
        }
    });
    connect(m_shadowPollTimer, &QTimer::timeout, this, [this]() {
        m_cloud->setConfig(readCloudConfigFromUi());
        m_cloud->queryDeviceShadow();
    });

    connect(m_cloud, &HuaweiIotdaClient::mqttStateChanged, this, &falling::onMqttStateChanged);
    connect(m_cloud, &HuaweiIotdaClient::mqttMessagePublished, this, [this](const QString &topic, const QByteArray &payload) {
        ui->txtCloudPayload->appendPlainText(
            QStringLiteral("[%1 TX %2]\n%3\n").arg(QDateTime::currentDateTime().toString("HH:mm:ss"), topic, QString::fromUtf8(payload)));
    });
    connect(m_cloud, &HuaweiIotdaClient::cloudCommandReceived, this, &falling::onCloudCommandReceived);
    connect(m_cloud, &HuaweiIotdaClient::shadowUpdated, this, &falling::onShadowUpdated);
    connect(m_cloud, &HuaweiIotdaClient::northboundResult, this, &falling::onNorthboundResult);
    connect(m_cloud, &HuaweiIotdaClient::tokenUpdated, this, [this](const QString &tok) {
        ui->editIamToken->setText(tok);
    });
    connect(m_cloud, &HuaweiIotdaClient::logMessage, this, &falling::appendSystemLog);

    // 仿真测试与判决参数
    connect(ui->btnSimT1, &QPushButton::clicked, this, [this]() { onStartScenario(FallSimulator::Scenario::NormalWalk); });
    connect(ui->btnSimT2, &QPushButton::clicked, this, [this]() { onStartScenario(FallSimulator::Scenario::CrouchDebounce); });
    connect(ui->btnSimT3, &QPushButton::clicked, this, [this]() { onStartScenario(FallSimulator::Scenario::QuickFall); });
    connect(ui->btnSimT4, &QPushButton::clicked, this, [this]() { onStartScenario(FallSimulator::Scenario::FallThenStandUp); });
    connect(ui->btnSimT8, &QPushButton::clicked, this, [this]() { onStartScenario(FallSimulator::Scenario::OfflineDrop); });
    connect(ui->btnSimStop, &QPushButton::clicked, this, &falling::onStopSimulator);
    connect(ui->btnInjectFrame, &QPushButton::clicked, this, &falling::onInjectManualFrame);
    connect(ui->btnApplyParams, &QPushButton::clicked, this, &falling::onApplyJudgeParams);
    connect(ui->btnDefaultParams, &QPushButton::clicked, this, &falling::onRestoreDefaultParams);
    connect(m_simulator, &FallSimulator::frameGenerated, this, &falling::onSimulatorFrame);
    connect(m_simulator, &FallSimulator::scenarioCompleted, this, [this](const QString &summary) {
        appendSystemLog(summary);
        statusBar()->showMessage(summary, 5000);
    });

    // 历史与日志
    connect(ui->editHistorySearch, &QLineEdit::textChanged, this, &falling::refreshHistoryTables);
    connect(ui->comboHistoryFilter, &QComboBox::currentIndexChanged, this, &falling::refreshHistoryTables);
    connect(ui->btnExportCsv, &QPushButton::clicked, this, &falling::onExportCsv);
    connect(ui->btnSaveJson, &QPushButton::clicked, this, &falling::onSaveJson);
    connect(ui->btnClearHistory, &QPushButton::clicked, this, &falling::onClearHistory);

    // 定时器
    connect(m_tickTimer, &QTimer::timeout, this, &falling::onPeriodicTick);
    connect(m_fpsTimer, &QTimer::timeout, this, &falling::onFpsTick);
}

void falling::refreshSerialPorts()
{
    const QString prevPort = ui->comboSerialPort->currentData().toString();
    ui->comboSerialPort->clear();

    const auto ports = WinSerialPort::availablePorts();
    if (ports.isEmpty()) {
        ui->comboSerialPort->addItem(QStringLiteral("(未检测到 COM 串口)"), QString());
        return;
    }

    int restoreIdx = 0;
    for (int i = 0; i < ports.size(); ++i) {
        ui->comboSerialPort->addItem(ports[i].displayLabel(), ports[i].portName);
        if (ports[i].portName == prevPort) {
            restoreIdx = i;
        }
    }
    ui->comboSerialPort->setCurrentIndex(restoreIdx);
}

void falling::onConnectSerialClicked()
{
    if (m_serial->isOpen()) {
        m_serial->closePort();
        return;
    }
    const QString port = ui->comboSerialPort->currentData().toString();
    if (port.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("串口提示"), QStringLiteral("当前未检测到可用串口，请先插好 USB-TTL 并点击刷新。"));
        return;
    }
    const quint32 baud = ui->comboBaudRate->currentText().toUInt();
    m_parser->reset();
    m_serial->openPort(port, baud, 8, 0, 1);
}

void falling::onSerialBytesReceived(const QByteArray &data)
{
    m_activeDataSource = QStringLiteral("串口/%1").arg(m_serial->portName());
    appendSerialMonitorText(QStringLiteral("RX"), data);
    m_parser->feedBytes(data, QDateTime::currentMSecsSinceEpoch());
}

void falling::onSendManualSerial()
{
    if (!m_serial->isOpen()) {
        statusBar()->showMessage(QStringLiteral("请先打开串口再发送数据"), 3000);
        return;
    }
    QByteArray payload = ui->editSerialTx->text().toUtf8();
    if (payload.isEmpty()) {
        return;
    }
    if (ui->chkTxAppendCrLf->isChecked()) {
        payload.append("\r\n");
    }
    if (m_serial->writeBytes(payload) > 0) {
        appendSerialMonitorText(QStringLiteral("TX"), payload);
    }
}

void falling::onClearSerialMonitor()
{
    ui->txtSerialMonitor->clear();
}

void falling::appendSerialMonitorText(const QString &prefix, const QByteArray &data)
{
    if (ui->chkSerialPause->isChecked()) {
        return;
    }
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    QString text;
    if (ui->chkSerialHex->isChecked()) {
        text = QString::fromLatin1(data.toHex(' ').toUpper());
    } else {
        text = QString::fromUtf8(data).trimmed();
        if (text.isEmpty()) return;
    }
    ui->txtSerialMonitor->appendPlainText(QStringLiteral("[%1 %2] %3").arg(ts, prefix, text));
}

void falling::onFrameParsed(const FallRawFrame &frame)
{
    const int role = ui->comboSerialRole->currentIndex();
    m_hasReceivedFrame = true;
    m_fpsCounter++;
    m_lastProtocolName = (frame.protocol == FrameProtocolKind::Robust) ? QStringLiteral("Robust") : QStringLiteral("Legacy");

    if (role == 2 && !m_simulator->isRunning()) {
        // STM32 调试终端模式下仅作可视化显示，不重复运行本地滑窗覆盖板端状态
        FallStatusSnapshot st = m_judge->status();
        st.x = frame.x;
        st.y = frame.y;
        st.w = frame.w;
        st.h = frame.h;
        st.confidence = frame.score;
        st.poseClass = frame.poseClass;
        ui->canvasVision->updateSnapshot(st, true);
        ui->chartConfidence->appendSample(frame.score, frame.isFall);
        return;
    }

    m_judge->processFrame(frame, QDateTime::currentMSecsSinceEpoch());
    const auto cfg = m_judge->config();
    const bool effectiveFall = frame.isFall || (cfg.treatSideLyingAsFall && frame.poseClass == PoseClass::SideLying);
    const bool hit = effectiveFall && frame.score >= cfg.scoreThreshold && (!cfg.geometryCheckEnabled || frame.w > frame.h);
    ui->chartConfidence->appendSample(frame.score, hit);
}

void falling::onStm32LogParsed(const Stm32LogEvent &evt)
{
    appendSystemLog(QStringLiteral("[STM32] %1").arg(evt.rawLine));
    if (!evt.hasStatus) {
        return;
    }

    m_activeDataSource = QStringLiteral("串口/STM32");
    if (evt.hasBbox) {
        m_hasReceivedFrame = true;
    }
    m_judge->applyExternalStatus(evt, QDateTime::currentMSecsSinceEpoch());
    if (evt.confidence >= 0.0) {
        ui->chartConfidence->appendSample(evt.confidence, evt.fallState == 1);
    }
}

void falling::onJudgeStatusChanged(const FallStatusSnapshot &st)
{
    updateStatusHeaderUi(st);

    ui->canvasVision->updateSnapshot(st, m_hasReceivedFrame);
    ui->widgetWindowSlots->setWindowState(st.windowSlots, st.winLen, m_judge->config().hitThreshold);
    ui->lblPoseValue->setText(poseClassLabel(st.poseClass));
    ui->progressConfidence->setValue(qRound(st.confidence * 100.0));
    ui->lblAlarmCountValue->setText(QStringLiteral("%1 次").arg(st.alarmCount));

    if (st.stateHoldRemainingMs > 0) {
        ui->lblHoldValue->setText(QStringLiteral("%1 s").arg(QString::number(st.stateHoldRemainingMs / 1000.0, 'f', 1)));
    } else {
        ui->lblHoldValue->setText(QStringLiteral("--"));
    }

    ui->lblBboxInfo->setText(QStringLiteral("目标框: (%1,%2,%3×%4) | 协议: %5 | 速率: %6 fps")
                                 .arg(st.x)
                                 .arg(st.y)
                                 .arg(st.w)
                                 .arg(st.h)
                                 .arg(m_lastProtocolName)
                                 .arg(QString::number(m_currentFps, 'f', 1)));
    ui->lblFrameStats->setText(QStringLiteral("有效帧: %1 | 错帧: %2")
                                   .arg(m_parser->okFrameCount())
                                   .arg(m_parser->errorFrameCount()));

    m_sound->setAlarmPlaying(st.beepOn);
}

void falling::updateStatusHeaderUi(const FallStatusSnapshot &st)
{
    ui->lblSysBadge->setText(QStringLiteral("● %1").arg(sysStateLabel(st.sysState)));
    if (st.sysState == FallSysState::Alarm) {
        ui->lblSysBadge->setStyleSheet(QStringLiteral("background-color:#991B1B;border:1px solid #EF4444;border-radius:14px;padding:5px 14px;color:#FEF2F2;font-weight:bold;font-size:10.5pt;"));
    } else if (st.sysState == FallSysState::Cooldown) {
        ui->lblSysBadge->setStyleSheet(QStringLiteral("background-color:#92400E;border:1px solid #F59E0B;border-radius:14px;padding:5px 14px;color:#FFFBEB;font-weight:bold;font-size:10.5pt;"));
    } else {
        ui->lblSysBadge->setStyleSheet(QStringLiteral("background-color:#065F46;border:1px solid #10B981;border-radius:14px;padding:5px 14px;color:#ECFDF5;font-weight:bold;font-size:10.5pt;"));
    }

    if (!m_hasReceivedFrame) {
        ui->lblVisionPill->setText(QStringLiteral("K230 视觉: 等待帧"));
        ui->lblVisionPill->setStyleSheet(QString());
    } else if (st.visionOffline) {
        ui->lblVisionPill->setText(QStringLiteral("K230 视觉: 超时掉线"));
        ui->lblVisionPill->setStyleSheet(QStringLiteral("background-color:#7F1D1D;border:1px solid #EF4444;border-radius:12px;padding:4px 10px;color:#FECACA;"));
    } else {
        ui->lblVisionPill->setText(QStringLiteral("K230 视觉: 在线 (%1 fps)").arg(QString::number(m_currentFps, 'f', 1)));
        ui->lblVisionPill->setStyleSheet(QStringLiteral("background-color:#064E3B;border:1px solid #10B981;border-radius:12px;padding:4px 10px;color:#D1FAE5;"));
    }

    if (st.beepOn) {
        ui->lblBeepPill->setText(QStringLiteral("蜂鸣器: 报警鸣响"));
        ui->lblBeepPill->setStyleSheet(QStringLiteral("background-color:#991B1B;border:1px solid #F87171;border-radius:12px;padding:4px 10px;color:#FEE2E2;"));
    } else {
        ui->lblBeepPill->setText(QStringLiteral("蜂鸣器: 静音"));
        ui->lblBeepPill->setStyleSheet(QString());
    }
}

void falling::onAlarmTriggered(const FallStatusSnapshot &st)
{
    const QString msg = QStringLiteral("检测到老人跌倒！命中窗口 %1/%2，置信度 %3")
                            .arg(st.winHits)
                            .arg(st.winLen)
                            .arg(QString::number(st.confidence, 'f', 2));
    appendSystemLog(QStringLiteral("[报警触发] %1").arg(msg));
    addEventRecord(m_activeDataSource,
                   QStringLiteral("跌倒报警"),
                   st.confidence,
                   QRect(st.x, st.y, st.w, st.h),
                   msg);
    statusBar()->showMessage(msg, 5000);
}

void falling::onAlarmCleared(const FallStatusSnapshot &st)
{
    appendSystemLog(QStringLiteral("[报警解除] 已退出报警状态，当前状态：%1").arg(sysStateLabel(st.sysState)));
    addEventRecord(m_activeDataSource,
                   QStringLiteral("报警解除"),
                   st.confidence,
                   QRect(st.x, st.y, st.w, st.h),
                   sysStateLabel(st.sysState));
}

void falling::onVisionOfflineChanged(bool offline)
{
    const auto st = m_judge->status();
    if (offline) {
        appendSystemLog(QStringLiteral("[视觉掉线] 超过 %1 秒未收到 K230 有效帧").arg(m_judge->config().offlineTimeoutMs / 1000));
        addEventRecord(m_activeDataSource, QStringLiteral("视觉掉线"), st.confidence, QRect(st.x, st.y, st.w, st.h), QStringLiteral("K230 心跳超时"));
    } else {
        appendSystemLog(QStringLiteral("[视觉恢复] 重新收到 K230 视觉帧"));
        addEventRecord(m_activeDataSource, QStringLiteral("视觉恢复"), st.confidence, QRect(st.x, st.y, st.w, st.h), QStringLiteral("K230 恢复在线"));
    }
}

void falling::onCloudReportNeeded(const FallStatusSnapshot &st)
{
    if (ui->chkAutoBridgeToCloud->isChecked() && m_cloud->isMqttConnected()) {
        m_cloud->publishProperties(st);
    }
}

void falling::executeControlCommand(bool isReset, const QString &source)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (isReset) {
        m_judge->resetAlarm(now);
    } else {
        m_judge->mute(now);
    }
    m_sound->setAlarmPlaying(false);

    const QByteArray cmd = isReset ? QByteArrayLiteral("$RESET#") : QByteArrayLiteral("$MUTE#");
    if (m_serial->isOpen()) {
        m_serial->writeBytes(cmd);
        appendSerialMonitorText(QStringLiteral("TX"), cmd);
    }

    const auto st = m_judge->status();
    const QString evtName = isReset ? QStringLiteral("系统复位 (RESET)") : QStringLiteral("消音 (MUTE)");
    const QString note = isReset ? QStringLiteral("复位窗口并发送 $RESET#") : QStringLiteral("静音并发送 $MUTE#");
    addEventRecord(source, evtName, st.confidence, QRect(st.x, st.y, st.w, st.h), note);
    appendSystemLog(QStringLiteral("[%1] 已执行%2").arg(source, evtName));
}

void falling::onMuteClicked()
{
    executeControlCommand(false, QStringLiteral("本地控制"));
}

void falling::onResetClicked()
{
    executeControlCommand(true, QStringLiteral("本地控制"));
}

void falling::onSoundAlertToggled(bool checked)
{
    m_sound->setEnabled(checked);
    if (checked && m_judge->status().beepOn) {
        m_sound->setAlarmPlaying(true);
    }
}

void falling::onTestSound()
{
    m_sound->playTestChime();
    statusBar()->showMessage(QStringLiteral("已播放告警测试音"), 2500);
}

void falling::onAbout()
{
    QMessageBox::about(
        this,
        QStringLiteral("关于 Qtfall 监控终端"),
        QStringLiteral("<b>老人跌倒检测 Windows 监控终端</b><br>"
                       "配套 <b>K230 视觉端</b> + <b>STM32F407ZET6</b> + <b>华为云 IoTDA</b>。<br><br>"
                       "支持功能：<br>"
                       "• Legacy ($x,y,w,h,State,score#) 与 Robust ($F,...*XOR) 双协议流式解析<br>"
                       "• 640×480 目标框与运动轨迹实时绘制、滑动窗口 10 格状态指示<br>"
                       "• STM32 调试日志同步解析与本地/串口双向控制 ($MUTE# / $RESET#)<br>"
                       "• 华为云 IoTDA MQTT 3.1.1 设备代报及北向 HTTPS 影子/命令控制<br>"
                       "• 开发文档 §9 T1~T8 场景仿真与 CSV/JSON 记录导出"));
}

void falling::onImportAppMqtt()
{
    const QStringList candidates = {
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../../../falling/usrcode/01_app/Inc/app_mqtt.h")),
        QStringLiteral("D:/13598/Desktop/falling/falling/usrcode/01_app/Inc/app_mqtt.h")
    };
    QString path;
    for (const QString &c : candidates) {
        if (QFileInfo::exists(c)) {
            path = c;
            break;
        }
    }
    if (path.isEmpty()) {
        path = QFileDialog::getOpenFileName(this, QStringLiteral("选择 STM32 app_mqtt.h"), QString(), QStringLiteral("C Header (*.h);;All Files (*)"));
        if (path.isEmpty()) return;
    }

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("导入失败"), QStringLiteral("无法读取文件: %1").arg(path));
        return;
    }

    HuaweiCloudConfig cfg = readCloudConfigFromUi();
    if (HuaweiIotdaClient::parseAppMqttHeader(QString::fromUtf8(f.readAll()), cfg)) {
        writeCloudConfigToUi(cfg);
        m_cloud->setConfig(cfg);
        appendSystemLog(QStringLiteral("已从 %1 导入华为云 IoTDA 参数").arg(path));
        statusBar()->showMessage(QStringLiteral("已导入 app_mqtt.h 云端配置"), 3500);
    } else {
        QMessageBox::warning(this, QStringLiteral("导入失败"), QStringLiteral("未在文件中识别到 MQTT_BROKER_HOST / HUAWEI_DEVICE_ID 宏定义"));
    }
}

void falling::onConnectMqttClicked()
{
    if (m_cloud->isMqttConnected()) {
        m_cloud->disconnectMqtt();
        return;
    }
    const auto cfg = readCloudConfigFromUi();
    m_cloud->setConfig(cfg);
    m_cloud->connectMqtt();
    appendSystemLog(QStringLiteral("正在连接 MQTT Broker %1:%2 ...").arg(cfg.brokerHost).arg(cfg.brokerPort));
}

void falling::onPublishNowClicked()
{
    if (!m_cloud->isMqttConnected()) {
        statusBar()->showMessage(QStringLiteral("MQTT 尚未连接"), 3000);
        return;
    }
    m_cloud->publishProperties(m_judge->status());
}

void falling::onMqttStateChanged(bool connected, const QString &detail)
{
    if (connected) {
        ui->btnConnectMqtt->setText(QStringLiteral("断开 MQTT"));
        ui->lblMqttPill->setText(QStringLiteral("华为云 MQTT: 已连接"));
        ui->lblMqttPill->setStyleSheet(QStringLiteral("background-color:#064E3B;border:1px solid #10B981;border-radius:12px;padding:4px 10px;color:#D1FAE5;"));
    } else {
        ui->btnConnectMqtt->setText(QStringLiteral("连接 MQTT"));
        ui->lblMqttPill->setText(QStringLiteral("华为云 MQTT: 未连接"));
        ui->lblMqttPill->setStyleSheet(QString());
    }
    appendSystemLog(QStringLiteral("[MQTT] %1").arg(detail));
    statusBar()->showMessage(detail, 4000);
}

void falling::onCloudCommandReceived(const IotdaCommand &cmd)
{
    ui->txtCloudPayload->appendPlainText(
        QStringLiteral("[%1 RX CMD %2 (rid=%3)]\n%4\n")
            .arg(QDateTime::currentDateTime().toString("HH:mm:ss"), cmd.commandName, cmd.requestId, cmd.rawPayload));
    appendSystemLog(QStringLiteral("[云端命令] 收到 %1 (request_id=%2)").arg(cmd.commandName, cmd.requestId));

    if (cmd.commandName == QStringLiteral("MUTE")) {
        executeControlCommand(false, QStringLiteral("华为云IoTDA"));
        m_cloud->sendCommandResponse(cmd.requestId, 0, QStringLiteral("muted"));
    } else if (cmd.commandName == QStringLiteral("RESET")) {
        executeControlCommand(true, QStringLiteral("华为云IoTDA"));
        m_cloud->sendCommandResponse(cmd.requestId, 0, QStringLiteral("reset"));
    } else {
        m_cloud->sendCommandResponse(cmd.requestId, 2, QStringLiteral("unknown command"));
    }
}

void falling::onShadowUpdated(const FallStatusSnapshot &snapshot, const QString &eventTime)
{
    ui->lblShadowStatus->setText(QStringLiteral("最新影子: %1 | fall=%2 | conf=%3")
                                     .arg(eventTime.isEmpty() ? QStringLiteral("未知时间") : eventTime)
                                     .arg(snapshot.fallState ? 1 : 0)
                                     .arg(QString::number(snapshot.confidence, 'f', 2)));

    Stm32LogEvent synth;
    synth.hasStatus = true;
    synth.fallState = snapshot.fallState ? 1 : 0;
    synth.beepOn = snapshot.beepOn ? 1 : 0;
    synth.visionOffline = snapshot.visionOffline ? 1 : 0;
    synth.confidence = snapshot.confidence;
    synth.hasBbox = (snapshot.w > 0 && snapshot.h > 0);
    synth.bbox = QRect(snapshot.x, snapshot.y, snapshot.w, snapshot.h);

    m_activeDataSource = QStringLiteral("华为云影子");
    if (synth.hasBbox) m_hasReceivedFrame = true;
    m_judge->applyExternalStatus(synth, QDateTime::currentMSecsSinceEpoch());
    ui->chartConfidence->appendSample(snapshot.confidence, snapshot.fallState);
}

void falling::onNorthboundResult(bool ok, const QString &message)
{
    ui->lblShadowStatus->setText(message);
    appendSystemLog(QStringLiteral("[华为云北向] %1: %2").arg(ok ? QStringLiteral("成功") : QStringLiteral("失败"), message));
    statusBar()->showMessage(message, 4000);
}

void falling::onStartScenario(FallSimulator::Scenario scenario)
{
    const bool robust = (ui->comboSimProtocol->currentIndex() == 1);
    const int fps = ui->spinSimFps->value();
    m_activeDataSource = QStringLiteral("本地仿真");
    m_simulator->start(scenario, robust, fps);
    appendSystemLog(QStringLiteral("启动场景仿真 (协议=%1, %2 fps)").arg(robust ? QStringLiteral("Robust") : QStringLiteral("Legacy")).arg(fps));
}

void falling::onStopSimulator()
{
    m_simulator->stop();
    appendSystemLog(QStringLiteral("已停止场景仿真"));
}

void falling::onSimulatorFrame(const QByteArray &rawBytes, const QString &description)
{
    m_activeDataSource = QStringLiteral("本地仿真");
    if (ui->chkSimSendSerial->isChecked() && m_serial->isOpen()) {
        m_serial->writeBytes(rawBytes);
        appendSerialMonitorText(QStringLiteral("SIM->TX"), rawBytes);
    }
    m_parser->feedBytes(rawBytes, QDateTime::currentMSecsSinceEpoch());
    statusBar()->showMessage(QStringLiteral("仿真: %1 -> %2").arg(description, QString::fromUtf8(rawBytes).trimmed()), 1500);
}

void falling::onInjectManualFrame()
{
    const int x = ui->spinManX->value();
    const int y = ui->spinManY->value();
    const int w = ui->spinManW->value();
    const int h = ui->spinManH->value();
    const double score = ui->dspinManScore->value();
    const int idx = ui->comboManState->currentIndex();
    const bool robust = (ui->comboSimProtocol->currentIndex() == 1) || (idx >= 2);

    QByteArray raw;
    if (robust) {
        int cls = 3;
        int st = 0;
        if (idx == 0) { cls = 0; st = 1; }
        else if (idx == 1) { cls = 3; st = 0; }
        else if (idx == 2) { cls = 1; st = 0; }
        else if (idx == 3) { cls = 2; st = 0; }
        else { cls = 3; st = 0; }
        const int cx = (x + w / 2) * 100 / kK230FrameWidth;
        const int cy = (y + h / 2) * 100 / kK230FrameHeight;
        raw = FallFrameParser::formatRobustFrame(1, st, cls, qRound(score * 100.0), cx, cy, 0);
    } else {
        raw = FallFrameParser::formatLegacyFrame(x, y, w, h, idx == 0, score);
    }

    onSimulatorFrame(raw, QStringLiteral("单帧注入"));
}

void falling::onApplyJudgeParams()
{
    FallJudgeConfig cfg;
    cfg.scoreThreshold = ui->dspinScoreTh->value();
    cfg.windowLength = ui->spinWinLen->value();
    cfg.hitThreshold = qMin(ui->spinHitTh->value(), cfg.windowLength);
    ui->spinHitTh->setValue(cfg.hitThreshold);
    cfg.alarmHoldMs = ui->spinHoldSec->value() * 1000;
    cfg.cooldownMs = ui->spinCooldownSec->value() * 1000;
    cfg.offlineTimeoutMs = ui->spinOfflineSec->value() * 1000;
    cfg.geometryCheckEnabled = ui->chkGeomCheck->isChecked();
    cfg.treatSideLyingAsFall = ui->chkSideLyingAsFall->isChecked();

    m_judge->setConfig(cfg);
    ui->chartConfidence->setThreshold(cfg.scoreThreshold);
    ui->widgetWindowSlots->setWindowState(m_judge->status().windowSlots, cfg.windowLength, cfg.hitThreshold);
    appendSystemLog(QStringLiteral("已应用判决策略: score>=%1, %2/%3 帧, geom(w>h)=%4, sideLying=%5")
                        .arg(QString::number(cfg.scoreThreshold, 'f', 2))
                        .arg(cfg.hitThreshold)
                        .arg(cfg.windowLength)
                        .arg(cfg.geometryCheckEnabled ? QStringLiteral("ON") : QStringLiteral("OFF"))
                        .arg(cfg.treatSideLyingAsFall ? QStringLiteral("ON") : QStringLiteral("OFF")));
}

void falling::onRestoreDefaultParams()
{
    ui->dspinScoreTh->setValue(0.50);
    ui->spinWinLen->setValue(10);
    ui->spinHitTh->setValue(6);
    ui->spinHoldSec->setValue(30);
    ui->spinCooldownSec->setValue(10);
    ui->spinOfflineSec->setValue(5);
    ui->chkGeomCheck->setChecked(true);
    ui->chkSideLyingAsFall->setChecked(false);
    onApplyJudgeParams();
}

void falling::addEventRecord(const QString &source,
                             const QString &eventType,
                             double confidence,
                             const QRect &bbox,
                             const QString &note)
{
    AlarmEventRecord rec;
    rec.timestamp = QDateTime::currentDateTime();
    rec.source = source;
    rec.eventType = eventType;
    rec.confidence = confidence;
    rec.bbox = bbox;
    rec.note = note;
    m_logger->addRecord(rec);
    refreshHistoryTables();
}

void falling::refreshHistoryTables()
{
    const auto all = m_logger->records();

    // 1) 实时大屏最近 25 条事件（倒序）
    ui->tableRecentEvents->setRowCount(0);
    const int recentLimit = qMin(25, all.size());
    for (int i = 0; i < recentLimit; ++i) {
        const auto &r = all.at(all.size() - 1 - i);
        const int row = ui->tableRecentEvents->rowCount();
        ui->tableRecentEvents->insertRow(row);
        ui->tableRecentEvents->setItem(row, 0, new QTableWidgetItem(r.timestamp.toString(QStringLiteral("HH:mm:ss"))));
        ui->tableRecentEvents->setItem(row, 1, new QTableWidgetItem(r.source));
        auto *evtItem = new QTableWidgetItem(r.eventType);
        if (r.eventType.contains(QStringLiteral("跌倒"))) {
            evtItem->setForeground(QColor("#FCA5A5"));
        } else if (r.eventType.contains(QStringLiteral("掉线"))) {
            evtItem->setForeground(QColor("#FCD34D"));
        } else {
            evtItem->setForeground(QColor("#6EE7B7"));
        }
        ui->tableRecentEvents->setItem(row, 2, evtItem);
        ui->tableRecentEvents->setItem(row, 3, new QTableWidgetItem(QString::number(r.confidence, 'f', 2)));
        ui->tableRecentEvents->setItem(row, 4, new QTableWidgetItem(r.note));
    }

    // 2) 完整历史表（按关键字 + 类别过滤）
    const QString keyword = ui->editHistorySearch->text().trimmed();
    const int filterIdx = ui->comboHistoryFilter->currentIndex();
    ui->tableHistory->setRowCount(0);

    for (int i = all.size() - 1; i >= 0; --i) {
        const auto &r = all.at(i);
        if (filterIdx == 1 && !r.eventType.contains(QStringLiteral("跌倒"))) continue;
        if (filterIdx == 2 && !r.eventType.contains(QStringLiteral("消音")) && !r.eventType.contains(QStringLiteral("复位"))) continue;
        if (filterIdx == 3 && !r.eventType.contains(QStringLiteral("掉线")) && !r.eventType.contains(QStringLiteral("恢复"))) continue;

        if (!keyword.isEmpty()) {
            const bool match = r.source.contains(keyword, Qt::CaseInsensitive) ||
                               r.eventType.contains(keyword, Qt::CaseInsensitive) ||
                               r.note.contains(keyword, Qt::CaseInsensitive);
            if (!match) continue;
        }

        const int row = ui->tableHistory->rowCount();
        ui->tableHistory->insertRow(row);
        ui->tableHistory->setItem(row, 0, new QTableWidgetItem(QString::number(r.id)));
        ui->tableHistory->setItem(row, 1, new QTableWidgetItem(r.timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
        ui->tableHistory->setItem(row, 2, new QTableWidgetItem(r.source));
        ui->tableHistory->setItem(row, 3, new QTableWidgetItem(r.eventType));
        ui->tableHistory->setItem(row, 4, new QTableWidgetItem(QString::number(r.confidence, 'f', 2)));
        ui->tableHistory->setItem(row, 5, new QTableWidgetItem(QStringLiteral("[%1,%2,%3,%4]")
                                                                  .arg(r.bbox.x())
                                                                  .arg(r.bbox.y())
                                                                  .arg(r.bbox.width())
                                                                  .arg(r.bbox.height())));
        ui->tableHistory->setItem(row, 6, new QTableWidgetItem(r.note));
    }
}

void falling::onExportCsv()
{
    const QString defaultName = QStringLiteral("fall_alarm_report_%1.csv")
                                    .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出 CSV 报告"), defaultName, QStringLiteral("CSV Files (*.csv)"));
    if (path.isEmpty()) return;

    if (m_logger->exportCsv(path)) {
        statusBar()->showMessage(QStringLiteral("已导出 CSV: %1").arg(path), 4000);
    } else {
        QMessageBox::warning(this, QStringLiteral("导出失败"), QStringLiteral("无法写入文件: %1").arg(path));
    }
}

void falling::onSaveJson()
{
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("保存 JSON 记录"), QStringLiteral("alarm_history.json"), QStringLiteral("JSON Files (*.json)"));
    if (path.isEmpty()) return;

    if (m_logger->saveJson(path)) {
        statusBar()->showMessage(QStringLiteral("已保存 JSON: %1").arg(path), 4000);
    } else {
        QMessageBox::warning(this, QStringLiteral("保存失败"), QStringLiteral("无法写入文件: %1").arg(path));
    }
}

void falling::onClearHistory()
{
    m_logger->clear();
    m_logger->saveJson(historyFilePath());
    refreshHistoryTables();
    appendSystemLog(QStringLiteral("已清空全部报警历史记录"));
}

void falling::onPeriodicTick()
{
    m_judge->onTick(QDateTime::currentMSecsSinceEpoch());
    const auto st = m_judge->status();
    if (st.stateHoldRemainingMs > 0) {
        ui->lblHoldValue->setText(QStringLiteral("%1 s").arg(QString::number(st.stateHoldRemainingMs / 1000.0, 'f', 1)));
    } else {
        ui->lblHoldValue->setText(QStringLiteral("--"));
    }
}

void falling::onFpsTick()
{
    m_currentFps = static_cast<double>(m_fpsCounter);
    m_fpsCounter = 0;

    ui->lblSerialStats->setText(QStringLiteral("RX: %1 B | TX: %2 B")
                                    .arg(m_serial->rxBytes())
                                    .arg(m_serial->txBytes()));

    const auto st = m_judge->status();
    updateStatusHeaderUi(st);
    ui->lblBboxInfo->setText(QStringLiteral("目标框: (%1,%2,%3×%4) | 协议: %5 | 速率: %6 fps")
                                 .arg(st.x)
                                 .arg(st.y)
                                 .arg(st.w)
                                 .arg(st.h)
                                 .arg(m_lastProtocolName)
                                 .arg(QString::number(m_currentFps, 'f', 1)));
}

void falling::appendSystemLog(const QString &msg)
{
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    ui->txtSystemLog->appendPlainText(QStringLiteral("[%1] %2").arg(ts, msg));
}

HuaweiCloudConfig falling::readCloudConfigFromUi() const
{
    HuaweiCloudConfig cfg;
    cfg.brokerHost = ui->editMqttHost->text().trimmed();
    cfg.brokerPort = static_cast<quint16>(ui->spinMqttPort->value());
    cfg.deviceId = ui->editMqttDeviceId->text().trimmed();
    cfg.serviceId = ui->editMqttServiceId->text().trimmed();
    cfg.clientId = ui->editMqttClientId->text().trimmed();
    cfg.username = ui->editMqttUser->text().trimmed();
    cfg.password = ui->editMqttPass->text();
    cfg.iamEndpoint = ui->editIamEndpoint->text().trimmed();
    cfg.iotdaAppEndpoint = ui->editAppEndpoint->text().trimmed();
    cfg.projectId = ui->editProjectId->text().trimmed();
    cfg.iamDomain = ui->editIamDomain->text().trimmed();
    cfg.iamUser = ui->editIamUser->text().trimmed();
    cfg.iamPassword = ui->editIamPass->text();
    cfg.iamToken = ui->editIamToken->text().trimmed();
    return cfg;
}

void falling::writeCloudConfigToUi(const HuaweiCloudConfig &cfg)
{
    ui->editMqttHost->setText(cfg.brokerHost);
    ui->spinMqttPort->setValue(cfg.brokerPort);
    ui->editMqttDeviceId->setText(cfg.deviceId);
    ui->editMqttServiceId->setText(cfg.serviceId);
    ui->editMqttClientId->setText(cfg.clientId);
    ui->editMqttUser->setText(cfg.username);
    ui->editMqttPass->setText(cfg.password);
    ui->editIamEndpoint->setText(cfg.iamEndpoint);
    ui->editAppEndpoint->setText(cfg.iotdaAppEndpoint);
    ui->editProjectId->setText(cfg.projectId);
    ui->editIamDomain->setText(cfg.iamDomain);
    ui->editIamUser->setText(cfg.iamUser);
    ui->editIamPass->setText(cfg.iamPassword);
    ui->editIamToken->setText(cfg.iamToken);
}

void falling::loadSettings()
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    ui->comboBaudRate->setCurrentText(s.value(QStringLiteral("serial/baud"), QStringLiteral("115200")).toString());
    ui->comboSerialRole->setCurrentIndex(s.value(QStringLiteral("serial/role"), 0).toInt());
    ui->chkAutoBridgeToCloud->setChecked(s.value(QStringLiteral("serial/bridgeCloud"), false).toBool());
    ui->chkSoundAlert->setChecked(s.value(QStringLiteral("ui/soundAlert"), true).toBool());
    m_sound->setEnabled(ui->chkSoundAlert->isChecked());

    HuaweiCloudConfig cfg;
    cfg.brokerHost = s.value(QStringLiteral("cloud/brokerHost"), cfg.brokerHost).toString();
    cfg.brokerPort = static_cast<quint16>(s.value(QStringLiteral("cloud/brokerPort"), cfg.brokerPort).toUInt());
    cfg.deviceId = s.value(QStringLiteral("cloud/deviceId"), cfg.deviceId).toString();
    cfg.serviceId = s.value(QStringLiteral("cloud/serviceId"), cfg.serviceId).toString();
    cfg.clientId = s.value(QStringLiteral("cloud/clientId")).toString();
    cfg.username = s.value(QStringLiteral("cloud/username")).toString();
    cfg.password = s.value(QStringLiteral("cloud/password")).toString();
    cfg.iamEndpoint = s.value(QStringLiteral("cloud/iamEndpoint"), cfg.iamEndpoint).toString();
    cfg.iotdaAppEndpoint = s.value(QStringLiteral("cloud/appEndpoint")).toString();
    cfg.projectId = s.value(QStringLiteral("cloud/projectId")).toString();
    cfg.iamDomain = s.value(QStringLiteral("cloud/iamDomain")).toString();
    cfg.iamUser = s.value(QStringLiteral("cloud/iamUser")).toString();
    writeCloudConfigToUi(cfg);
    m_cloud->setConfig(cfg);

    ui->dspinScoreTh->setValue(s.value(QStringLiteral("judge/scoreTh"), 0.50).toDouble());
    ui->spinWinLen->setValue(s.value(QStringLiteral("judge/winLen"), 10).toInt());
    ui->spinHitTh->setValue(s.value(QStringLiteral("judge/hitTh"), 6).toInt());
    ui->spinHoldSec->setValue(s.value(QStringLiteral("judge/holdSec"), 30).toInt());
    ui->spinCooldownSec->setValue(s.value(QStringLiteral("judge/cooldownSec"), 10).toInt());
    ui->spinOfflineSec->setValue(s.value(QStringLiteral("judge/offlineSec"), 5).toInt());
    ui->chkGeomCheck->setChecked(s.value(QStringLiteral("judge/geomCheck"), true).toBool());
    ui->chkSideLyingAsFall->setChecked(s.value(QStringLiteral("judge/sideLying"), false).toBool());
    onApplyJudgeParams();
}

void falling::saveSettings()
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("serial/baud"), ui->comboBaudRate->currentText());
    s.setValue(QStringLiteral("serial/role"), ui->comboSerialRole->currentIndex());
    s.setValue(QStringLiteral("serial/bridgeCloud"), ui->chkAutoBridgeToCloud->isChecked());
    s.setValue(QStringLiteral("ui/soundAlert"), ui->chkSoundAlert->isChecked());

    const auto cfg = readCloudConfigFromUi();
    s.setValue(QStringLiteral("cloud/brokerHost"), cfg.brokerHost);
    s.setValue(QStringLiteral("cloud/brokerPort"), cfg.brokerPort);
    s.setValue(QStringLiteral("cloud/deviceId"), cfg.deviceId);
    s.setValue(QStringLiteral("cloud/serviceId"), cfg.serviceId);
    s.setValue(QStringLiteral("cloud/clientId"), cfg.clientId);
    s.setValue(QStringLiteral("cloud/username"), cfg.username);
    s.setValue(QStringLiteral("cloud/password"), cfg.password);
    s.setValue(QStringLiteral("cloud/iamEndpoint"), cfg.iamEndpoint);
    s.setValue(QStringLiteral("cloud/appEndpoint"), cfg.iotdaAppEndpoint);
    s.setValue(QStringLiteral("cloud/projectId"), cfg.projectId);
    s.setValue(QStringLiteral("cloud/iamDomain"), cfg.iamDomain);
    s.setValue(QStringLiteral("cloud/iamUser"), cfg.iamUser);

    s.setValue(QStringLiteral("judge/scoreTh"), ui->dspinScoreTh->value());
    s.setValue(QStringLiteral("judge/winLen"), ui->spinWinLen->value());
    s.setValue(QStringLiteral("judge/hitTh"), ui->spinHitTh->value());
    s.setValue(QStringLiteral("judge/holdSec"), ui->spinHoldSec->value());
    s.setValue(QStringLiteral("judge/cooldownSec"), ui->spinCooldownSec->value());
    s.setValue(QStringLiteral("judge/offlineSec"), ui->spinOfflineSec->value());
    s.setValue(QStringLiteral("judge/geomCheck"), ui->chkGeomCheck->isChecked());
    s.setValue(QStringLiteral("judge/sideLying"), ui->chkSideLyingAsFall->isChecked());
}
