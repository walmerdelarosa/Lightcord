// SPDX-License-Identifier: MIT
#include "PlaybackMixer.h"

#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lightcord::audio {

namespace {
constexpr int kRate = 48000;
constexpr int kTickMs = 10;
constexpr int kDeviceTargetMs = 40;  // audio kept queued in the sound card
constexpr int kDeviceBufferMs = 120; // device buffer capacity

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
    : QObject(parent)
    , m_timer(this)
{
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setInterval(kTickMs);
    connect(&m_timer, &QTimer::timeout, this, &PlaybackMixer::pump);
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

    m_sink = std::make_unique<QAudioSink>(dev, m_format);
    m_sink->setBufferSize(m_format.bytesForDuration(kDeviceBufferMs * 1000));
    m_io = m_sink->start();
    if (!m_io) {
        m_sink.reset();
        return false;
    }
    m_clock.start();
    pump(); // prefill with silence so the device starts immediately
    m_timer.start();
    return true;
}

void PlaybackMixer::stop()
{
    m_timer.stop();
    if (m_sink) {
        m_sink->stop();
        m_sink.reset();
    }
    m_io = nullptr;
}

void PlaybackMixer::push(uint32_t source, const int16_t *stereo, std::size_t frames)
{
    m_sources[source].push(stereo, frames, m_clock.isValid() ? m_clock.elapsed() : 0);
}

void PlaybackMixer::removeSource(uint32_t source)
{
    m_sources.erase(source);
}

void PlaybackMixer::clear()
{
    m_sources.clear();
}

QString PlaybackMixer::stats() const
{
    QStringList parts;
    for (const auto &entry : m_sources) {
        const JitterBuffer &jb = entry.second;
        parts << QStringLiteral("ssrc %1: target %2ms, buffered %3ms, underruns %4, dropped %5ms")
                     .arg(entry.first)
                     .arg(jb.targetMs())
                     .arg(jb.bufferedFrames() / 48)
                     .arg(jb.underruns())
                     .arg(jb.droppedFrames() / 48);
    }
    return QStringLiteral("device underruns %1; %2").arg(m_deviceUnderruns).arg(parts.join(QStringLiteral(" | ")));
}

void PlaybackMixer::render(std::size_t frames48k)
{
    std::vector<int32_t> acc(frames48k * 2, 0);
    const int64_t now = m_clock.elapsed();
    for (auto &entry : m_sources)
        entry.second.mixInto(acc.data(), frames48k, now);

    std::vector<float> l(frames48k), r(frames48k);
    const float gain = m_muted ? 0.0f : m_volume / 32768.0f;
    for (std::size_t i = 0; i < frames48k; ++i) {
        l[i] = static_cast<float>(acc[2 * i]) * gain;
        r[i] = static_cast<float>(acc[2 * i + 1]) * gain;
    }
    m_resampleL.process(l.data(), l.size(), m_pendingL);
    m_resampleR.process(r.data(), r.size(), m_pendingR);
}

void PlaybackMixer::writePending()
{
    const int channels = std::max(1, m_format.channelCount());
    const int bps = m_format.bytesPerSample();
    const std::size_t frames = std::min(m_pendingL.size(), m_pendingR.size());
    m_outBytes.resize(static_cast<qsizetype>(frames * channels * bps));
    char *p = m_outBytes.data();
    for (std::size_t i = 0; i < frames; ++i) {
        const float left = m_pendingL[i];
        const float right = m_pendingR[i];
        for (int c = 0; c < channels; ++c) {
            const float v = channels == 1 ? 0.5f * (left + right) : (c == 0 ? left : (c == 1 ? right : 0.0f));
            writeSample(p, m_format.sampleFormat(), v);
            p += bps;
        }
    }
    const qint64 written = m_io->write(m_outBytes);
    const qint64 bytesPerFrame = static_cast<qint64>(channels) * bps;
    const std::size_t writtenFrames = written > 0 ? static_cast<std::size_t>(written / bytesPerFrame) : 0;
    m_pendingL.erase(m_pendingL.begin(), m_pendingL.begin() + static_cast<std::ptrdiff_t>(writtenFrames));
    m_pendingR.erase(m_pendingR.begin(), m_pendingR.begin() + static_cast<std::ptrdiff_t>(writtenFrames));
}

void PlaybackMixer::pump()
{
    if (!m_sink || !m_io)
        return;

    const qsizetype capacity = m_sink->bufferSize();
    const qsizetype freeBytes = m_sink->bytesFree();
    const qint64 queuedUs = m_format.durationForBytes(static_cast<qint32>(std::max<qsizetype>(0, capacity - freeBytes)));
    if (queuedUs <= 0 && m_clock.elapsed() > 200)
        ++m_deviceUnderruns;

    const qint64 missingUs = static_cast<qint64>(kDeviceTargetMs) * 1000 - queuedUs;
    if (missingUs <= 0)
        return;

    // Device frames still needed, minus what is already resampled and waiting.
    const qint64 deviceFrames = m_format.framesForDuration(missingUs);
    const qint64 alreadyPending = static_cast<qint64>(std::min(m_pendingL.size(), m_pendingR.size()));
    const qint64 neededDevice = deviceFrames - alreadyPending;
    if (neededDevice > 0) {
        const double ratio = static_cast<double>(kRate) / m_format.sampleRate();
        render(static_cast<std::size_t>(std::ceil(neededDevice * ratio)));
    }

    // Never write more than the device can take right now.
    const int bytesPerFrame = std::max(1, m_format.bytesPerFrame());
    const std::size_t maxFrames = static_cast<std::size_t>(m_sink->bytesFree() / bytesPerFrame);
    if (m_pendingL.size() > maxFrames) {
        // Keep the remainder for the next tick.
        std::vector<float> keepL(m_pendingL.begin() + static_cast<std::ptrdiff_t>(maxFrames), m_pendingL.end());
        std::vector<float> keepR(m_pendingR.begin() + static_cast<std::ptrdiff_t>(maxFrames), m_pendingR.end());
        m_pendingL.resize(maxFrames);
        m_pendingR.resize(maxFrames);
        writePending();
        keepL.insert(keepL.begin(), m_pendingL.begin(), m_pendingL.end());
        keepR.insert(keepR.begin(), m_pendingR.begin(), m_pendingR.end());
        m_pendingL.swap(keepL);
        m_pendingR.swap(keepR);
    } else {
        writePending();
    }
}

} // namespace lightcord::audio
