// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

struct OpusEncoder;
struct OpusDecoder;

namespace lightcord::voice {

// Stereo 48 kHz Opus encoder for 20 ms frames, tuned for voice.
class OpusFrameEncoder {
public:
    explicit OpusFrameEncoder(int bitrate = 64000);
    ~OpusFrameEncoder();
    OpusFrameEncoder(const OpusFrameEncoder &) = delete;
    OpusFrameEncoder &operator=(const OpusFrameEncoder &) = delete;

    bool isValid() const { return m_encoder != nullptr; }
    void setBitrate(int bitrate);

    // `pcm` holds kFrameSamples interleaved stereo samples (1920 int16).
    bool encode(const int16_t *pcm, std::vector<uint8_t> &out);

private:
    OpusEncoder *m_encoder = nullptr;
};

// Stereo 48 kHz Opus decoder (one per remote SSRC).
class OpusFrameDecoder {
public:
    OpusFrameDecoder();
    ~OpusFrameDecoder();
    OpusFrameDecoder(const OpusFrameDecoder &) = delete;
    OpusFrameDecoder &operator=(const OpusFrameDecoder &) = delete;

    bool isValid() const { return m_decoder != nullptr; }

    // Decodes one packet into interleaved stereo int16. Returns samples per channel, or -1.
    int decode(const uint8_t *data, std::size_t size, std::vector<int16_t> &pcm);
    // Packet-loss concealment for one missing 20 ms frame.
    int conceal(std::vector<int16_t> &pcm);

private:
    OpusDecoder *m_decoder = nullptr;
};

} // namespace lightcord::voice
