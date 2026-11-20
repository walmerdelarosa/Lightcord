// SPDX-License-Identifier: MIT
#pragma once

#include "AudioPipeline.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QIODevice>

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>

class QAudioSink;

namespace lightcord::audio {

// Mixes the decoded audio of every remote speaker (48 kHz interleaved
// stereo int16, pushed per SSRC) and plays it on the output device in pull
// mode. Each source has a small jitter buffer: playback of a source starts
// once 40 ms are buffered and old audio is dropped beyond 200 ms, which keeps
// latency bounded when the network bursts.
class PlaybackMixer : public QIODevice {
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

    bool isSequential() const override { return true; }
    // The mixer is an endless generator (it produces silence when nobody
    // speaks). Qt's Windows audio sink only reads as many bytes as
    // bytesAvailable() reports, so this must not return 0.
    qint64 bytesAvailable() const override { return (qint64(1) << 24) + QIODevice::bytesAvailable(); }

protected:
    qint64 readData(char *data, qint64 maxSize) override;
    qint64 writeData(const char *, qint64) override { return -1; }

private:
    struct Source {
        std::deque<int16_t> samples; // interleaved stereo
        bool primed = false;
    };

    void mixInto(std::vector<int32_t> &acc, std::size_t frames);

    std::mutex m_mutex;
    std::map<uint32_t, Source> m_sources;
    std::unique_ptr<QAudioSink> m_sink;
    QAudioFormat m_format;
    LinearResampler m_resampleL;
    LinearResampler m_resampleR;
    std::vector<float> m_pendingL; // resampled samples not yet delivered
    std::vector<float> m_pendingR;
    bool m_muted = false;
    float m_volume = 1.0f;
};

} // namespace lightcord::audio
