#ifndef FALL_SIMULATOR_H
#define FALL_SIMULATOR_H

#include "fall_types.h"
#include <QByteArray>
#include <QObject>
#include <QTimer>

namespace qtfall {

class FallSimulator : public QObject
{
    Q_OBJECT
public:
    enum class Scenario {
        NormalWalk = 0,      // T1: 正常行走
        CrouchDebounce,      // T2: 缓慢蹲下（防抖测试）
        QuickFall,           // T3: 快速跌倒并躺地
        FallThenStandUp,     // T4: 跌倒后自行爬起
        OfflineDrop          // T8: K230 断电掉线
    };

    explicit FallSimulator(QObject *parent = nullptr);

    void start(Scenario scenario, bool robustMode, int fps = 10);
    void stop();
    bool isRunning() const { return m_timer->isActive(); }
    Scenario currentScenario() const { return m_scenario; }

    QByteArray stepFrame();

signals:
    void frameGenerated(const QByteArray &rawBytes, const QString &description);
    void scenarioCompleted(const QString &summary);

private slots:
    void onTimer();

private:
    QTimer *m_timer = nullptr;
    Scenario m_scenario = Scenario::NormalWalk;
    bool m_robustMode = false;
    int m_tickIndex = 0;
    int m_seq = 0;
};

} // namespace qtfall

#endif // FALL_SIMULATOR_H
