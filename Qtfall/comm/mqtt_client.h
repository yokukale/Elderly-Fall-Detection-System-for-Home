#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include "mqtt_codec.h"
#include <QByteArray>
#include <QObject>
#include <QTcpSocket>
#include <QTimer>

namespace qtfall {

class MqttClient : public QObject
{
    Q_OBJECT
public:
    explicit MqttClient(QObject *parent = nullptr);

    void connectToBroker(const QString &host,
                         quint16 port,
                         const QString &clientId,
                         const QString &username,
                         const QString &password,
                         quint16 keepAliveSec = 30);
    void disconnectFromBroker();
    bool isConnected() const { return m_connected; }

    quint16 publish(const QString &topic, const QByteArray &payload, int qos = 1, bool retain = false);
    quint16 subscribe(const QString &topicFilter, int qos = 1);

signals:
    void connected();
    void disconnected(const QString &reason);
    void messageReceived(const QString &topic, const QByteArray &payload);
    void errorOccurred(const QString &message);

private slots:
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketReadyRead();
    void onSocketError(QAbstractSocket::SocketError err);
    void onKeepAliveTimeout();

private:
    void handleDecodedPacket(const MqttDecodedPacket &pkt);

    QTcpSocket *m_socket = nullptr;
    QTimer *m_keepAliveTimer = nullptr;
    QByteArray m_rxBuffer;
    QString m_clientId;
    QString m_username;
    QString m_password;
    quint16 m_keepAliveSec = 30;
    quint16 m_nextPacketId = 1;
    bool m_connected = false;
};

} // namespace qtfall

#endif // MQTT_CLIENT_H
