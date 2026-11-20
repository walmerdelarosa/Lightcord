// SPDX-License-Identifier: MIT
#include "PlaybackMixer.h"

#include <QAudioSink>
#include <QMediaDevices>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lightcord::audio {

namespace {
constexpr int kRate = 48000;
constexpr std::size_t kPrimeFrames = kRate / 1000 * 40;  // 40 ms
constexpr std::size_t kMaxFrames = kRate / 1000 * 200;   // 200 ms

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
} // namespace

PlaybackMixer::PlaybackMixer(QObject *parent)
    : QIODevice(parent)
{
}

PlaybackMixer::~PlaybackMixer()
{
    stop();
}

bool PlaybackMixer::start(const QAudioDevice &device)
{
    stop();
    const QAudioDevice dev = device.isNull() ? QMediaDevices::defaultAudioOutput() : device;
    if (dev.isNull())
        return false;

    QAudioFormat f;
    f.setSampleRate(kRate);
    f.setChannelCount(2);
    f.setSampleFormat(QAudioFormat::Int16);
    m_format = dev.isFormatSupported(f) ? f : dev.preferredFormat();
    m_resampleL.setRates(kRate, m_format.sampleRate());
    m_resampleR.setRates(kRate, m_format.sampleRate());
    m_pendingL.clear();
    m_pendingR.clear();

    open(QIODevice::ReadOnly);
    m_sink = std::make_unique<QAudioSink>(dev, m_format);
    m_sink->setBufferSize(m_format.bytesForDuration(60000));
    m_sink->start(this);
    return true;
}

void PlaybackMixer::stop()
{
    if (m_sink) {
        m_sink->stop();
        m_sink.reset();
    }
    if (isOpen())
        close();
}

void PlaybackMixer::push(uint32_t source, const int16_t *stereo, std::size_t frames)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    Source &s = m_sources[source];
    s.samples.insert(s.samples.end(), stereo, stereo + frames * 2);
    const std::size_t buffered = s.samples.size() / 2;
    if (buffered > kMaxFrames) {
        const std::size_t drop = (buffered - kMaxFrames / 2) * 2;
        s.samples.erase(s.samples.begin(), s.samples.begin() + static_cast<std::ptrdiff_t>(drop));
    }
    if (!s.primed && s.samples.size() / 2 >= kPrimeFrames)
        s.primed = true;
}

void PlaybackMixer::removeSource(uint32_t source)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sources.erase(source);
}

void PlaybackMixer::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sources.clear();
}

void PlaybackMixer::mixInto(std::vector<int32_t> &acc, std::size_t frames)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto &entry : m_sources) {
        Source &s = entry.second;
        if (!s.primed)
            continue;
        const std::size_t available = std::min(frames, s.samples.size() / 2);
        for (std::size_t i = 0; i < available * 2; ++i)
            acc[i] += s.samples[i];
        s.samples.erase(s.samples.begin(), s.samples.begin() + static_cast<std::ptrdiff_t>(available * 2));
        if (s.samples.empty())
            s.primed = false; // underrun: re-buffer before playing again
    }
}

qint64 PlaybackMixer::readData(char *data, qint64 maxSize)
{
    const int channels = std::max(1, m_format.channelCount());
    const int bps = m_format.bytesPerSample();
    const qint64 frameBytes = static_cast<qint64>(channels) * bps;
    if (frameBytes <= 0 || maxSize < frameBytes)
        return 0;
    const std::size_t outFrames = static_cast<std::size_t>(maxSize / frameBytes);

    // How many 48 kHz frames are needed to produce `outFrames` device frames.
    const double ratio = static_cast<double>(kRate) / m_format.sampleRate();
    std::size_t needed = outFrames > m_pendingL.size()
                             ? static_cast<std::size_t>(std::ceil((outFrames - m_pendingL.size()) * ratio)) + 1
                             : 0;

    if (needed > 0) {
        std::vector<int32_t> acc(needed * 2, 0);
        mixInto(acc, needed);
        std::vector<float> l(needed), r(needed);
        const float gain = m_muted ? 0.0f : m_volume / 32768.0f;
        for (std::size_t i = 0; i < needed; ++i) {
            l[i] = static_cast<float>(acc[2 * i]) * gain;
            r[i] = static_cast<float>(acc[2 * i + 1]) * gain;
        }
        m_resampleL.process(l.data(), l.size(), m_pendingL);
        m_resampleR.process(r.data(), r.size(), m_pendingR);
    }

    const std::size_t frames = std::min(outFrames, std::min(m_pendingL.size(), m_pendingR.size()));
    char *p = data;
    for (std::size_t i = 0; i < frames; ++i) {
        const float left = m_pendingL[i];
        const float right = m_pendingR[i];
        for (int c = 0; c < channels; ++c) {
            float v = channels == 1 ? 0.5f * (left + right) : (c == 0 ? left : (c == 1 ? right : 0.0f));
            writeSample(p, m_format.sampleFormat(), v);
            p += bps;
        }
    }
    m_pendingL.erase(m_pendingL.begin(), m_pendingL.begin() + static_cast<std::ptrdiff_t>(frames));
    m_pendingR.erase(m_pendingR.begin(), m_pendingR.begin() + static_cast<std::ptrdiff_t>(frames));
    return static_cast<qint64>(frames) * frameBytes;
}

} // namespace lightcord::audio
