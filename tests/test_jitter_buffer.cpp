// SPDX-License-Identifier: MIT
// Simulates a speaker sending 20 ms Opus frames over a jittery network while
// the sound card pulls 10 ms every 10 ms, and checks that the adaptive
// jitter buffer keeps playback continuous with bounded latency.

#include "audio/JitterBuffer.h"

#include <algorithm>
#include <cstdio>
#include <random>
#include <vector>

using lightcord::audio::JitterBuffer;

namespace {

int g_failures = 0;

void check(bool ok, const char *what)
{
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        ++g_failures;
}

struct Result {
    int gapTicks = 0;      // 10 ms ticks with missing audio while the speaker talked
    int maxBufferedMs = 0;
    int finalTargetMs = 0;
    int underruns = 0;
};

// jitterMs: each packet is delayed by a random amount in [0, jitterMs].
// burstEvery: every N packets, a stall of `burstMs` (Wi-Fi hiccup).
Result simulate(int seconds, int jitterMs, int burstEvery, int burstMs, unsigned seed)
{
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> jitter(0, jitterMs);

    const int packets = seconds * 50;
    std::vector<int> arrival(packets);
    int lastArrival = 0;
    for (int i = 0; i < packets; ++i) {
        int t = i * 20 + jitter(rng);
        if (burstEvery > 0 && i % burstEvery == 0 && i > 0)
            t += burstMs;
        // UDP arrives roughly in order; a packet cannot overtake the previous one here.
        t = std::max(t, lastArrival);
        arrival[i] = lastArrival = t;
    }

    JitterBuffer jb;
    std::vector<int16_t> frame(960 * 2, 1000);
    std::vector<int32_t> acc(480 * 2);
    Result r;
    int next = 0;
    bool started = false;
    const int endMs = seconds * 1000;
    for (int now = 0; now < endMs + 500; now += 10) {
        while (next < packets && arrival[next] <= now) {
            jb.push(frame.data(), 960, now);
            ++next;
        }
        r.maxBufferedMs = std::max(r.maxBufferedMs, static_cast<int>(jb.bufferedFrames() / 48));
        std::fill(acc.begin(), acc.end(), 0);
        const std::size_t got = jb.mixInto(acc.data(), 480, now);
        if (got > 0)
            started = true;
        const bool speakerActive = next < packets; // still sending
        if (started && speakerActive && got < 480)
            ++r.gapTicks;
    }
    r.finalTargetMs = jb.targetMs();
    r.underruns = jb.underruns();
    return r;
}

} // namespace

int main()
{
    {
        const Result r = simulate(60, 5, 0, 0, 1);
        std::printf("    stable network: gaps=%d underruns=%d target=%dms max buffered=%dms\n", r.gapTicks, r.underruns,
                    r.finalTargetMs, r.maxBufferedMs);
        check(r.gapTicks == 0, "stable network: continuous playback");
        check(r.maxBufferedMs <= 100, "stable network: latency stays low (<= 100 ms buffered)");
    }
    {
        const Result r = simulate(60, 40, 0, 0, 2);
        std::printf("    40 ms jitter: gaps=%d underruns=%d target=%dms max buffered=%dms\n", r.gapTicks, r.underruns,
                    r.finalTargetMs, r.maxBufferedMs);
        check(r.gapTicks <= 3, "40 ms of jitter: at most a few gaps in 60 s");
    }
    {
        const Result r = simulate(60, 30, 100, 90, 3);
        std::printf("    Wi-Fi stalls (90 ms every 2 s): gaps=%d underruns=%d target=%dms max buffered=%dms\n",
                    r.gapTicks, r.underruns, r.finalTargetMs, r.maxBufferedMs);
        check(r.gapTicks <= 15, "periodic 90 ms stalls: the buffer adapts after the first ones");
        check(r.finalTargetMs <= 160 && r.maxBufferedMs <= 300, "latency stays bounded under stalls");
    }

    std::printf("%s\n", g_failures == 0 ? "All jitter buffer tests passed." : "Some jitter buffer tests FAILED.");
    return g_failures == 0 ? 0 : 1;
}
