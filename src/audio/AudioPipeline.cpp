// SPDX-License-Identifier: MIT
#include "AudioPipeline.h"

#include <QAudioSink>
#include <QAudioSource>
#include <QIODevice>
#include <QMediaDevices>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lightcord::audio {

namespace {

float levelFromRms(double rms)
{
    if (rms <= 1e-9)
        return 0.0f;
    const double db = 20.0 * std::log10(rms);
    return static_cast<float>(std::clamp((db + 60.0) / 60.0, 0.0, 1.0));
}

double rmsInt16(const int16_t *s, std::size_t n)
{
    if (!n)
        return 0.0;
    double acc = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double v = s[i] / 32768.0;
        acc += v * v;
    }
    return std::sqrt(acc / static_cast<double>(n));
}

float readSample(const char *p, QAudioFormat::SampleFormat fmt)
{
    switch (fmt) {
    case QAudioFormat::UInt8:
        return (static_cast<int>(*reinterpret_cast<const quint8 *>(p)) - 128) / 128.0f;
    case QAudioFormat::Int16: {
        qint16 v;
        std::memcpy(&v, p, sizeof v);
        return v / 32768.0f;
    }
    case QAudioFormat::Int32: {
        qint32 v;
        std::memcpy(&v, p, sizeof v);
        return static_cast<float>(v / 2147483648.0);
    }
    case QAudioFormat::Float: {
        float v;
        std::memcpy(&v, p, sizeof v);
        return v;
    }
    default:
        return 0.0f;
    }
}

void writeSample(char *p, QAudioFormat::SampleFormat fmt, float v)
{
    v = std::clamp(v, -1.0f, 1.0f);
    switch (fmt) {
    case QAudioFormat::UInt8: {
        const quint8 s = static_cast<quint8>(std::lrintf(v * 127.0f) + 128);
        std::memcpy(p, &s, sizeof s);
        break;
    }
    case QAudioFormat::Int16: {
        const qint16 s = static_cast<qint16>(std::lrintf(v * 32767.0f));
        std::memcpy(p, &s, sizeof s);
        break;
    }
    case QAudioFormat::Int32: {
        const qint32 s = static_cast<qint32>(std::lrint(v * 2147483647.0));
        std::memcpy(p, &s, sizeof s);
        break;
    }
    case QAudioFormat::Float:
        std::memcpy(p, &v, sizeof v);
        break;
    default:
        break;
    }
}

QAudioFormat desiredFormat()
{
    QAudioFormat f;
    f.setSampleRate(NoiseSuppressor::kSampleRate);
    f.setChannelCount(1);
    f.setSampleFormat(QAudioFormat::Int16);
    return f;
}

} // namespace

// ---------------------------------------------------------------------------
// LinearResampler
// ---------------------------------------------------------------------------

void LinearResampler::setRates(int inRate, int outRate)
{
    m_step = (inRate > 0 && outRate > 0) ? static_cast<double>(inRate) / outRate : 1.0;
    reset();
}

void LinearResampler::reset()
{
    m_pos = 0.0;
    m_prev = 0.0f;
}

void LinearResampler::process(const float *in, std::size_t count, std::vector<float> &out)
{
    if (m_step == 1.0) {
        out.insert(out.end(), in, in + count);
        return;
    }
    // Interpolate between the previous sample (t = 0) and the current one (t = 1).
    for (std::size_t i = 0; i < count; ++i) {
        const float cur = in[i];
        while (m_pos < 1.0) {
            out.push_back(m_prev + (cur - m_prev) * static_cast<float>(m_pos));
            m_pos += m_step;
        }
        m_pos -= 1.0;
        m_prev = cur;
    }
}

// ---------------------------------------------------------------------------
// AudioPipeline
// ---------------------------------------------------------------------------

AudioPipeline::AudioPipeline(QObject *parent)
    : QObject(parent)
    , m_inputDevice(QMediaDevices::defaultAudioInput())
    , m_outputDevice(QMediaDevices::defaultAudioOutput())
{
}

AudioPipeline::~AudioPipeline()
{
    stop();
}

void AudioPipeline::setInputDevice(const QAudioDevice &device)
{
    const bool wasRunning = isRunning();
    if (wasRunning)
        stop();
    m_inputDevice = device.isNull() ? QMediaDevices::defaultAudioInput() : device;
    if (wasRunning)
        start();
}

void AudioPipeline::setOutputDevice(const QAudioDevice &device)
{
    m_outputDevice = device.isNull() ? QMediaDevices::defaultAudioOutput() : device;
    if (m_sink) {
        stopSink();
        startSink();
    }
}

void AudioPipeline::setSuppressorSettings(const NoiseSuppressor::Settings &settings)
{
    m_suppressor.setSettings(settings);
}

