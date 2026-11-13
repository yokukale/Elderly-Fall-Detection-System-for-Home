#ifndef FALL_JUDGE_ENGINE_H
#define FALL_JUDGE_ENGINE_H

#include "fall_types.h"
#include <QObject>

namespace qtfall {

class FallJudgeEngine : public QObject
{
    Q_OBJECT
public:
    explicit FallJudgeEngine(QObject *parent = nullptr);

    void setConfig(const FallJudgeConfig &cfg);
    FallJudgeConfig config() const { return m_cfg; }

    void resetAll(qint64 nowMs = 0);
    void processFrame(const FallRawFrame &frame, qint64 nowMs = 0);
    void onTick(qint64 nowMs);
    void mute(qint64 nowMs = 0);
    void resetAlarm(qint64 nowMs = 0);

    // 用于同步来自 STM32 串口日志或华为云影子的外部权威状态
    void applyExternalStatus(const Stm32LogEvent &evt, qint64 nowMs = 0);

    FallStatusSnapshot status() const { return m_status; }

signals:
    void statusChanged(const qtfall::FallStatusSnapshot &snapshot);
    void alarmTriggered(const qtfall::FallStatusSnapshot &snapshot);
    void alarmCleared(const qtfall::FallStatusSnapshot &snapshot);
    void visionOfflineChanged(bool offline);
    void cloudReportNeeded(const qtfall::FallStatusSnapshot &snapshot);

private:
    bool isFrameHit(const FallRawFrame &frame) const;
    void clearWindow();
    void updateWindowHits();
    qint64 currentHoldRemainingMs(qint64 nowMs) const;

    FallJudgeConfig m_cfg;
    FallStatusSnapshot m_status;
    QVector<bool> m_window;
    int m_winCursor = 0;
    qint64 m_stateEnterMs = 0;
    qint64 m_lastFrameMs = 0;
    bool m_hasSeenFrame = false;
};

} // namespace qtfall

#endif // FALL_JUDGE_ENGINE_H
