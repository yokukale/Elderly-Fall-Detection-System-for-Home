#ifndef MQTT_CODEC_H
#define MQTT_CODEC_H

#include <QByteArray>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace qtfall {

enum class MqttPacketType : quint8 {
    Connect = 1,
    ConnAck = 2,
    Publish = 3,
    PubAck = 4,
    Subscribe = 8,
    SubAck = 9,
    PingReq = 12,
    PingResp = 13,
    Disconnect = 14
};

struct MqttDecodedPacket {
    MqttPacketType type = MqttPacketType::PingResp;
    quint8 flags = 0;
    int qos = 0;
    bool retain = false;
    bool dup = false;
    quint16 packetId = 0;
    quint8 connAckReturnCode = 0;
    QString topic;
    QByteArray payload;
    QVector<quint8> subAckCodes;
};

class MqttCodec
{
public:
    static QByteArray encodeRemainingLength(int length);
    static bool decodeRemainingLength(const QByteArray &buf, int offset, int &lengthOut, int &bytesReadOut);

    static QByteArray encodeString(const QString &str);
    static bool decodeString(const QByteArray &buf, int &offset, QString &out);

    static QByteArray encodeConnect(const QString &clientId,
                                    const QString &username,
                                    const QString &password,
                                    quint16 keepAliveSec,
                                    bool cleanSession = true);
    static QByteArray encodePublish(const QString &topic,
                                    const QByteArray &payload,
                                    int qos = 0,
                                    quint16 packetId = 0,
                                    bool retain = false);
    static QByteArray encodePubAck(quint16 packetId);
    static QByteArray encodeSubscribe(quint16 packetId, const QString &topicFilter, int qos = 1);
    static QByteArray encodePingReq();
    static QByteArray encodeDisconnect();

    enum class DecodeStatus {
        NeedMoreData,
        Ok,
        Malformed
    };

    static DecodeStatus tryDecodePacket(QByteArray &rxBuffer, MqttDecodedPacket &out);
};

} // namespace qtfall

#endif // MQTT_CODEC_H
