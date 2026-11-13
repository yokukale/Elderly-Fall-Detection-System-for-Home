#include "mqtt_codec.h"

namespace qtfall {

namespace {
void appendUint16BE(QByteArray &out, quint16 v)
{
    out.append(static_cast<char>((v >> 8) & 0xFF));
    out.append(static_cast<char>(v & 0xFF));
}

bool readUint16BE(const QByteArray &buf, int &offset, quint16 &out)
{
    if (offset + 2 > buf.size()) {
        return false;
    }
    const quint8 hi = static_cast<quint8>(buf.at(offset));
    const quint8 lo = static_cast<quint8>(buf.at(offset + 1));
    out = static_cast<quint16>((hi << 8) | lo);
    offset += 2;
    return true;
}
} // namespace

QByteArray MqttCodec::encodeRemainingLength(int length)
{
    QByteArray out;
    int x = qMax(0, length);
    do {
        quint8 encodedByte = static_cast<quint8>(x % 128);
        x /= 128;
        if (x > 0) {
            encodedByte |= 0x80;
        }
        out.append(static_cast<char>(encodedByte));
    } while (x > 0);
    return out;
}

bool MqttCodec::decodeRemainingLength(const QByteArray &buf, int offset, int &lengthOut, int &bytesReadOut)
{
    int multiplier = 1;
    int value = 0;
    int idx = offset;

    for (int i = 0; i < 4; ++i) {
        if (idx >= buf.size()) {
            return false;
        }
        const quint8 encodedByte = static_cast<quint8>(buf.at(idx++));
        value += (encodedByte & 0x7F) * multiplier;
        if ((encodedByte & 0x80) == 0) {
            lengthOut = value;
            bytesReadOut = idx - offset;
            return true;
        }
        multiplier *= 128;
    }
    return false;
}

QByteArray MqttCodec::encodeString(const QString &str)
{
    const QByteArray utf8 = str.toUtf8();
    QByteArray out;
    out.reserve(2 + utf8.size());
    appendUint16BE(out, static_cast<quint16>(utf8.size()));
    out.append(utf8);
    return out;
}

bool MqttCodec::decodeString(const QByteArray &buf, int &offset, QString &out)
{
    quint16 len = 0;
    if (!readUint16BE(buf, offset, len)) {
        return false;
    }
    if (offset + len > buf.size()) {
        return false;
    }
    out = QString::fromUtf8(buf.constData() + offset, len);
    offset += len;
    return true;
}

QByteArray MqttCodec::encodeConnect(const QString &clientId,
                                    const QString &username,
                                    const QString &password,
                                    quint16 keepAliveSec,
                                    bool cleanSession)
{
    QByteArray variable;
    variable.append(encodeString(QStringLiteral("MQTT")));
    variable.append(static_cast<char>(0x04)); // MQTT 3.1.1

    quint8 connectFlags = 0;
    if (cleanSession) connectFlags |= 0x02;
    if (!username.isEmpty()) connectFlags |= 0x80;
    if (!password.isEmpty()) connectFlags |= 0x40;
    variable.append(static_cast<char>(connectFlags));
    appendUint16BE(variable, keepAliveSec);

    QByteArray payload;
    payload.append(encodeString(clientId));
    if (!username.isEmpty()) {
        payload.append(encodeString(username));
    }
    if (!password.isEmpty()) {
        payload.append(encodeString(password));
    }

    QByteArray packet;
    packet.append(static_cast<char>(0x10)); // CONNECT
    packet.append(encodeRemainingLength(variable.size() + payload.size()));
    packet.append(variable);
    packet.append(payload);
    return packet;
}

QByteArray MqttCodec::encodePublish(const QString &topic,
                                    const QByteArray &payload,
                                    int qos,
                                    quint16 packetId,
                                    bool retain)
{
    QByteArray variable;
    variable.append(encodeString(topic));
    if (qos > 0) {
        appendUint16BE(variable, packetId);
    }

    quint8 header = 0x30;
    if (qos == 1) header |= 0x02;
    else if (qos == 2) header |= 0x04;
    if (retain) header |= 0x01;

    QByteArray packet;
    packet.append(static_cast<char>(header));
    packet.append(encodeRemainingLength(variable.size() + payload.size()));
    packet.append(variable);
    packet.append(payload);
    return packet;
}

QByteArray MqttCodec::encodePubAck(quint16 packetId)
{
    QByteArray packet;
    packet.append(static_cast<char>(0x40));
    packet.append(static_cast<char>(0x02));
    appendUint16BE(packet, packetId);
    return packet;
}

QByteArray MqttCodec::encodeSubscribe(quint16 packetId, const QString &topicFilter, int qos)
{
    QByteArray variable;
    appendUint16BE(variable, packetId);
    variable.append(encodeString(topicFilter));
    variable.append(static_cast<char>(qBound(0, qos, 1)));

    QByteArray packet;
    packet.append(static_cast<char>(0x82)); // SUBSCRIBE with fixed flags 0x02
    packet.append(encodeRemainingLength(variable.size()));
    packet.append(variable);
    return packet;
}

QByteArray MqttCodec::encodePingReq()
{
    return QByteArray("\xC0\x00", 2);
}

QByteArray MqttCodec::encodeDisconnect()
{
    return QByteArray("\xE0\x00", 2);
}

MqttCodec::DecodeStatus MqttCodec::tryDecodePacket(QByteArray &rxBuffer, MqttDecodedPacket &out)
{
    if (rxBuffer.size() < 2) {
        return DecodeStatus::NeedMoreData;
    }

    const quint8 firstByte = static_cast<quint8>(rxBuffer.at(0));
    const quint8 rawType = (firstByte >> 4) & 0x0F;
    const quint8 flags = firstByte & 0x0F;

    int remLen = 0;
    int lenBytes = 0;
    if (!decodeRemainingLength(rxBuffer, 1, remLen, lenBytes)) {
        // 如果连续超过 4 字节都有最高位则报文畸形
        if (rxBuffer.size() >= 5 &&
            (static_cast<quint8>(rxBuffer.at(1)) & 0x80) &&
            (static_cast<quint8>(rxBuffer.at(2)) & 0x80) &&
            (static_cast<quint8>(rxBuffer.at(3)) & 0x80) &&
            (static_cast<quint8>(rxBuffer.at(4)) & 0x80)) {
            return DecodeStatus::Malformed;
        }
        return DecodeStatus::NeedMoreData;
    }

    const int headerLen = 1 + lenBytes;
    const int totalLen = headerLen + remLen;
    if (rxBuffer.size() < totalLen) {
        return DecodeStatus::NeedMoreData;
    }

    const QByteArray body = rxBuffer.mid(headerLen, remLen);
    rxBuffer.remove(0, totalLen);

    out = MqttDecodedPacket{};
    out.type = static_cast<MqttPacketType>(rawType);
    out.flags = flags;

    int offset = 0;
    switch (out.type) {
    case MqttPacketType::ConnAck:
        if (body.size() < 2) return DecodeStatus::Malformed;
        out.connAckReturnCode = static_cast<quint8>(body.at(1));
        return DecodeStatus::Ok;

    case MqttPacketType::Publish:
        out.dup = (flags & 0x08) != 0;
        out.qos = (flags >> 1) & 0x03;
        out.retain = (flags & 0x01) != 0;
        if (!decodeString(body, offset, out.topic)) return DecodeStatus::Malformed;
        if (out.qos > 0) {
            if (!readUint16BE(body, offset, out.packetId)) return DecodeStatus::Malformed;
        }
        out.payload = body.mid(offset);
        return DecodeStatus::Ok;

    case MqttPacketType::PubAck:
        if (!readUint16BE(body, offset, out.packetId)) return DecodeStatus::Malformed;
        return DecodeStatus::Ok;

    case MqttPacketType::SubAck:
        if (!readUint16BE(body, offset, out.packetId)) return DecodeStatus::Malformed;
        while (offset < body.size()) {
            out.subAckCodes.append(static_cast<quint8>(body.at(offset++)));
        }
        return DecodeStatus::Ok;

    case MqttPacketType::PingResp:
        return DecodeStatus::Ok;

    default:
        return DecodeStatus::Ok;
    }
}

} // namespace qtfall
