// SPDX-License-Identifier: MIT
#include "OpusCodec.h"

#include "Rtp.h"

#include <opus.h>

namespace lightcord::voice {

namespace {
constexpr int kMaxPacketBytes = 1275;          // largest Opus packet
constexpr int kMaxFrameSamples = 5760;         // 120 ms at 48 kHz
} // namespace

OpusFrameEncoder::OpusFrameEncoder(int bitrate)
{
    int err = OPUS_OK;
    m_encoder = opus_encoder_create(kSampleRate, kChannels, OPUS_APPLICATION_VOIP, &err);
    if (err != OPUS_OK) {
        m_encoder = nullptr;
        return;
    }
    setBitrate(bitrate);
    opus_encoder_ctl(m_encoder, OPUS_SET_INBAND_FEC(1));
    opus_encoder_ctl(m_encoder, OPUS_SET_PACKET_LOSS_PERC(10));
    opus_encoder_ctl(m_encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
}

OpusFrameEncoder::~OpusFrameEncoder()
{
    if (m_encoder)
        opus_encoder_destroy(m_encoder);
}

void OpusFrameEncoder::setBitrate(int bitrate)
{
    if (m_encoder)
        opus_encoder_ctl(m_encoder, OPUS_SET_BITRATE(bitrate));
}

bool OpusFrameEncoder::encode(const int16_t *pcm, std::vector<uint8_t> &out)
{
    if (!m_encoder)
        return false;
    out.resize(kMaxPacketBytes);
    const opus_int32 n = opus_encode(m_encoder, pcm, kFrameSamples, out.data(), kMaxPacketBytes);
    if (n < 0) {
        out.clear();
        return false;
    }
    out.resize(static_cast<std::size_t>(n));
    return true;
}

OpusFrameDecoder::OpusFrameDecoder()
{
    int err = OPUS_OK;
    m_decoder = opus_decoder_create(kSampleRate, kChannels, &err);
    if (err != OPUS_OK)
        m_decoder = nullptr;
}

OpusFrameDecoder::~OpusFrameDecoder()
{
    if (m_decoder)
        opus_decoder_destroy(m_decoder);
}

int OpusFrameDecoder::decode(const uint8_t *data, std::size_t size, std::vector<int16_t> &pcm)
{
    if (!m_decoder)
        return -1;
    pcm.resize(static_cast<std::size_t>(kMaxFrameSamples) * kChannels);
    const int n = opus_decode(m_decoder, data, static_cast<opus_int32>(size), pcm.data(), kMaxFrameSamples, 0);
    if (n < 0) {
        pcm.clear();
        return -1;
    }
    pcm.resize(static_cast<std::size_t>(n) * kChannels);
    return n;
}

int OpusFrameDecoder::conceal(std::vector<int16_t> &pcm)
{
    if (!m_decoder)
        return -1;
    pcm.resize(static_cast<std::size_t>(kFrameSamples) * kChannels);
    const int n = opus_decode(m_decoder, nullptr, 0, pcm.data(), kFrameSamples, 0);
    if (n < 0) {
        pcm.clear();
        return -1;
    }
    pcm.resize(static_cast<std::size_t>(n) * kChannels);
    return n;
}

} // namespace lightcord::voice
