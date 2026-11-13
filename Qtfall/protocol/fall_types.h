#ifndef FALL_TYPES_H
#define FALL_TYPES_H

#include <QDateTime>
#include <QRect>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace qtfall {

inline constexpr int kK230FrameWidth = 640;
inline constexpr int kK230FrameHeight = 480;

enum class FrameProtocolKind {
    Unknown = 0,
    Legacy,
    Robust,
    Stm32Log
};

enum class PoseClass : int {
    NoTarget = -1,
    Fall = 0,
    SideLying = 1,
    Sitting = 2,
    Standing = 3,
    NoFall = 10
};

inline QString poseClassLabel(PoseClass cls)
{
    switch (cls) {
    case PoseClass::Fall:
        return QStringLiteral("Fall (跌倒)");
    case PoseClass::SideLying:
        return QStringLiteral("Side-Lying (侧卧)");
    case PoseClass::Sitting:
        return QStringLiteral("Sitting (坐姿)");
    case PoseClass::Standing:
        return QStringLiteral("Standing (站立)");
    case PoseClass::NoFall:
        return QStringLiteral("NoFall (正常)");
    case PoseClass::NoTarget:
    default:
        return QStringLiteral("NoTarget (无目标)");
    }
}

struct FallRawFrame {
    FrameProtocolKind protocol = FrameProtocolKind::Unknown;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    bool isFall = false;
    PoseClass poseClass = PoseClass::NoTarget;
    double score = 0.0;
    int evt = 0;
    int cx = -1;
    int cy = -1;
    int seq = 0;
    qint64 timestampMs = 0;
    QString rawText;
};

struct Stm32LogEvent {
    enum class Type {
        Info = 0,
        AlarmTriggered,
        AlarmHoldExpired,
        CooldownFinished,
        Muted,
        Reset,
        VisionOffline,
        BeepChanged,
        MqttConnected,
        MqttPublished,
        MqttCommand
    };

    Type type = Type::Info;
    bool hasStatus = false;
    int fallState = -1;
    int beepOn = -1;
    int visionOffline = -1;
    double confidence = -1.0;
    QRect bbox;
    bool hasBbox = false;
    int winHits = -1;
    QString commandName;
    QString requestId;
    QString rawLine;
};

enum class FallSysState : int {
    Idle = 0,
    Alarm = 1,
    Cooldown = 2
};

inline QString sysStateLabel(FallSysState st)
{
    switch (st) {
    case FallSysState::Alarm:
        return QStringLiteral("跌倒报警中 (ALARM)");
    case FallSysState::Cooldown:
        return QStringLiteral("报警冷却期 (COOLDOWN)");
    case FallSysState::Idle:
    default:
        return QStringLiteral("正常监测中 (IDLE)");
    }
}

struct FallJudgeConfig {
    double scoreThreshold = 0.50;
    int windowLength = 10;
    int hitThreshold = 6;
    bool geometryCheckEnabled = true;
    bool treatSideLyingAsFall = false;
    int alarmHoldMs = 30000;
    int cooldownMs = 10000;
    int offlineTimeoutMs = 5000;
};

struct FallStatusSnapshot {
    FallSysState sysState = FallSysState::Idle;
    bool fallState = false;
    bool beepOn = false;
    bool visionOffline = false;
    int winHits = 0;
    int winCount = 0;
    int winLen = 10;
    QVector<bool> windowSlots;
    double confidence = 0.0;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    PoseClass poseClass = PoseClass::NoTarget;
    quint32 alarmCount = 0;
    qint64 stateHoldRemainingMs = 0;
};

struct AlarmEventRecord {
    qint64 id = 0;
    QDateTime timestamp;
    QString source;
    QString eventType;
    double confidence = 0.0;
    QRect bbox;
    QString note;
};

struct HuaweiCloudConfig {
    QString brokerHost = QStringLiteral("f37725de82.st1.iotda-device.cn-north-4.myhuaweicloud.com");
    quint16 brokerPort = 1883;
    QString clientId;
    QString username;
    QString password;
    QString deviceId = QStringLiteral("6af6a443cbb0cf6bb9970fe2_fall_stm32_dev01");
    QString serviceId = QStringLiteral("FallDetect");
    int keepAliveSec = 30;

    // 北向 REST API（用于不互踢 STM32 ClientId 的远程影子查询和命令下发）
    QString iamEndpoint = QStringLiteral("https://iam.cn-north-4.myhuaweicloud.com");
    QString iotdaAppEndpoint;
    QString projectId;
    QString iamDomain;
    QString iamUser;
    QString iamPassword;
    QString iamToken;
};

} // namespace qtfall

#endif // FALL_TYPES_H
