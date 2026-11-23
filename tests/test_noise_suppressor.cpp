// SPDX-License-Identifier: MIT
// Unit test for lightcord::audio::NoiseSuppressor (no Qt required).
//
// 1. Pure white noise must come out much quieter.
// 2. Output length must always equal input length, for odd chunk sizes too.
// 3. With suppression disabled the signal must pass through (delayed 480 samples).

#include "audio/NoiseSuppressor.h"

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using lightcord::audio::NoiseSuppressor;

namespace {

int g_failures = 0;

void check(bool ok, const char *what)
{
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        ++g_failures;
}

double rms(const std::vector<int16_t> &v, std::size_t from = 0)
{
    double acc = 0.0;
    std::size_t n = 0;
    for (std::size_t i = from; i < v.size(); ++i, ++n)
        acc += static_cast<double>(v[i]) * v[i];
    return n ? std::sqrt(acc / static_cast<double>(n)) : 0.0;
}

std::vector<int16_t> whiteNoise(std::size_t n, double amplitude, unsigned seed)
{
    std::mt19937 rng(seed);
    std::normal_distribution<double> dist(0.0, amplitude);
    std::vector<int16_t> out(n);
    for (auto &s : out)
        s = static_cast<int16_t>(std::max(-32768.0, std::min(32767.0, dist(rng))));
    return out;
}

void runInChunks(NoiseSuppressor &ns, std::vector<int16_t> &buf, std::size_t chunk)
{
    for (std::size_t pos = 0; pos < buf.size(); pos += chunk) {
        const std::size_t n = std::min(chunk, buf.size() - pos);
        ns.process(buf.data() + pos, n);
    }
}

} // namespace

int main()
{
    const std::size_t seconds = 3;
    const std::size_t total = NoiseSuppressor::kSampleRate * seconds;

    // 1. Steady white noise should be strongly attenuated.
    {
        NoiseSuppressor ns;
        auto noise = whiteNoise(total, 2000.0, 42);
        const double before = rms(noise, total / 2);
        runInChunks(ns, noise, 441); // deliberately not a multiple of 480
        const double after = rms(noise, total / 2); // ignore the adaptation period
        const double reductionDb = 20.0 * std::log10(before / std::max(after, 1e-9));
        std::printf("    noise RMS before=%.1f after=%.1f reduction=%.1f dB\n", before, after, reductionDb);
        check(reductionDb > 15.0, "white noise is reduced by more than 15 dB");
    }

    // 2. Without the voice gate, RNNoise alone should still reduce noise.
    {
        NoiseSuppressor ns;
        NoiseSuppressor::Settings s;
        s.voiceGate = false;
        ns.setSettings(s);
        auto noise = whiteNoise(total, 2000.0, 7);
        const double before = rms(noise, total / 2);
        runInChunks(ns, noise, 960);
        const double after = rms(noise, total / 2);
        const double reductionDb = 20.0 * std::log10(before / std::max(after, 1e-9));
        std::printf("    (gate off) reduction=%.1f dB\n", reductionDb);
        check(reductionDb > 6.0, "RNNoise without gate reduces noise by more than 6 dB");
    }

    // 3. Disabled suppressor is a pure 480-sample delay line.
    {
        NoiseSuppressor ns;
        NoiseSuppressor::Settings s;
        s.enabled = false;
        ns.setSettings(s);
        auto input = whiteNoise(9600, 3000.0, 3);
        auto buf = input;
        runInChunks(ns, buf, 333);
        bool same = true;
        for (std::size_t i = NoiseSuppressor::latencySamples(); i < buf.size(); ++i) {
            if (buf[i] != input[i - NoiseSuppressor::latencySamples()]) {
                same = false;
                break;
            }
        }
        check(same, "disabled suppressor passes audio through with a constant delay");
    }

    // 4. Odd chunk sizes never change the buffer length or crash.
    {
        NoiseSuppressor ns;
        std::vector<int16_t> buf(12345, 100);
        for (std::size_t chunk : {1u, 7u, 480u, 481u, 2048u})
            runInChunks(ns, buf, chunk);
        check(buf.size() == 12345u, "arbitrary chunk sizes are handled");
    }

    std::printf("%s\n", g_failures == 0 ? "All tests passed." : "Some tests FAILED.");
    return g_failures == 0 ? 0 : 1;
}
