// SPDX-License-Identifier: MIT
#pragma once

#include "AudioPipeline.h"
#include "JitterBuffer.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

class QAudioSink;
class QIODevice;

namespace lightcord::audio {

// Mixes the decoded audio of every remote speaker (48 kHz interleaved stereo
// int16, pushed per SSRC) and plays it on the output device.
//
// Each speaker has an adaptive JitterBuffer. A precise 10 ms timer keeps the
// device buffer topped up to ~40 ms in push mode, so the jitter buffers are
// drained in small, regular steps (Qt's pull mode on Windows polls with the
// coarse system timer and takes 30-45 ms chunks, which caused audible gaps).
class PlaybackMixer : public QObject {
    Q_OBJECT
public:
    explicit PlaybackMixer(QObject *parent = nullptr);
    ~PlaybackMixer() override;

    bool start(const QAudioDevice &device);
    void stop();

    void push(uint32_t source, const int16_t *stereo, std::size_t frames);
    void removeSource(uint32_t source);
    void clear();
    void setMuted(bool muted) { m_muted = muted; }
    void setVolume(float volume) { m_volume = volume; }

    // Summary for the diagnostics log.
    QString stats() const;

private:
    void pump();
    void render(std::size_t frames48k);
    void writePending();

    std::map<uint32_t, JitterBuffer> m_sources;
    std::unique_ptr<QAudioSink> m_sink;
    QIODevice *m_io = nullptr;
    QAudioFormat m_format;
    QTimer m_timer;
    QElapsedTimer m_clock;

    LinearResampler m_resampleL;
    LinearResampler m_resampleR;
    std::vector<float> m_pendingL; // device-rate samples not yet written
    std::vector<float> m_pendingR;
    QByteArray m_outBytes;

    bool m_muted = false;
    float m_volume = 1.0f;
    int m_deviceUnderruns = 0;
};

} // namespace lightcord::audio
