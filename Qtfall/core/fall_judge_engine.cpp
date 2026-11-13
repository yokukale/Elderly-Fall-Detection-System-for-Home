#include "fall_judge_engine.h"

namespace qtfall {

FallJudgeEngine::FallJudgeEngine(QObject *parent)
    : QObject(parent)
{
    m_status.winLen = m_cfg.windowLength;
    m_window.fill(false, m_cfg.windowLength);
}

void FallJudgeEngine::setConfig(const FallJudgeConfig &cfg)
{
    FallJudgeConfig sanitized = cfg;
    sanitized.windowLength = qBound(1, sanitized.windowLength, 30);
    sanitized.hitThreshold = qBound(1, sanitized.hitThreshold, sanitized.windowLength);
    sanitized.scoreThreshold = qBound(0.0, sanitized.scoreThreshold, 1.0);
    sanitized.alarmHoldMs = qMax(1000, sanitized.alarmHoldMs);
    sanitized.cooldownMs = qMax(0, sanitized.cooldownMs);
    sanitized.offlineTimeoutMs = qMax(1000, sanitized.offlineTimeoutMs);

    const bool winLenChanged = (sanitized.windowLength != m_cfg.windowLength);
    m_cfg = sanitized;
    m_status.winLen = m_cfg.windowLength;

    if (winLenChanged) {
        clearWindow();
    }
    emit statusChanged(m_status);
}

void FallJudgeEngine::resetAll(qint64 nowMs)
{
    const quint32 preservedAlarms = m_status.alarmCount;
    m_status = FallStatusSnapshot{};
    m_status.winLen = m_cfg.windowLength;
    m_status.alarmCount = preservedAlarms;
    clearWindow();
    m_stateEnterMs = nowMs;
    m_lastFrameMs = nowMs;
    m_hasSeenFrame = false;
    emit statusChanged(m_status);
}

void FallJudgeEngine::clearWindow()
{
    m_window.fill(false, m_cfg.windowLength);
    m_winCursor = 0;
    m_status.winCount = 0;
    m_status.winHits = 0;
    m_status.windowSlots.clear();
}

void FallJudgeEngine::updateWindowHits()
{
    int hits = 0;
    QVector<bool> ordered;
    ordered.reserve(m_status.winCount);

    // 按时间顺序（最旧 -> 最新）生成窗口槽位快照
    const int start = (m_status.winCount < m_cfg.windowLength) ? 0 : m_winCursor;
    for (int i = 0; i < m_status.winCount; ++i) {
        const int idx = (start + i) % m_cfg.windowLength;
        const bool v = m_window.at(idx);
        if (v) {
            hits++;
        }
        ordered.append(v);
    }
    m_status.winHits = hits;
    m_status.windowSlots = ordered;
}

bool FallJudgeEngine::isFrameHit(const FallRawFrame &frame) const
{
    bool poseFall = frame.isFall;
    if (!poseFall && m_cfg.treatSideLyingAsFall && frame.poseClass == PoseClass::SideLying) {
        poseFall = true;
    }
    if (!poseFall) {
        return false;
    }
    if (frame.score < m_cfg.scoreThreshold) {
        return false;
    }
    if (m_cfg.geometryCheckEnabled) {
        if (frame.w > 0 && frame.h > 0 && frame.w <= frame.h) {
            return false;
        }
    }
    return true;
}

qint64 FallJudgeEngine::currentHoldRemainingMs(qint64 nowMs) const
{
    if (m_status.sysState == FallSysState::Alarm) {
        return qMax<qint64>(0, m_cfg.alarmHoldMs - (nowMs - m_stateEnterMs));
    }
    if (m_status.sysState == FallSysState::Cooldown) {
        return qMax<qint64>(0, m_cfg.cooldownMs - (nowMs - m_stateEnterMs));
    }
    return 0;
}

void FallJudgeEngine::processFrame(const FallRawFrame &frame, qint64 nowMs)
{
    const bool hit = isFrameHit(frame);

    m_lastFrameMs = nowMs;
    m_hasSeenFrame = true;

    bool offlineCleared = false;
    if (m_status.visionOffline) {
        m_status.visionOffline = false;
        offlineCleared = true;
        emit visionOfflineChanged(false);
    }

    m_status.x = frame.x;
    m_status.y = frame.y;
    m_status.w = frame.w;
    m_status.h = frame.h;
    m_status.confidence = frame.score;
    m_status.poseClass = frame.poseClass;

    bool triggeredNow = false;
    if (m_status.sysState == FallSysState::Idle) {
        m_window[m_winCursor] = hit;
        m_winCursor = (m_winCursor + 1) % m_cfg.windowLength;
        if (m_status.winCount < m_cfg.windowLength) {
            m_status.winCount++;
        }
        updateWindowHits();

        if (m_status.winHits >= m_cfg.hitThreshold) {
            m_status.sysState = FallSysState::Alarm;
            m_status.fallState = true;
            m_status.beepOn = true;
            m_status.alarmCount++;
            m_stateEnterMs = nowMs;
            triggeredNow = true;
        }
    }

    m_status.stateHoldRemainingMs = currentHoldRemainingMs(nowMs);
    emit statusChanged(m_status);

    if (triggeredNow) {
        emit alarmTriggered(m_status);
        emit cloudReportNeeded(m_status);
    } else if (offlineCleared) {
        emit cloudReportNeeded(m_status);
    }
}

void FallJudgeEngine::onTick(qint64 nowMs)
{
    bool changed = false;
    bool needCloud = false;
    bool clearedNow = false;

    if (m_hasSeenFrame && (nowMs - m_lastFrameMs) >= m_cfg.offlineTimeoutMs) {
        if (!m_status.visionOffline) {
            m_status.visionOffline = true;
            changed = true;
            needCloud = true;
            emit visionOfflineChanged(true);
        }
    }

    if (m_status.sysState == FallSysState::Alarm) {
        if ((nowMs - m_stateEnterMs) >= m_cfg.alarmHoldMs) {
            m_status.sysState = (m_cfg.cooldownMs > 0) ? FallSysState::Cooldown : FallSysState::Idle;
            m_status.fallState = false;
            m_status.beepOn = false;
            m_stateEnterMs = nowMs;
            clearWindow();
            changed = true;
            needCloud = true;
            clearedNow = true;
        } else {
            m_status.stateHoldRemainingMs = currentHoldRemainingMs(nowMs);
        }
    } else if (m_status.sysState == FallSysState::Cooldown) {
        if ((nowMs - m_stateEnterMs) >= m_cfg.cooldownMs) {
            m_status.sysState = FallSysState::Idle;
            m_status.stateHoldRemainingMs = 0;
            clearWindow();
            changed = true;
        } else {
            m_status.stateHoldRemainingMs = currentHoldRemainingMs(nowMs);
        }
    }

    if (changed) {
        m_status.stateHoldRemainingMs = currentHoldRemainingMs(nowMs);
        emit statusChanged(m_status);
    }
    if (clearedNow) {
        emit alarmCleared(m_status);
    }
    if (needCloud) {
        emit cloudReportNeeded(m_status);
    }
}

void FallJudgeEngine::mute(qint64 nowMs)
{
    if (m_status.beepOn || m_status.fallState) {
        m_status.beepOn = false;
        m_status.stateHoldRemainingMs = currentHoldRemainingMs(nowMs);
        emit statusChanged(m_status);
        emit cloudReportNeeded(m_status);
    }
}

void FallJudgeEngine::resetAlarm(qint64 nowMs)
{
    const bool wasAlarm = m_status.fallState || m_status.beepOn || m_status.sysState != FallSysState::Idle;
    m_status.sysState = FallSysState::Idle;
    m_status.fallState = false;
    m_status.beepOn = false;
    m_status.stateHoldRemainingMs = 0;
    clearWindow();
    m_stateEnterMs = nowMs;
    emit statusChanged(m_status);
    if (wasAlarm) {
        emit alarmCleared(m_status);
        emit cloudReportNeeded(m_status);
    }
}

void FallJudgeEngine::applyExternalStatus(const Stm32LogEvent &evt, qint64 nowMs)
{
    if (!evt.hasStatus) {
        return;
    }

    const bool prevFall = m_status.fallState;
    const bool prevOffline = m_status.visionOffline;

    if (evt.fallState >= 0) {
        m_status.fallState = (evt.fallState != 0);
        if (m_status.fallState) {
            m_status.sysState = FallSysState::Alarm;
            if (!prevFall) {
                m_status.alarmCount++;
                m_stateEnterMs = nowMs;
            }
        } else if (evt.type == Stm32LogEvent::Type::AlarmHoldExpired) {
            m_status.sysState = FallSysState::Cooldown;
            m_stateEnterMs = nowMs;
            clearWindow();
        } else if (evt.type == Stm32LogEvent::Type::CooldownFinished || evt.type == Stm32LogEvent::Type::Reset) {
            m_status.sysState = FallSysState::Idle;
            clearWindow();
        } else {
            m_status.sysState = FallSysState::Idle;
        }
    }

    if (evt.beepOn >= 0) {
        m_status.beepOn = (evt.beepOn != 0);
    }
    if (evt.visionOffline >= 0) {
        m_status.visionOffline = (evt.visionOffline != 0);
    }
    if (evt.confidence >= 0.0) {
        m_status.confidence = evt.confidence;
    }
    if (evt.hasBbox) {
        m_status.x = evt.bbox.x();
        m_status.y = evt.bbox.y();
        m_status.w = evt.bbox.width();
        m_status.h = evt.bbox.height();
    }
    if (evt.winHits >= 0) {
        m_status.winHits = evt.winHits;
    }

    m_status.stateHoldRemainingMs = currentHoldRemainingMs(nowMs);
    emit statusChanged(m_status);

    if (m_status.visionOffline != prevOffline) {
        emit visionOfflineChanged(m_status.visionOffline);
    }
    if (!prevFall && m_status.fallState) {
        emit alarmTriggered(m_status);
    } else if (prevFall && !m_status.fallState) {
        emit alarmCleared(m_status);
    }
}

} // namespace qtfall
