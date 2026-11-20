// SPDX-License-Identifier: MIT
// Lightcord - lightweight native Discord client
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

struct DenoiseState;

namespace lightcord::audio {

// Real-time noise suppression for 48 kHz mono 16-bit PCM, built on RNNoise.
//
// RNNoise works on fixed 10 ms frames (480 samples). This class accepts
// chunks of any size, keeps a small FIFO and always returns exactly as many
// samples as it receives, at the cost of a constant 10 ms of latency.
//
// On top of the denoiser it offers:
//  * strength  - wet/dry mix between the denoised and the original signal.
//  * voice gate - mutes the output while RNNoise's voice-activity probability
//                 stays below a threshold (with attack/release smoothing),
//                 which removes the residual hiss between sentences.
//
// The class has no Qt dependency so it can be unit-tested and reused by the
// voice engine.
class NoiseSuppressor {
public:
    static constexpr int kSampleRate = 48000;
    static constexpr int kFrameSize = 480; // 10 ms @ 48 kHz

    struct Settings {
        bool enabled = true;
        float strength = 1.0f;      // 0.0 = original signal, 1.0 = fully denoised
        bool voiceGate = true;
        float gateThreshold = 0.60f; // voice probability below which the gate closes
        int gateHoldMs = 200;        // keep the gate open this long after speech
    };

    NoiseSuppressor();
    ~NoiseSuppressor();

    NoiseSuppressor(const NoiseSuppressor &) = delete;
    NoiseSuppressor &operator=(const NoiseSuppressor &) = delete;

    void setSettings(const Settings &settings);
    Settings settings() const { return m_settings; }

    // Processes `count` samples in place. Output length always equals input length.
    void process(int16_t *samples, std::size_t count);

    // Latest voice-activity probability reported by RNNoise (0..1).
    float voiceProbability() const { return m_lastVad; }

    // True while the voice gate lets audio through.
    bool gateOpen() const { return m_gateGain > 0.5f; }

    // Latency added by the internal frame buffering, in samples.
    static constexpr int latencySamples() { return kFrameSize; }

    void reset();

private:
    void processFrame(const float *in, float *out);

    DenoiseState *m_state = nullptr;
    Settings m_settings;

    std::vector<float> m_inFifo;   // pending input samples (< kFrameSize)
    std::vector<int16_t> m_outFifo; // processed samples waiting to be returned

    float m_lastVad = 0.0f;
    float m_gateGain = 1.0f;
    int m_holdFramesLeft = 0;
};

} // namespace lightcord::audio
