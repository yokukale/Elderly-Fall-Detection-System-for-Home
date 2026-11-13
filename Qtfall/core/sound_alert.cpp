#include "sound_alert.h"

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

#include <QtEndian>
#include <cmath>

namespace qtfall {

SoundAlert::SoundAlert(QObject *parent)
    : QObject(parent)
    , m_wavBuffer(synthesizeAlertWav(900, 22050))
{
}

SoundAlert::~SoundAlert()
{
    stopLoop();
}

void SoundAlert::setEnabled(bool enabled)
{
    m_enabled = enabled;
    if (!m_enabled) {
        stopLoop();
    }
}

void SoundAlert::setAlarmPlaying(bool active)
{
    if (!active || !m_enabled) {
        stopLoop();
        return;
    }
    if (!m_playing) {
        startLoop();
    }
}

void SoundAlert::playTestChime()
{
    if (!m_enabled || m_wavBuffer.isEmpty()) {
        return;
    }
#ifdef Q_OS_WIN
    PlaySoundW(reinterpret_cast<LPCWSTR>(m_wavBuffer.constData()),
               nullptr,
               SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
#endif
}

void SoundAlert::startLoop()
{
    if (m_wavBuffer.isEmpty()) {
        return;
    }
    m_playing = true;
#ifdef Q_OS_WIN
    PlaySoundW(reinterpret_cast<LPCWSTR>(m_wavBuffer.constData()),
               nullptr,
               SND_MEMORY | SND_ASYNC | SND_LOOP | SND_NODEFAULT);
#endif
}

void SoundAlert::stopLoop()
{
    if (m_playing) {
        m_playing = false;
#ifdef Q_OS_WIN
        PlaySoundW(nullptr, nullptr, 0);
#endif
    }
}

QByteArray SoundAlert::synthesizeAlertWav(int durationMs, int sampleRate)
{
    const int numSamples = qMax(1, (durationMs * sampleRate) / 1000);
    const int dataBytes = numSamples * 2; // 16-bit mono PCM
    const int fileBytes = 44 + dataBytes;

    QByteArray wav;
    wav.resize(fileBytes);
    char *p = wav.data();

    auto writeU32 = [&](int offset, quint32 v) { qToLittleEndian(v, p + offset); };
    auto writeU16 = [&](int offset, quint16 v) { qToLittleEndian(v, p + offset); };

    memcpy(p + 0, "RIFF", 4);
    writeU32(4, static_cast<quint32>(fileBytes - 8));
    memcpy(p + 8, "WAVE", 4);
    memcpy(p + 12, "fmt ", 4);
    writeU32(16, 16);
    writeU16(20, 1); // PCM
    writeU16(22, 1); // Mono
    writeU32(24, static_cast<quint32>(sampleRate));
    writeU32(28, static_cast<quint32>(sampleRate * 2));
    writeU16(32, 2);
    writeU16(34, 16);
    memcpy(p + 36, "data", 4);
    writeU32(40, static_cast<quint32>(dataBytes));

    constexpr double kTwoPi = 6.283185307179586;
    for (int i = 0; i < numSamples; ++i) {
        const double sec = static_cast<double>(i) / sampleRate;
        // 双音急促蜂鸣节奏：0~0.25s 880Hz, 0.30~0.55s 660Hz, 0.55~0.90s 静音
        double amp = 0.0;
        double freq = 880.0;
        if (sec < 0.24) {
            freq = 880.0;
            amp = 0.32;
        } else if (sec >= 0.30 && sec < 0.54) {
            freq = 660.0;
            amp = 0.32;
        }
        const qint16 sample = static_cast<qint16>(amp * 32767.0 * std::sin(kTwoPi * freq * sec));
        qToLittleEndian(sample, p + 44 + i * 2);
    }

    return wav;
}

} // namespace qtfall
