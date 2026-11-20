// SPDX-License-Identifier: MIT
#include "JitterBuffer.h"

#include <algorithm>

namespace lightcord::audio {

JitterBuffer::JitterBuffer()
    : JitterBuffer(Config{})
{
}

JitterBuffer::JitterBuffer(const Config &config)
    : m_cfg(config)
    , m_targetMs(config.initialTargetMs)
{
}

void JitterBuffer::push(const int16_t *stereo, std::size_t frames, int64_t nowMs)
{
    if (frames == 0)
        return;

    // Ran dry a moment ago and the speaker is still talking: a genuine underrun.
    if (!m_playing && m_emptiedAtMs >= 0 && nowMs - m_emptiedAtMs <= m_cfg.underrunWindowMs) {
        ++m_underruns;
        m_targetMs = std::min(m_cfg.maxTargetMs, m_targetMs + m_cfg.growMs);
        m_lastUnderrunMs = nowMs;
    }
    m_emptiedAtMs = -1;

    // Slowly give latency back after a long stable period.
    if (m_lastUnderrunMs == 0)
        m_lastUnderrunMs = nowMs;
    if (nowMs - m_lastUnderrunMs > m_cfg.shrinkAfterMs && m_targetMs > m_cfg.minTargetMs) {
        m_targetMs = std::max(m_cfg.minTargetMs, m_targetMs - m_cfg.growMs);
        m_lastUnderrunMs = nowMs;
    }

    m_samples.insert(m_samples.end(), stereo, stereo + frames * kChannels);

    const std::size_t limit = msToFrames(m_targetMs + m_cfg.maxExcessMs);
    if (bufferedFrames() > limit) {
        const std::size_t drop = bufferedFrames() - msToFrames(m_targetMs);
        m_samples.erase(m_samples.begin(), m_samples.begin() + static_cast<std::ptrdiff_t>(drop * kChannels));
        m_dropped += static_cast<int>(drop);
    }

    if (!m_playing && bufferedFrames() >= msToFrames(m_targetMs))
        m_playing = true;
}

std::size_t JitterBuffer::mixInto(int32_t *acc, std::size_t frames, int64_t nowMs)
{
    if (!m_playing)
        return 0;
    const std::size_t n = std::min(frames, bufferedFrames());
    for (std::size_t i = 0; i < n * kChannels; ++i)
        acc[i] += m_samples[i];
    m_samples.erase(m_samples.begin(), m_samples.begin() + static_cast<std::ptrdiff_t>(n * kChannels));
    if (m_samples.empty()) {
        m_playing = false;
        m_emptiedAtMs = nowMs;
    }
    return n;
}

} // namespace lightcord::audio
