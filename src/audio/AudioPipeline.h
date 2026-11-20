// SPDX-License-Identifier: MIT
#pragma once

#include "NoiseSuppressor.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QByteArray>
#include <QObject>

#include <memory>
#include <vector>

class QAudioSource;
class QAudioSink;
class QIODevice;

namespace lightcord::audio {

// Linear resampler for a mono float stream (streaming, keeps state between calls).
class LinearResampler {
public:
    void setRates(int inRate, int outRate);
    void process(const float *in, std::size_t count, std::vector<float> &out);
    void reset();

private:
    double m_step = 1.0; // input samples per output sample
    double m_pos = 0.0;
    float m_prev = 0.0f;
};

// Microphone capture pipeline:
//
//   QAudioSource (any device format) -> mono float -> 48 kHz -> int16
//     -> NoiseSuppressor -> [processedAudio signal] -> optional monitor sink
//
// The processed 48 kHz mono stream is what the voice engine will encode with
// Opus. Today it powers the "mic test" in the settings, so users can hear the
// effect of noise suppression before joining a call.
class AudioPipeline : public QObject {
    Q_OBJECT
public:
    explicit AudioPipeline(QObject *parent = nullptr);
    ~AudioPipeline() override;

    void setInputDevice(const QAudioDevice &device);
    void setOutputDevice(const QAudioDevice &device);
    void setSuppressorSettings(const NoiseSuppressor::Settings &settings);

    bool start();
    void stop();
    bool isRunning() const { return m_source != nullptr; }

    // Plays the processed microphone signal back on the output device.
    void setMonitorEnabled(bool enabled);
    bool monitorEnabled() const { return m_monitor; }

    void setMuted(bool muted) { m_muted = muted; }

signals:
    // Levels are normalised to 0..1 (from -60 dBFS to 0 dBFS).
    void levels(float inputLevel, float outputLevel, float voiceProbability, bool gateOpen);
    void processedAudio(const QByteArray &pcm48kMonoInt16);
    void errorOccurred(const QString &message);

private:
    void onReadyRead();
    void startSink();
    void stopSink();
    void writeToSink(const int16_t *samples, std::size_t count);

    QAudioDevice m_inputDevice;
    QAudioDevice m_outputDevice;
    QAudioFormat m_captureFormat;
    QAudioFormat m_sinkFormat;

    std::unique_ptr<QAudioSource> m_source;
    QIODevice *m_sourceIo = nullptr;
    std::unique_ptr<QAudioSink> m_sink;
    QIODevice *m_sinkIo = nullptr;

    NoiseSuppressor m_suppressor;
    LinearResampler m_captureResampler;
    LinearResampler m_sinkResampler;
    QByteArray m_pending; // incomplete frame bytes from the source

    bool m_monitor = false;
    bool m_muted = false;
};

} // namespace lightcord::audio
