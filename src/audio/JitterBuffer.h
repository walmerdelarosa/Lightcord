// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>

namespace lightcord::audio {

// Adaptive jitter buffer for one remote speaker (48 kHz interleaved stereo int16).
//
// Network packets arrive in bursts while the sound card consumes audio at a
// steady pace. The buffer waits until `targetMs` of audio is queued before
// playing ("priming"). If it runs dry while the speaker is still talking (new
// audio arrives shortly after it emptied), that was a real underrun, so the
// target grows by `growMs`, up to `maxTargetMs`. Long stable periods shrink
// it back. If too much audio piles up (clock drift or a burst after a stall)
// the oldest audio is dropped down to the target, keeping latency bounded.
//
// Qt-free and clock-agnostic (the caller passes the time), so it is unit-tested.
class JitterBuffer {
public:
    struct Config {
        int initialTargetMs = 60;
        int minTargetMs = 40;
        int maxTargetMs = 160;
        int growMs = 20;
        int shrinkAfterMs = 20000; // shrink by growMs after this long without underruns
        int maxExcessMs = 120;     // drop audio beyond target + maxExcessMs
        int underrunWindowMs = 150; // audio arriving this soon after emptying = underrun
    };

    static constexpr int kRate = 48000;
    static constexpr int kChannels = 2;

    JitterBuffer();
    explicit JitterBuffer(const Config &config);

    void push(const int16_t *stereo, std::size_t frames, int64_t nowMs);

    // Adds up to `frames` frames into `acc` (interleaved stereo). Returns frames mixed.
    std::size_t mixInto(int32_t *acc, std::size_t frames, int64_t nowMs);

    std::size_t bufferedFrames() const { return m_samples.size() / kChannels; }
    int targetMs() const { return m_targetMs; }
    bool isPlaying() const { return m_playing; }
    int underruns() const { return m_underruns; }
    int droppedFrames() const { return m_dropped; }

private:
    static std::size_t msToFrames(int ms) { return static_cast<std::size_t>(ms) * kRate / 1000; }

    Config m_cfg;
    std::deque<int16_t> m_samples;
    bool m_playing = false;
    int m_targetMs;
    int64_t m_emptiedAtMs = -1;
    int64_t m_lastUnderrunMs = 0;
    int m_underruns = 0;
    int m_dropped = 0;
};

} // namespace lightcord::audio
