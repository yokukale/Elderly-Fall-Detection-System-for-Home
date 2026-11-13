#include "fall_simulator.h"
#include "fall_frame_parser.h"

#include <cmath>

namespace qtfall {

FallSimulator::FallSimulator(QObject *parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
{
    connect(m_timer, &QTimer::timeout, this, &FallSimulator::onTimer);
}

void FallSimulator::start(Scenario scenario, bool robustMode, int fps)
{
    m_scenario = scenario;
    m_robustMode = robustMode;
    m_tickIndex = 0;
    m_seq = 0;
    const int intervalMs = qBound(33, 1000 / qMax(1, fps), 1000);
    m_timer->start(intervalMs);
}

void FallSimulator::stop()
{
    m_timer->stop();
}

QByteArray FallSimulator::stepFrame()
{
    const int t = m_tickIndex++;
    int x = 260, y = 110, w = 100, h = 240;
    bool isFall = false;
    int cls = static_cast<int>(PoseClass::Standing);
    double score = 0.88;
    int evt = 0;
    QString desc;

    switch (m_scenario) {
    case Scenario::NormalWalk: {
        x = 80 + ((t * 14) % 440);
        y = 110 + static_cast<int>(std::sin(t * 0.4) * 8.0);
        w = 98;
        h = 236;
        isFall = false;
        cls = static_cast<int>(PoseClass::Standing);
        score = 0.86 + 0.05 * std::sin(t * 0.3);
        desc = QStringLiteral("T1 正常行走");
        break;
    }
    case Scenario::CrouchDebounce: {
        // 模拟蹲下系鞋带：第 4、5 帧出现短暂低置信度或窄框 Fall 误触发，其余为 Sitting
        x = 240;
        if (t % 15 == 4 || t % 15 == 5) {
            y = 220; w = 115; h = 140; // w < h 且 score 低，验证防抖拦截
            isFall = true;
            cls = static_cast<int>(PoseClass::Fall);
            score = 0.44;
            desc = QStringLiteral("T2 蹲下瞬态干扰帧(应被防抖过滤)");
        } else {
            y = 195; w = 128; h = 155;
            isFall = false;
            cls = static_cast<int>(PoseClass::Sitting);
            score = 0.84;
            desc = QStringLiteral("T2 缓慢蹲下/坐姿");
        }
        break;
    }
    case Scenario::QuickFall: {
        if (t < 4) {
            x = 250; y = 115 + t * 15; w = 105; h = 230 - t * 15;
            isFall = false;
            cls = static_cast<int>(PoseClass::Standing);
            score = 0.85;
            desc = QStringLiteral("T3 跌倒前站立");
        } else {
            x = 200; y = 305; w = 245; h = 115; // w > h
            isFall = true;
            cls = static_cast<int>(PoseClass::Fall);
            score = 0.88 + 0.03 * std::cos(t * 0.5);
            evt = (t == 4) ? 1 : 0;
            desc = QStringLiteral("T3 跌倒躺地 (w>h)");
        }
        break;
    }
    case Scenario::FallThenStandUp: {
        if (t >= 2 && t < 16) {
            x = 195; y = 300; w = 240; h = 118;
            isFall = true;
            cls = static_cast<int>(PoseClass::Fall);
            score = 0.86;
            evt = (t == 2) ? 1 : 0;
            desc = QStringLiteral("T4 跌倒保持阶段");
        } else {
            x = 265; y = 110; w = 96; h = 238;
            isFall = false;
            cls = static_cast<int>(PoseClass::Standing);
            score = 0.91;
            evt = (t == 16) ? 1 : 0;
            desc = (t < 2) ? QStringLiteral("T4 初始站立") : QStringLiteral("T4 自行爬起恢复");
        }
        break;
    }
    case Scenario::OfflineDrop: {
        if (t < 5) {
            x = 260; y = 115; w = 98; h = 232;
            isFall = false;
            cls = static_cast<int>(PoseClass::Standing);
            score = 0.89;
            desc = QStringLiteral("T8 断电前心跳帧");
        } else {
            stop();
            emit scenarioCompleted(QStringLiteral("T8 已停止发帧，等待 5 秒超时触发 K230 离线告警"));
            return {};
        }
        break;
    }
    }

    if (m_robustMode) {
        const int cx = (x + w / 2) * 100 / kK230FrameWidth;
        const int cy = (y + h / 2) * 100 / kK230FrameHeight;
        const int sx100 = static_cast<int>(std::round(qBound(0.0, score, 1.0) * 100.0));
        const QByteArray bytes = FallFrameParser::formatRobustFrame(evt, isFall ? 1 : 0, cls, sx100, cx, cy, m_seq++);
        emit frameGenerated(bytes, desc);
        return bytes;
    }

    const QByteArray bytes = FallFrameParser::formatLegacyFrame(x, y, w, h, isFall, score);
    emit frameGenerated(bytes, desc);
    return bytes;
}

void FallSimulator::onTimer()
{
    (void)stepFrame();
}

} // namespace qtfall
