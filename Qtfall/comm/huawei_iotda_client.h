#ifndef HUAWEI_IOTDA_CLIENT_H
#define HUAWEI_IOTDA_CLIENT_H

#include "fall_types.h"
#include "mqtt_client.h"
#include <QNetworkAccessManager>
#include <QObject>

namespace qtfall {

struct IotdaCommand {
    QString requestId;
    QString serviceId;
    QString commandName;
    QString rawPayload;
};

class HuaweiIotdaClient : public QObject
{
    Q_OBJECT
public:
    explicit HuaweiIotdaClient(QObject *parent = nullptr);

    void setConfig(const HuaweiCloudConfig &cfg);
    HuaweiCloudConfig config() const { return m_cfg; }

    // MQTT 设备级通道（用于 PC 代报华为云或连接任意兼容 Broker）
    void connectMqtt();
    void disconnectMqtt();
    bool isMqttConnected() const;
    void publishProperties(const FallStatusSnapshot &st);
    void sendCommandResponse(const QString &requestId, int resultCode, const QString &resultText);

    // 北向 HTTPS REST API（用于不踢掉 STM32 的云端影子拉取与 MUTE/RESET 下发）
    void fetchIamToken();
    void queryDeviceShadow();
    void sendNorthboundCommand(const QString &commandName);

    // 纯工具方法（可直接单测）
    static QString buildPropertyReportTopic(const QString &deviceId);
    static QString buildCommandSubTopic(const QString &deviceId);
    static QString buildCommandRespTopic(const QString &deviceId, const QString &requestId);
    static QByteArray buildPropertyReportPayload(const QString &serviceId, const FallStatusSnapshot &st);
    static QByteArray buildCommandResponsePayload(int resultCode, const QString &resultText);
    static bool parseCommandMessage(const QString &topic, const QByteArray &payload, IotdaCommand &out);
    static bool parseShadowResponse(const QByteArray &jsonBytes, FallStatusSnapshot &outSnapshot, QString &eventTimeOut);
    static bool parseAppMqttHeader(const QString &headerText, HuaweiCloudConfig &inOutCfg);

signals:
    void mqttStateChanged(bool connected, const QString &detail);
    void mqttMessagePublished(const QString &topic, const QByteArray &payload);
    void cloudCommandReceived(const qtfall::IotdaCommand &cmd);
    void shadowUpdated(const qtfall::FallStatusSnapshot &snapshot, const QString &eventTime);
    void northboundResult(bool ok, const QString &message);
    void tokenUpdated(const QString &token);
    void logMessage(const QString &msg);

private:
    HuaweiCloudConfig m_cfg;
    MqttClient *m_mqtt = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
};

} // namespace qtfall

#endif // HUAWEI_IOTDA_CLIENT_H
