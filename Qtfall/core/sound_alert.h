#ifndef SOUND_ALERT_H
#define SOUND_ALERT_H

#include <QByteArray>
#include <QObject>

namespace qtfall {

class SoundAlert : public QObject
{
    Q_OBJECT
public:
    explicit SoundAlert(QObject *parent = nullptr);
    ~SoundAlert() override;

    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

    void setAlarmPlaying(bool active);
    bool isAlarmPlaying() const { return m_playing; }
    void playTestChime();

    static QByteArray synthesizeAlertWav(int durationMs = 900, int sampleRate = 22050);

private:
    void startLoop();
    void stopLoop();

    QByteArray m_wavBuffer;
    bool m_enabled = true;
    bool m_playing = false;
};

} // namespace qtfall

#endif // SOUND_ALERT_H
