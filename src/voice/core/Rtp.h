// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>

namespace lightcord::voice {

constexpr uint8_t kRtpVersionFlags = 0x80;
constexpr uint8_t kOpusPayloadType = 0x78; // 120
constexpr std::size_t kRtpHeaderSize = 12;

// Opus parameters required by Discord.
constexpr int kSampleRate = 48000;
constexpr int kChannels = 2;
constexpr int kFrameSamples = 960; // 20 ms per channel
constexpr uint8_t kOpusSilenceFrame[3] = {0xF8, 0xFF, 0xFE};

inline void writeBe16(uint8_t *p, uint16_t v)
{
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v);
}

inline void writeBe32(uint8_t *p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}

inline uint16_t readBe16(const uint8_t *p)
{
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

inline uint32_t readBe32(const uint8_t *p)
{
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

// Writes the fixed 12-byte RTP header used for outgoing voice packets.
void writeRtpHeader(uint8_t *out, uint16_t sequence, uint32_t timestamp, uint32_t ssrc);

struct RtpInfo {
    uint8_t payloadType = 0;
    bool marker = false;
    uint16_t sequence = 0;
    uint32_t timestamp = 0;
    uint32_t ssrc = 0;
    // Size of the part left unencrypted by the "rtpsize" transport modes:
    // fixed header + CSRCs + (if present) the 4-byte extension preamble.
    std::size_t unencryptedHeaderSize = 0;
    // Length in bytes of the extension body, which IS encrypted and must be
    // stripped from the decrypted plaintext.
    std::size_t extensionBodySize = 0;
};

// Parses an incoming RTP packet. Returns false for anything that is not a
// valid RTP packet (RTCP and keep-alives included).
bool parseRtp(const uint8_t *data, std::size_t size, RtpInfo &info);

// True for RTCP packets (payload types 200-204), which Discord also sends on the voice socket.
bool isRtcp(const uint8_t *data, std::size_t size);

} // namespace lightcord::voice
