#include "fall_frame_parser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <cmath>

namespace qtfall {

namespace {
constexpr int kMaxFrameLen = 80;
constexpr int kMaxLineLen = 1024;

bool parseStrictInt(const QByteArray &token, int minVal, int maxVal, int &out)
{
    if (token.isEmpty()) {
        return false;
    }
    bool ok = false;
    const int v = token.trimmed().toInt(&ok, 10);
    if (!ok || v < minVal || v > maxVal) {
        return false;
    }
    out = v;
    return true;
}

bool parseBboxArrayOrString(const QJsonValue &val, QRect &rectOut)
{
    if (val.isArray()) {
        const QJsonArray arr = val.toArray();
        if (arr.size() == 4) {
            rectOut = QRect(arr[0].toInt(), arr[1].toInt(), arr[2].toInt(), arr[3].toInt());
            return true;
        }
        return false;
    }
    if (val.isString()) {
        static const QRegularExpression re(R"(\[\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\])");
        const QRegularExpressionMatch m = re.match(val.toString());
        if (m.hasMatch()) {
            rectOut = QRect(m.captured(1).toInt(), m.captured(2).toInt(),
                            m.captured(3).toInt(), m.captured(4).toInt());
            return true;
        }
    }
    return false;
}
} // namespace

FallFrameParser::FallFrameParser(QObject *parent)
    : QObject(parent)
{
}

void FallFrameParser::reset()
{
    m_state = RxState::Idle;
    m_frameBuf.clear();
    m_lineBuf.clear();
    m_okCount = 0;
    m_errCount = 0;
    m_logCount = 0;
}

quint8 FallFrameParser::computeXor(const QByteArray &payloadBetweenDollarAndStar)
{
    quint8 chk = 0;
    for (unsigned char c : payloadBetweenDollarAndStar) {
        chk ^= c;
    }
    return chk;
}

QByteArray FallFrameParser::formatLegacyFrame(int x, int y, int w, int h, bool isFall, double score)
{
    const double clamped = qBound(0.0, score, 1.0);
    return QStringLiteral("$%1,%2,%3,%4,%5,%6#")
        .arg(x)
        .arg(y)
        .arg(w)
        .arg(h)
        .arg(isFall ? QStringLiteral("Fall") : QStringLiteral("NoFall"))
        .arg(QString::number(clamped, 'f', 2))
        .toUtf8();
}

QByteArray FallFrameParser::formatRobustFrame(int evt, int state, int cls, int scoreX100, int cx, int cy, int seq)
{
    const QByteArray body = QStringLiteral("F,%1,%2,%3,%4,%5,%6,%7")
                                .arg(evt)
                                .arg(state)
                                .arg(cls)
                                .arg(qBound(0, scoreX100, 100))
                                .arg(cx)
                                .arg(cy)
                                .arg(seq & 0xFF)
                                .toUtf8();
    const quint8 chk = computeXor(body);
    const QByteArray hex = QByteArray::number(chk, 16).toUpper().rightJustified(2, '0');
    return QByteArray("$") + body + "*" + hex + "\r\n";
}

bool FallFrameParser::parseLegacyFrame(const QByteArray &body, FallRawFrame &out, qint64 nowMs)
{
    const QList<QByteArray> fields = body.split(',');
    if (fields.size() != 6) {
        return false;
    }

    int x = 0, y = 0, w = 0, h = 0;
    if (!parseStrictInt(fields[0], 0, 65535, x) ||
        !parseStrictInt(fields[1], 0, 65535, y) ||
        !parseStrictInt(fields[2], 0, 65535, w) ||
        !parseStrictInt(fields[3], 0, 65535, h)) {
        return false;
    }

    const QByteArray stateField = fields[4].trimmed();
    bool isFall = false;
    if (stateField == "Fall") {
        isFall = true;
    } else if (stateField == "NoFall") {
        isFall = false;
    } else {
        return false;
    }

    bool scoreOk = false;
    const double score = fields[5].trimmed().toDouble(&scoreOk);
    if (!scoreOk || !std::isfinite(score) || score < 0.0 || score > 1.0) {
        return false;
    }

    out = FallRawFrame{};
    out.protocol = FrameProtocolKind::Legacy;
    out.x = x;
    out.y = y;
    out.w = w;
    out.h = h;
    out.isFall = isFall;
    out.poseClass = isFall ? PoseClass::Fall : PoseClass::NoFall;
    out.score = score;
    out.cx = (x + w / 2) * 100 / kK230FrameWidth;
    out.cy = (y + h / 2) * 100 / kK230FrameHeight;
    out.timestampMs = nowMs;
    out.rawText = QStringLiteral("$%1#").arg(QString::fromUtf8(body));
    return true;
}

bool FallFrameParser::parseRobustFrame(const QByteArray &body, FallRawFrame &out, qint64 nowMs)
{
    QByteArray trimmed = body.trimmed();
    const int starIdx = trimmed.indexOf('*');
    if (starIdx < 3 || starIdx + 3 > trimmed.size()) {
        return false;
    }

    const QByteArray payload = trimmed.left(starIdx);
    const QByteArray hexPart = trimmed.mid(starIdx + 1, 2);
    if (trimmed.size() != starIdx + 3) {
        return false;
    }

    bool hexOk = false;
    const int expectedChk = hexPart.toInt(&hexOk, 16);
    if (!hexOk || expectedChk < 0 || expectedChk > 255) {
        return false;
    }
    if (computeXor(payload) != static_cast<quint8>(expectedChk)) {
        return false;
    }

    const QList<QByteArray> fields = payload.split(',');
    if (fields.size() != 8 || fields[0] != "F") {
        return false;
    }

    int evt = 0, state = 0, clsVal = -1, scoreX100 = 0, cx = -1, cy = -1, seq = 0;
    if (!parseStrictInt(fields[1], 0, 255, evt) ||
        !parseStrictInt(fields[2], 0, 1, state) ||
        !parseStrictInt(fields[3], -1, 10, clsVal) ||
        !parseStrictInt(fields[4], 0, 100, scoreX100) ||
        !parseStrictInt(fields[5], -1, 100, cx) ||
        !parseStrictInt(fields[6], -1, 100, cy) ||
        !parseStrictInt(fields[7], 0, 255, seq)) {
        return false;
    }

    PoseClass pose = PoseClass::NoTarget;
    if (clsVal >= -1 && clsVal <= 3) {
        pose = static_cast<PoseClass>(clsVal);
    }

    const bool isFall = (state != 0);
    int x = 0, y = 0, w = 0, h = 0;
    if (cx >= 0 && cy >= 0) {
        const bool horizontal = isFall || (pose == PoseClass::SideLying);
        w = horizontal ? 220 : 110;
        h = horizontal ? 120 : 220;
        const int px = cx * kK230FrameWidth / 100;
        const int py = cy * kK230FrameHeight / 100;
        x = qBound(0, px - w / 2, kK230FrameWidth - w);
        y = qBound(0, py - h / 2, kK230FrameHeight - h);
    }

    out = FallRawFrame{};
    out.protocol = FrameProtocolKind::Robust;
    out.x = x;
    out.y = y;
    out.w = w;
    out.h = h;
    out.isFall = isFall;
    out.poseClass = pose;
    out.score = static_cast<double>(scoreX100) / 100.0;
    out.evt = evt;
    out.cx = cx;
    out.cy = cy;
    out.seq = seq;
    out.timestampMs = nowMs;
    out.rawText = QStringLiteral("$%1").arg(QString::fromUtf8(trimmed));
    return true;
}

bool FallFrameParser::parseStm32LogLine(const QString &line, Stm32LogEvent &out)
{
    const QString s = line.trimmed();
    if (!s.startsWith('[')) {
        return false;
    }

    out = Stm32LogEvent{};
    out.rawLine = s;

    if (s.startsWith(QStringLiteral("[FALL] ALARM TRIGGERED!"))) {
        out.type = Stm32LogEvent::Type::AlarmTriggered;
        out.hasStatus = true;
        out.fallState = 1;
        out.beepOn = 1;
        static const QRegularExpression reHits(R"(hits=(\d+)/\d+)");
        static const QRegularExpression reScore(R"(score=([0-9]+\.[0-9]+))");
        static const QRegularExpression reBbox(R"(bbox=\((\d+),(\d+),(\d+),(\d+)\))");
        const auto mHits = reHits.match(s);
        if (mHits.hasMatch()) out.winHits = mHits.captured(1).toInt();
        const auto mScore = reScore.match(s);
        if (mScore.hasMatch()) out.confidence = mScore.captured(1).toDouble();
        const auto mBbox = reBbox.match(s);
        if (mBbox.hasMatch()) {
            out.hasBbox = true;
            out.bbox = QRect(mBbox.captured(1).toInt(), mBbox.captured(2).toInt(),
                             mBbox.captured(3).toInt(), mBbox.captured(4).toInt());
        }
        return true;
    }

    if (s.startsWith(QStringLiteral("[FALL] K230 vision offline timeout"))) {
        out.type = Stm32LogEvent::Type::VisionOffline;
        out.hasStatus = true;
        out.visionOffline = 1;
        return true;
    }

    if (s.startsWith(QStringLiteral("[FALL] Alarm hold expired"))) {
        out.type = Stm32LogEvent::Type::AlarmHoldExpired;
        out.hasStatus = true;
        out.fallState = 0;
        out.beepOn = 0;
        return true;
    }

    if (s.startsWith(QStringLiteral("[FALL] Cooldown finished"))) {
        out.type = Stm32LogEvent::Type::CooldownFinished;
        out.hasStatus = true;
        out.fallState = 0;
        out.beepOn = 0;
        return true;
    }

    if (s.startsWith(QStringLiteral("[FALL] Muted by command"))) {
        out.type = Stm32LogEvent::Type::Muted;
        out.hasStatus = true;
        out.beepOn = 0;
        return true;
    }

    if (s.startsWith(QStringLiteral("[FALL] Reset to IDLE"))) {
        out.type = Stm32LogEvent::Type::Reset;
        out.hasStatus = true;
        out.fallState = 0;
        out.beepOn = 0;
        return true;
    }

    if (s.startsWith(QStringLiteral("[ALARM] BEEP ON"))) {
        out.type = Stm32LogEvent::Type::BeepChanged;
        out.hasStatus = true;
        out.beepOn = 1;
        return true;
    }

    if (s.startsWith(QStringLiteral("[ALARM] BEEP OFF"))) {
        out.type = Stm32LogEvent::Type::BeepChanged;
        out.hasStatus = true;
        out.beepOn = 0;
        return true;
    }

    if (s.startsWith(QStringLiteral("[MQTT] Connected to Huawei IoTDA"))) {
        out.type = Stm32LogEvent::Type::MqttConnected;
        return true;
    }

    if (s.startsWith(QStringLiteral("[MQTT] Published:"))) {
        const int jsonStart = s.indexOf('{');
        if (jsonStart < 0) {
            return false;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(s.mid(jsonStart).toUtf8());
        if (!doc.isObject()) {
            return false;
        }
        const QJsonArray services = doc.object().value(QStringLiteral("services")).toArray();
        if (services.isEmpty()) {
            return false;
        }
        const QJsonObject props = services.at(0).toObject().value(QStringLiteral("properties")).toObject();
        out.type = Stm32LogEvent::Type::MqttPublished;
        out.hasStatus = true;
        if (props.contains(QStringLiteral("fall_state"))) out.fallState = props.value(QStringLiteral("fall_state")).toInt();
        if (props.contains(QStringLiteral("beep"))) out.beepOn = props.value(QStringLiteral("beep")).toInt();
        if (props.contains(QStringLiteral("vision_offline"))) out.visionOffline = props.value(QStringLiteral("vision_offline")).toInt();
        if (props.contains(QStringLiteral("confidence"))) out.confidence = props.value(QStringLiteral("confidence")).toDouble();
        if (props.contains(QStringLiteral("bbox"))) {
            out.hasBbox = parseBboxArrayOrString(props.value(QStringLiteral("bbox")), out.bbox);
        }
        return true;
    }

    if (s.startsWith(QStringLiteral("[MQTT-CMD]"))) {
        out.type = Stm32LogEvent::Type::MqttCommand;
        static const QRegularExpression reCmd(R"(\[MQTT-CMD\]\s+(\S+)\s+\(request_id=([^)]+)\))");
        const auto m = reCmd.match(s);
        if (m.hasMatch()) {
            out.commandName = m.captured(1);
            out.requestId = m.captured(2);
        }
        return true;
    }

    if (s.startsWith(QStringLiteral("[BOOT]")) || s.startsWith(QStringLiteral("[MQTT]")) || s.startsWith(QStringLiteral("[FATAL]"))) {
        out.type = Stm32LogEvent::Type::Info;
        return true;
    }

    return false;
}

void FallFrameParser::handleLineCompleted(const QByteArray &lineBytes)
{
    const QString line = QString::fromUtf8(lineBytes).trimmed();
    if (line.isEmpty()) {
        return;
    }
    emit rawLineReceived(line);

    Stm32LogEvent ev;
    if (parseStm32LogLine(line, ev)) {
        m_logCount++;
        emit stm32LogParsed(ev);
    }
}

void FallFrameParser::feedBytes(const QByteArray &data, qint64 nowMs)
{
    for (char rawC : data) {
        const unsigned char c = static_cast<unsigned char>(rawC);

        if (m_state == RxState::InLogLine) {
            if (c == '\n') {
                handleLineCompleted(m_lineBuf);
                m_lineBuf.clear();
                m_state = RxState::Idle;
            } else if (c == '$' && !m_lineBuf.startsWith('[')) {
                // 非方括号开头的杂散日志遇到 '$'，优先同步到新帧起始
                m_lineBuf.clear();
                m_frameBuf.clear();
                m_state = RxState::InLegacyFrame;
            } else if (c != '\r') {
                if (m_lineBuf.size() < kMaxLineLen) {
                    m_lineBuf.append(rawC);
                }
            }
            continue;
        }

        if (m_state == RxState::SkippingFrame) {
            if (c == '#' || c == '\n') {
                m_state = RxState::Idle;
            } else if (c == '$') {
                m_frameBuf.clear();
                m_state = RxState::InLegacyFrame;
            }
            continue;
        }

        if (c == '$') {
            if (m_state == RxState::InLegacyFrame || m_state == RxState::InRobustFrame) {
                // 上一帧未闭合被新帧打断
                m_errCount++;
                emit frameParseError(QString::fromUtf8(m_frameBuf));
            }
            m_frameBuf.clear();
            m_state = RxState::InLegacyFrame;
            continue;
        }

        if (m_state == RxState::Idle) {
            if (c == '\r' || c == '\n') {
                continue;
            }
            m_lineBuf.clear();
            m_lineBuf.append(rawC);
            m_state = RxState::InLogLine;
            continue;
        }

        // InLegacyFrame or InRobustFrame
        if (m_frameBuf.isEmpty() && c == 'F') {
            m_state = RxState::InRobustFrame;
            m_frameBuf.append(rawC);
            continue;
        }

        if (m_state == RxState::InLegacyFrame && c == '#') {
            FallRawFrame frame;
            if (parseLegacyFrame(m_frameBuf, frame, nowMs)) {
                m_okCount++;
                emit frameParsed(frame);
            } else {
                m_errCount++;
                emit frameParseError(QStringLiteral("$%1#").arg(QString::fromUtf8(m_frameBuf)));
            }
            m_frameBuf.clear();
            m_state = RxState::Idle;
            continue;
        }

        if (m_state == RxState::InRobustFrame && c == '\n') {
            FallRawFrame frame;
            if (parseRobustFrame(m_frameBuf, frame, nowMs)) {
                m_okCount++;
                emit frameParsed(frame);
            } else {
                m_errCount++;
                emit frameParseError(QStringLiteral("$%1").arg(QString::fromUtf8(m_frameBuf)));
            }
            m_frameBuf.clear();
            m_state = RxState::Idle;
            continue;
        }

        if (m_frameBuf.size() >= kMaxFrameLen) {
            m_errCount++;
            emit frameParseError(QStringLiteral("$%1...<overflow>").arg(QString::fromUtf8(m_frameBuf.left(32))));
            m_frameBuf.clear();
            m_state = RxState::SkippingFrame;
            continue;
        }

        m_frameBuf.append(rawC);
    }
}

} // namespace qtfall