bool AudioPipeline::start()
{
    stop();
    if (m_inputDevice.isNull()) {
        emit errorOccurred(tr("No microphone was found."));
        return false;
    }

    m_captureFormat = desiredFormat();
    if (!m_inputDevice.isFormatSupported(m_captureFormat))
        m_captureFormat = m_inputDevice.preferredFormat();

    m_captureResampler.setRates(m_captureFormat.sampleRate(), NoiseSuppressor::kSampleRate);
    m_suppressor.reset();
    m_pending.clear();

    m_source = std::make_unique<QAudioSource>(m_inputDevice, m_captureFormat);
    // ~20 ms of buffering keeps latency low without starving the device.
    m_source->setBufferSize(m_captureFormat.bytesForDuration(20000) * 2);
    m_sourceIo = m_source->start();
    if (!m_sourceIo) {
        emit errorOccurred(tr("Could not open the microphone \"%1\".").arg(m_inputDevice.description()));
        m_source.reset();
        return false;
    }
    connect(m_sourceIo, &QIODevice::readyRead, this, &AudioPipeline::onReadyRead);

    if (m_monitor)
        startSink();
    return true;
}

void AudioPipeline::stop()
{
    stopSink();
    if (m_source) {
        m_source->stop();
        m_source.reset();
    }
    m_sourceIo = nullptr;
    m_pending.clear();
}

void AudioPipeline::setMonitorEnabled(bool enabled)
{
    m_monitor = enabled;
    if (!isRunning())
        return;
    if (enabled)
        startSink();
    else
        stopSink();
}

void AudioPipeline::startSink()
{
    if (m_sink || m_outputDevice.isNull())
        return;
    m_sinkFormat = desiredFormat();
    if (!m_outputDevice.isFormatSupported(m_sinkFormat))
        m_sinkFormat = m_outputDevice.preferredFormat();
    m_sinkResampler.setRates(NoiseSuppressor::kSampleRate, m_sinkFormat.sampleRate());

    m_sink = std::make_unique<QAudioSink>(m_outputDevice, m_sinkFormat);
    m_sink->setBufferSize(m_sinkFormat.bytesForDuration(60000));
    m_sinkIo = m_sink->start();
    if (!m_sinkIo) {
        emit errorOccurred(tr("Could not open the output device \"%1\".").arg(m_outputDevice.description()));
        m_sink.reset();
    }
}

void AudioPipeline::stopSink()
{
    if (m_sink) {
        m_sink->stop();
        m_sink.reset();
    }
    m_sinkIo = nullptr;
}

void AudioPipeline::writeToSink(const int16_t *samples, std::size_t count)
{
    if (!m_sink || !m_sinkIo)
        return;

    std::vector<float> mono;
    mono.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
        mono.push_back(samples[i] / 32768.0f);

    std::vector<float> resampled;
    resampled.reserve(count * 2);
    m_sinkResampler.process(mono.data(), mono.size(), resampled);

    const int channels = std::max(1, m_sinkFormat.channelCount());
    const int bps = m_sinkFormat.bytesPerSample();
    QByteArray out(static_cast<qsizetype>(resampled.size()) * channels * bps, Qt::Uninitialized);
    char *p = out.data();
    for (float v : resampled) {
        for (int c = 0; c < channels; ++c) {
            writeSample(p, m_sinkFormat.sampleFormat(), v);
            p += bps;
        }
    }

    // Drop audio instead of accumulating latency if the device falls behind.
    if (m_sink->bytesFree() >= out.size())
        m_sinkIo->write(out);
}

void AudioPipeline::onReadyRead()
{
    if (!m_sourceIo)
        return;
    m_pending.append(m_sourceIo->readAll());

    const int frameBytes = m_captureFormat.bytesPerFrame();
    const int bps = m_captureFormat.bytesPerSample();
    const int channels = std::max(1, m_captureFormat.channelCount());
    if (frameBytes <= 0)
        return;

    const qsizetype frames = m_pending.size() / frameBytes;
    if (frames == 0)
        return;

    // Down-mix to mono float.
    std::vector<float> mono(static_cast<std::size_t>(frames));
    const char *p = m_pending.constData();
    for (qsizetype f = 0; f < frames; ++f) {
        float acc = 0.0f;
        for (int c = 0; c < channels; ++c)
            acc += readSample(p + c * bps, m_captureFormat.sampleFormat());
        mono[static_cast<std::size_t>(f)] = acc / channels;
        p += frameBytes;
    }
    m_pending.remove(0, frames * frameBytes);

    // Resample to 48 kHz.
    std::vector<float> at48k;
    at48k.reserve(mono.size() * 2);
    m_captureResampler.process(mono.data(), mono.size(), at48k);

    std::vector<int16_t> pcm(at48k.size());
    for (std::size_t i = 0; i < at48k.size(); ++i)
        pcm[i] = static_cast<int16_t>(std::lrintf(std::clamp(at48k[i], -1.0f, 1.0f) * 32767.0f));

    const double inRms = rmsInt16(pcm.data(), pcm.size());
    m_suppressor.process(pcm.data(), pcm.size());
    if (m_muted)
        std::fill(pcm.begin(), pcm.end(), int16_t{0});
    const double outRms = rmsInt16(pcm.data(), pcm.size());

    emit levels(levelFromRms(inRms), levelFromRms(outRms), m_suppressor.voiceProbability(), m_suppressor.gateOpen());
    emit processedAudio(QByteArray(reinterpret_cast<const char *>(pcm.data()),
                                   static_cast<qsizetype>(pcm.size() * sizeof(int16_t))));

    if (m_monitor)
        writeToSink(pcm.data(), pcm.size());
}

} // namespace lightcord::audio
