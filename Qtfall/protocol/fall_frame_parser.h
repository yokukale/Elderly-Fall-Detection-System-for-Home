#ifndef FALL_FRAME_PARSER_H
#define FALL_FRAME_PARSER_H

#include "fall_types.h"
#include <QByteArray>
#include <QObject>
#include <QVector>

namespace qtfall {

class FallFrameParser : public QObject
{
    Q_OBJECT
public:
    explicit FallFrameParser(QObject *parent = nullptr);

    void reset();
    void feedBytes(const QByteArray &data, qint64 nowMs = 0);

    static bool parseLegacyFrame(const QByteArray &body, FallRawFrame &out, qint64 nowMs = 0);
    static bool parseRobustFrame(const QByteArray &body, FallRawFrame &out, qint64 nowMs = 0);
    static bool parseStm32LogLine(const QString &line, Stm32LogEvent &out);

    static QByteArray formatLegacyFrame(int x, int y, int w, int h, bool isFall, double score);
    static QByteArray formatRobustFrame(int evt, int state, int cls, int scoreX100, int cx, int cy, int seq);
    static quint8 computeXor(const QByteArray &payloadBetweenDollarAndStar);

    quint64 okFrameCount() const { return m_okCount; }
    quint64 errorFrameCount() const { return m_errCount; }
    quint64 logLineCount() const { return m_logCount; }

signals:
    void frameParsed(const qtfall::FallRawFrame &frame);
    void stm32LogParsed(const qtfall::Stm32LogEvent &event);
    void rawLineReceived(const QString &line);
    void frameParseError(const QString &rawFragment);

private:
    enum class RxState {
        Idle,
        InLegacyFrame,
        InRobustFrame,
        SkippingFrame,
        InLogLine
    };

    void handleLineCompleted(const QByteArray &lineBytes);

    RxState m_state = RxState::Idle;
    QByteArray m_frameBuf;
    QByteArray m_lineBuf;
    quint64 m_okCount = 0;
    quint64 m_errCount = 0;
    quint64 m_logCount = 0;
};

} // namespace qtfall

#endif // FALL_FRAME_PARSER_H
