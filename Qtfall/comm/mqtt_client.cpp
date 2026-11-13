#include "mqtt_client.h"

namespace qtfall {

MqttClient::MqttClient(QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
    , m_keepAliveTimer(new QTimer(this))
{
    connect(m_socket, &QTcpSocket::connected, this, &MqttClient::onSocketConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &MqttClient::onSocketDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &MqttClient::onSocketReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &MqttClient::onSocketError);
    connect(m_keepAliveTimer, &QTimer::timeout, this, &MqttClient::onKeepAliveTimeout);
}

void MqttClient::connectToBroker(const QString &host,
                                 quint16 port,
                                 const QString &clientId,
                                 const QString &username,
                                 const QString &password,
                                 quint16 keepAliveSec)
{
    disconnectFromBroker();
    m_clientId = clientId;
    m_username = username;
    m_password = password;
    m_keepAliveSec = qMax<quint16>(5, keepAliveSec);
    m_rxBuffer.clear();
    m_socket->connectToHost(host, port);
}

void MqttClient::disconnectFromBroker()
{
    m_keepAliveTimer->stop();
    if (m_connected && m_socket->state() == QAbstractSocket::ConnectedState) {
        m_socket->write(MqttCodec::encodeDisconnect());
        m_socket->flush();
    }
    m_connected = false;
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->disconnectFromHost();
    }
}

quint16 MqttClient::publish(const QString &topic, const QByteArray &payload, int qos, bool retain)
{
    if (!m_connected) {
        return 0;
    }
    const quint16 pid = (qos > 0) ? m_nextPacketId++ : 0;
    if (m_nextPacketId == 0) m_nextPacketId = 1;
    m_socket->write(MqttCodec::encodePublish(topic, payload, qos, pid, retain));
    return pid;
}

quint16 MqttClient::subscribe(const QString &topicFilter, int qos)
{
    if (!m_connected) {
        return 0;
    }
    const quint16 pid = m_nextPacketId++;
    if (m_nextPacketId == 0) m_nextPacketId = 1;
    m_socket->write(MqttCodec::encodeSubscribe(pid, topicFilter, qos));
    return pid;
}

void MqttClient::onSocketConnected()
{
    m_socket->write(MqttCodec::encodeConnect(m_clientId, m_username, m_password, m_keepAliveSec, true));
}

void MqttClient::onSocketDisconnected()
{
    m_keepAliveTimer->stop();
    const bool wasConnected = m_connected;
    m_connected = false;
    if (wasConnected) {
        emit disconnected(QStringLiteral("TCP 连接已断开"));
    }
}

void MqttClient::onSocketReadyRead()
{
    m_rxBuffer.append(m_socket->readAll());
    MqttDecodedPacket pkt;
    while (true) {
        const auto st = MqttCodec::tryDecodePacket(m_rxBuffer, pkt);
        if (st == MqttCodec::DecodeStatus::Ok) {
            handleDecodedPacket(pkt);
        } else if (st == MqttCodec::DecodeStatus::Malformed) {
            emit errorOccurred(QStringLiteral("MQTT 报文格式异常"));
            disconnectFromBroker();
            break;
        } else {
            break;
        }
    }
}

void MqttClient::handleDecodedPacket(const MqttDecodedPacket &pkt)
{
    switch (pkt.type) {
    case MqttPacketType::ConnAck:
        if (pkt.connAckReturnCode == 0) {
            m_connected = true;
            m_keepAliveTimer->start(static_cast<int>(m_keepAliveSec) * 500);
            emit connected();
        } else {
            emit errorOccurred(QStringLiteral("MQTT CONNACK 拒绝，返回码=%1").arg(pkt.connAckReturnCode));
            m_socket->disconnectFromHost();
        }
        break;
    case MqttPacketType::Publish:
        if (pkt.qos == 1) {
            m_socket->write(MqttCodec::encodePubAck(pkt.packetId));
        }
        emit messageReceived(pkt.topic, pkt.payload);
        break;
    default:
        break;
    }
}

void MqttClient::onSocketError(QAbstractSocket::SocketError)
{
    m_keepAliveTimer->stop();
    m_connected = false;
    emit errorOccurred(m_socket->errorString());
}

void MqttClient::onKeepAliveTimeout()
{
    if (m_connected && m_socket->state() == QAbstractSocket::ConnectedState) {
        m_socket->write(MqttCodec::encodePingReq());
    }
}

} // namespace qtfall
