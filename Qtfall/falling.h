#ifndef FALLING_H
#define FALLING_H

#include "comm/huawei_iotda_client.h"
#include "comm/win_serial_port.h"
#include "core/event_logger.h"
#include "core/fall_judge_engine.h"
#include "core/fall_simulator.h"
#include "core/sound_alert.h"
#include "protocol/fall_frame_parser.h"

#include <QMainWindow>
#include <QTimer>

QT_BEGIN_NAMESPACE
namespace Ui {
class falling;
}
QT_END_NAMESPACE

class falling : public QMainWindow
{
    Q_OBJECT

public:
    explicit falling(QWidget *parent = nullptr);
    ~falling() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    // 顶部全局控制与菜单
    void onMuteClicked();
    void onResetClicked();
    void onSoundAlertToggled(bool checked);
    void onTestSound();
    void onAbout();

    // 串口控制
    void refreshSerialPorts();
    void onConnectSerialClicked();
    void onSerialBytesReceived(const QByteArray &data);
    void onSendManualSerial();
    void onClearSerialMonitor();

    // 解析与判决回调
    void onFrameParsed(const qtfall::FallRawFrame &frame);
    void onStm32LogParsed(const qtfall::Stm32LogEvent &evt);
    void onJudgeStatusChanged(const qtfall::FallStatusSnapshot &snapshot);
    void onAlarmTriggered(const qtfall::FallStatusSnapshot &snapshot);
    void onAlarmCleared(const qtfall::FallStatusSnapshot &snapshot);
    void onVisionOfflineChanged(bool offline);
    void onCloudReportNeeded(const qtfall::FallStatusSnapshot &snapshot);

    // 华为云 IoTDA
    void onImportAppMqtt();
    void onConnectMqttClicked();
    void onPublishNowClicked();
    void onMqttStateChanged(bool connected, const QString &detail);
    void onCloudCommandReceived(const qtfall::IotdaCommand &cmd);
    void onShadowUpdated(const qtfall::FallStatusSnapshot &snapshot, const QString &eventTime);
    void onNorthboundResult(bool ok, const QString &message);

    // 仿真与参数
    void onStartScenario(qtfall::FallSimulator::Scenario scenario);
    void onStopSimulator();
    void onSimulatorFrame(const QByteArray &rawBytes, const QString &description);
    void onInjectManualFrame();
    void onApplyJudgeParams();
    void onRestoreDefaultParams();

    // 历史记录
    void refreshHistoryTables();
    void onExportCsv();
    void onSaveJson();
    void onClearHistory();

    // 定时器
    void onPeriodicTick();
    void onFpsTick();

private:
    void setupConnections();
    void loadSettings();
    void saveSettings();
    QString historyFilePath() const;
    void appendSystemLog(const QString &msg);
    void appendSerialMonitorText(const QString &prefix, const QByteArray &data);
    void addEventRecord(const QString &source,
                        const QString &eventType,
                        double confidence,
                        const QRect &bbox,
                        const QString &note);
    void updateStatusHeaderUi(const qtfall::FallStatusSnapshot &st);
    void executeControlCommand(bool isReset, const QString &source);
    qtfall::HuaweiCloudConfig readCloudConfigFromUi() const;
    void writeCloudConfigToUi(const qtfall::HuaweiCloudConfig &cfg);

    Ui::falling *ui = nullptr;

    qtfall::WinSerialPort *m_serial = nullptr;
    qtfall::FallFrameParser *m_parser = nullptr;
    qtfall::FallJudgeEngine *m_judge = nullptr;
    qtfall::FallSimulator *m_simulator = nullptr;
    qtfall::HuaweiIotdaClient *m_cloud = nullptr;
    qtfall::EventLogger *m_logger = nullptr;
    qtfall::SoundAlert *m_sound = nullptr;

    QTimer *m_tickTimer = nullptr;
    QTimer *m_fpsTimer = nullptr;
    QTimer *m_shadowPollTimer = nullptr;

    bool m_hasReceivedFrame = false;
    int m_fpsCounter = 0;
    double m_currentFps = 0.0;
    QString m_lastProtocolName = QStringLiteral("-");
    QString m_activeDataSource = QStringLiteral("本地终端");
};

#endif // FALLING_H
