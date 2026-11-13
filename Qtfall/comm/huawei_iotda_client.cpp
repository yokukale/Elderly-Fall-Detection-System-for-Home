#include "huawei_iotda_client.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <cmath>

namespace qtfall {

HuaweiIotdaClient::HuaweiIotdaClient(QObject *parent)
    : QObject(parent)
    , m_mqtt(new MqttClient(this))
    , m_nam(new QNetworkAccessManager(this))
{
    connect(m_mqtt, &MqttClient::connected, this, [this]() {
        const QString subTopic = buildCommandSubTopic(m_cfg.deviceId);
        m_mqtt->subscribe(subTopic, 1);
        emit mqttStateChanged(true, QStringLiteral("已连接华为云/MQTT Broker，已订阅 %1").arg(subTopic));
    });

    connect(m_mqtt, &MqttClient::disconnected, this, [this](const QString &reason) {
        emit mqttStateChanged(false, reason);
    });

    connect(m_mqtt, &MqttClient::errorOccurred, this, [this](const QString &msg) {
        emit mqttStateChanged(false, msg);
        emit logMessage(QStringLiteral("[MQTT错误] %1").arg(msg));
    });

    connect(m_mqtt, &MqttClient::messageReceived, this, [this](const QString &topic, const QByteArray &payload) {
        IotdaCommand cmd;
        if (parseCommandMessage(topic, payload, cmd)) {
            emit cloudCommandReceived(cmd);
        } else {
            emit logMessage(QStringLiteral("[MQTT下行] %1 -> %2").arg(topic, QString::fromUtf8(payload)));
        }
    });
}

void HuaweiIotdaClient::setConfig(const HuaweiCloudConfig &cfg)
{
    m_cfg = cfg;
}

void HuaweiIotdaClient::connectMqtt()
{
    if (m_cfg.brokerHost.trimmed().isEmpty() || m_cfg.clientId.trimmed().isEmpty()) {
        emit mqttStateChanged(false, QStringLiteral("请先填写 Broker 地址与 ClientId"));
        return;
    }
    m_mqtt->connectToBroker(m_cfg.brokerHost.trimmed(),
                            m_cfg.brokerPort,
                            m_cfg.clientId.trimmed(),
                            m_cfg.username.trimmed(),
                            m_cfg.password,
                            static_cast<quint16>(qBound(10, m_cfg.keepAliveSec, 1200)));
}

void HuaweiIotdaClient::disconnectMqtt()
{
    m_mqtt->disconnectFromBroker();
}

bool HuaweiIotdaClient::isMqttConnected() const
{
    return m_mqtt->isConnected();
}

void HuaweiIotdaClient::publishProperties(const FallStatusSnapshot &st)
{
    if (!m_mqtt->isConnected()) {
        return;
    }
    const QString topic = buildPropertyReportTopic(m_cfg.deviceId);
    const QByteArray payload = buildPropertyReportPayload(m_cfg.serviceId, st);
    m_mqtt->publish(topic, payload, 1, false);
    emit mqttMessagePublished(topic, payload);
}

void HuaweiIotdaClient::sendCommandResponse(const QString &requestId, int resultCode, const QString &resultText)
{
    if (!m_mqtt->isConnected() || requestId.isEmpty()) {
        return;
    }
    const QString topic = buildCommandRespTopic(m_cfg.deviceId, requestId);
    const QByteArray payload = buildCommandResponsePayload(resultCode, resultText);
    m_mqtt->publish(topic, payload, 1, false);
}

QString HuaweiIotdaClient::buildPropertyReportTopic(const QString &deviceId)
{
    return QStringLiteral("$oc/devices/%1/sys/properties/report").arg(deviceId);
}

QString HuaweiIotdaClient::buildCommandSubTopic(const QString &deviceId)
{
    return QStringLiteral("$oc/devices/%1/sys/commands/#").arg(deviceId);
}

QString HuaweiIotdaClient::buildCommandRespTopic(const QString &deviceId, const QString &requestId)
{
    return QStringLiteral("$oc/devices/%1/sys/commands/response/request_id=%2").arg(deviceId, requestId);
}

QByteArray HuaweiIotdaClient::buildPropertyReportPayload(const QString &serviceId, const FallStatusSnapshot &st)
{
    const double roundedConf = std::round(qBound(0.0, st.confidence, 1.0) * 100.0) / 100.0;
    const QString bboxStr = QStringLiteral("[%1,%2,%3,%4]").arg(st.x).arg(st.y).arg(st.w).arg(st.h);

    QJsonObject props;
    props[QStringLiteral("fall_state")] = st.fallState ? 1 : 0;
    props[QStringLiteral("confidence")] = roundedConf;
    props[QStringLiteral("bbox")] = bboxStr;
    props[QStringLiteral("beep")] = st.beepOn ? 1 : 0;
    props[QStringLiteral("vision_offline")] = st.visionOffline ? 1 : 0;

    QJsonObject serviceObj;
    serviceObj[QStringLiteral("service_id")] = serviceId.isEmpty() ? QStringLiteral("FallDetect") : serviceId;
    serviceObj[QStringLiteral("properties")] = props;

    QJsonArray services;
    services.append(serviceObj);

    QJsonObject root;
    root[QStringLiteral("services")] = services;
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray HuaweiIotdaClient::buildCommandResponsePayload(int resultCode, const QString &resultText)
{
    QJsonObject paras;
    paras[QStringLiteral("result")] = resultText;

    QJsonObject root;
    root[QStringLiteral("result_code")] = resultCode;
    root[QStringLiteral("response_name")] = QStringLiteral("COMMAND_RESPONSE");
    root[QStringLiteral("paras")] = paras;
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool HuaweiIotdaClient::parseCommandMessage(const QString &topic, const QByteArray &payload, IotdaCommand &out)
{
    const int idx = topic.indexOf(QStringLiteral("request_id="));
    if (idx < 0) {
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (!doc.isObject()) {
        return false;
    }
    const QJsonObject obj = doc.object();
    const QString cmdName = obj.value(QStringLiteral("command_name")).toString();
    if (cmdName.isEmpty()) {
        return false;
    }

    out.requestId = topic.mid(idx + 11).trimmed();
    out.serviceId = obj.value(QStringLiteral("service_id")).toString();
    out.commandName = cmdName;
    out.rawPayload = QString::fromUtf8(payload);
    return !out.requestId.isEmpty();
}

bool HuaweiIotdaClient::parseShadowResponse(const QByteArray &jsonBytes, FallStatusSnapshot &outSnapshot, QString &eventTimeOut)
{
    const QJsonDocument doc = QJsonDocument::fromJson(jsonBytes);
    if (!doc.isObject()) {
        return false;
    }
    const QJsonArray shadows = doc.object().value(QStringLiteral("shadow")).toArray();
    if (shadows.isEmpty()) {
        return false;
    }

    const QJsonObject reported = shadows.at(0).toObject().value(QStringLiteral("reported")).toObject();
    const QJsonObject props = reported.value(QStringLiteral("properties")).toObject();
    if (props.isEmpty()) {
        return false;
    }

    outSnapshot.fallState = (props.value(QStringLiteral("fall_state")).toInt() != 0);
    outSnapshot.sysState = outSnapshot.fallState ? FallSysState::Alarm : FallSysState::Idle;
    outSnapshot.beepOn = (props.value(QStringLiteral("beep")).toInt() != 0);
    outSnapshot.visionOffline = (props.value(QStringLiteral("vision_offline")).toInt() != 0);
    outSnapshot.confidence = props.value(QStringLiteral("confidence")).toDouble();

    const QJsonValue bboxVal = props.value(QStringLiteral("bbox"));
    if (bboxVal.isArray()) {
        const QJsonArray arr = bboxVal.toArray();
        if (arr.size() == 4) {
            outSnapshot.x = arr[0].toInt();
            outSnapshot.y = arr[1].toInt();
            outSnapshot.w = arr[2].toInt();
            outSnapshot.h = arr[3].toInt();
        }
    } else if (bboxVal.isString()) {
        static const QRegularExpression re(R"(\[\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\])");
        const auto m = re.match(bboxVal.toString());
        if (m.hasMatch()) {
            outSnapshot.x = m.captured(1).toInt();
            outSnapshot.y = m.captured(2).toInt();
            outSnapshot.w = m.captured(3).toInt();
            outSnapshot.h = m.captured(4).toInt();
        }
    }

    eventTimeOut = reported.value(QStringLiteral("event_time")).toString();
    return true;
}

bool HuaweiIotdaClient::parseAppMqttHeader(const QString &headerText, HuaweiCloudConfig &inOutCfg)
{
    auto matchDefineStr = [&headerText](const QString &macroName, QString &target) -> bool {
        const QRegularExpression re(QStringLiteral(R"re(#define\s+%1\s+"([^"]*)")re").arg(macroName));
        const auto m = re.match(headerText);
        if (m.hasMatch()) {
            target = m.captured(1);
            return true;
        }
        return false;
    };

    bool anyFound = false;
    anyFound |= matchDefineStr(QStringLiteral("MQTT_BROKER_HOST"), inOutCfg.brokerHost);
    anyFound |= matchDefineStr(QStringLiteral("MQTT_CLIENT_ID"), inOutCfg.clientId);
    anyFound |= matchDefineStr(QStringLiteral("MQTT_USERNAME"), inOutCfg.username);
    anyFound |= matchDefineStr(QStringLiteral("MQTT_PASSWORD"), inOutCfg.password);
    anyFound |= matchDefineStr(QStringLiteral("HUAWEI_DEVICE_ID"), inOutCfg.deviceId);
    anyFound |= matchDefineStr(QStringLiteral("HUAWEI_SERVICE_ID"), inOutCfg.serviceId);

    const QRegularExpression rePort(QStringLiteral(R"(#define\s+MQTT_BROKER_PORT\s+(\d+)[Uu]?)"));
    const auto mPort = rePort.match(headerText);
    if (mPort.hasMatch()) {
        inOutCfg.brokerPort = static_cast<quint16>(mPort.captured(1).toUInt());
        anyFound = true;
    }
    return anyFound;
}

void HuaweiIotdaClient::fetchIamToken()
{
    if (m_cfg.iamEndpoint.isEmpty() || m_cfg.iamDomain.isEmpty() ||
        m_cfg.iamUser.isEmpty() || m_cfg.iamPassword.isEmpty()) {
        emit northboundResult(false, QStringLiteral("IAM 参数不完整，无法获取 Token"));
        return;
    }

    const QString url = m_cfg.iamEndpoint.trimmed().replace(QRegularExpression("/+$"), "") + QStringLiteral("/v3/auth/tokens");
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QJsonObject domainObj{{QStringLiteral("name"), m_cfg.iamDomain}};
    QJsonObject userObj{{QStringLiteral("name"), m_cfg.iamUser},
                        {QStringLiteral("password"), m_cfg.iamPassword},
                        {QStringLiteral("domain"), domainObj}};
    QJsonObject passwordObj{{QStringLiteral("user"), userObj}};
    QJsonObject identityObj{{QStringLiteral("methods"), QJsonArray{QStringLiteral("password")}},
                            {QStringLiteral("password"), passwordObj}};
    QJsonObject scopeObj;
    if (!m_cfg.projectId.isEmpty()) {
        scopeObj[QStringLiteral("project")] = QJsonObject{{QStringLiteral("id"), m_cfg.projectId}};
    }
    QJsonObject authObj{{QStringLiteral("identity"), identityObj}};
    if (!scopeObj.isEmpty()) {
        authObj[QStringLiteral("scope")] = scopeObj;
    }
    const QByteArray body = QJsonDocument(QJsonObject{{QStringLiteral("auth"), authObj}}).toJson(QJsonDocument::Compact);

    QNetworkReply *reply = m_nam->post(req, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit northboundResult(false, QStringLiteral("获取 IAM Token 失败: %1").arg(reply->errorString()));
            return;
        }
        const QByteArray token = reply->rawHeader("X-Subject-Token");
        if (token.isEmpty()) {
            emit northboundResult(false, QStringLiteral("IAM 响应缺少 X-Subject-Token"));
            return;
        }
        m_cfg.iamToken = QString::fromUtf8(token);
        emit tokenUpdated(m_cfg.iamToken);
        emit northboundResult(true, QStringLiteral("IAM Token 获取成功"));
    });
}

void HuaweiIotdaClient::queryDeviceShadow()
{
    if (m_cfg.iotdaAppEndpoint.isEmpty() || m_cfg.projectId.isEmpty() ||
        m_cfg.deviceId.isEmpty() || m_cfg.iamToken.isEmpty()) {
        emit northboundResult(false, QStringLiteral("北向查询需要 AppEndpoint、ProjectId、DeviceId 与 Token"));
        return;
    }

    const QString base = m_cfg.iotdaAppEndpoint.trimmed().replace(QRegularExpression("/+$"), "");
    const QString url = QStringLiteral("%1/v5/iot/%2/devices/%3/shadow")
                            .arg(base, m_cfg.projectId.trimmed(), m_cfg.deviceId.trimmed());
    QNetworkRequest req{QUrl(url)};
    req.setRawHeader("X-Auth-Token", m_cfg.iamToken.toUtf8());

    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const QByteArray data = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            emit northboundResult(false, QStringLiteral("查询影子失败: %1 %2").arg(reply->errorString(), QString::fromUtf8(data)));
            return;
        }
        FallStatusSnapshot snap;
        QString evtTime;
        if (parseShadowResponse(data, snap, evtTime)) {
            emit shadowUpdated(snap, evtTime);
            emit northboundResult(true, QStringLiteral("影子已刷新 (%1)").arg(evtTime));
        } else {
            emit northboundResult(false, QStringLiteral("影子 JSON 解析失败"));
        }
    });
}

void HuaweiIotdaClient::sendNorthboundCommand(const QString &commandName)
{
    if (m_cfg.iotdaAppEndpoint.isEmpty() || m_cfg.projectId.isEmpty() ||
        m_cfg.deviceId.isEmpty() || m_cfg.iamToken.isEmpty()) {
        emit northboundResult(false, QStringLiteral("北向命令需要 AppEndpoint、ProjectId、DeviceId 与 Token"));
        return;
    }

    const QString base = m_cfg.iotdaAppEndpoint.trimmed().replace(QRegularExpression("/+$"), "");
    const QString url = QStringLiteral("%1/v5/iot/%2/devices/%3/commands")
                            .arg(base, m_cfg.projectId.trimmed(), m_cfg.deviceId.trimmed());
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("X-Auth-Token", m_cfg.iamToken.toUtf8());

    QJsonObject root;
    root[QStringLiteral("service_id")] = m_cfg.serviceId.isEmpty() ? QStringLiteral("FallDetect") : m_cfg.serviceId;
    root[QStringLiteral("command_name")] = commandName;
    root[QStringLiteral("paras")] = QJsonObject{};

    QNetworkReply *reply = m_nam->post(req, QJsonDocument(root).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, commandName]() {
        reply->deleteLater();
        const QByteArray resp = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            emit northboundResult(false, QStringLiteral("下发 %1 失败: %2 %3")
                                             .arg(commandName, reply->errorString(), QString::fromUtf8(resp)));
            return;
        }
        emit northboundResult(true, QStringLiteral("云端命令 %1 下发成功").arg(commandName));
    });
}

} // namespace qtfall
