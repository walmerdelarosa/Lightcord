// SPDX-License-Identifier: MIT
#include "NoiseSuppressor.h"

#include <algorithm>
#include <cmath>

extern "C" {
#include "rnnoise.h"
}

namespace lightcord::audio {

namespace {
constexpr float kGateAttack = 0.5f;   // per-frame gain step when opening (fast)
constexpr float kGateRelease = 0.08f; // per-frame gain step when closing (smooth)
constexpr float kGateFloor = 0.0f;    // gain while closed

inline int16_t clampToInt16(float v)
{
    if (v > 32767.0f)
        return 32767;
    if (v < -32768.0f)
        return -32768;
    return static_cast<int16_t>(std::lrintf(v));
}
} // namespace

NoiseSuppressor::NoiseSuppressor()
{
    m_state = rnnoise_create(nullptr);
    m_inFifo.reserve(kFrameSize);
    reset();
}

NoiseSuppressor::~NoiseSuppressor()
{
    if (m_state)
        rnnoise_destroy(m_state);
}

void NoiseSuppressor::setSettings(const Settings &settings)
{
    m_settings = settings;
    m_settings.strength = std::clamp(m_settings.strength, 0.0f, 1.0f);
    m_settings.gateThreshold = std::clamp(m_settings.gateThreshold, 0.0f, 1.0f);
    m_settings.gateHoldMs = std::max(0, m_settings.gateHoldMs);
}

void NoiseSuppressor::reset()
{
    m_inFifo.clear();
    // Prime the output FIFO with one frame of silence so that process()
    // can always return as many samples as it receives.
    m_outFifo.assign(kFrameSize, 0);
    m_lastVad = 0.0f;
    m_gateGain = 1.0f;
    m_holdFramesLeft = 0;
}

void NoiseSuppressor::processFrame(const float *in, float *out)
{
    const Settings &s = m_settings;

    if (!s.enabled || !m_state) {
        std::copy(in, in + kFrameSize, out);
        m_lastVad = 1.0f;
        m_gateGain = 1.0f;
        return;
    }

    // RNNoise expects float samples in the int16 range.
    float denoised[kFrameSize];
    m_lastVad = rnnoise_process_frame(m_state, denoised, in);

    // Voice gate with hold time and attack/release smoothing.
    float targetGain = 1.0f;
    if (s.voiceGate) {
        const int holdFrames = s.gateHoldMs / 10;
        if (m_lastVad >= s.gateThreshold) {
            m_holdFramesLeft = holdFrames;
        } else if (m_holdFramesLeft > 0) {
            --m_holdFramesLeft;
        }
        targetGain = (m_lastVad >= s.gateThreshold || m_holdFramesLeft > 0) ? 1.0f : kGateFloor;
    }

    const float startGain = m_gateGain;
    float endGain = startGain;
    if (targetGain > startGain)
        endGain = std::min(targetGain, startGain + kGateAttack);
    else if (targetGain < startGain)
        endGain = std::max(targetGain, startGain - kGateRelease);
    m_gateGain = endGain;

    const float wet = s.strength;
    const float dry = 1.0f - wet;
    for (int i = 0; i < kFrameSize; ++i) {
        // Interpolate the gain across the frame to avoid clicks.
        const float t = static_cast<float>(i) / kFrameSize;
        const float gain = startGain + (endGain - startGain) * t;
        out[i] = (wet * denoised[i] + dry * in[i]) * gain;
    }
}

void NoiseSuppressor::process(int16_t *samples, std::size_t count)
{
    float frameOut[kFrameSize];

    for (std::size_t i = 0; i < count; ++i) {
        m_inFifo.push_back(static_cast<float>(samples[i]));
        if (m_inFifo.size() == static_cast<std::size_t>(kFrameSize)) {
            processFrame(m_inFifo.data(), frameOut);
            m_inFifo.clear();
            for (float v : frameOut)
                m_outFifo.push_back(clampToInt16(v));
        }
    }

    // The FIFO always holds at least `count` samples thanks to the priming
    // frame, because every kFrameSize input samples produce kFrameSize output.
    const std::size_t available = std::min(count, m_outFifo.size());
    std::copy(m_outFifo.begin(), m_outFifo.begin() + static_cast<std::ptrdiff_t>(available), samples);
    m_outFifo.erase(m_outFifo.begin(), m_outFifo.begin() + static_cast<std::ptrdiff_t>(available));
    if (available < count)
        std::fill(samples + available, samples + count, int16_t{0});
}

} // namespace lightcord::audio
