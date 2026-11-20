// SPDX-License-Identifier: MIT
#include "Rtp.h"

namespace lightcord::voice {

void writeRtpHeader(uint8_t *out, uint16_t sequence, uint32_t timestamp, uint32_t ssrc)
{
    out[0] = kRtpVersionFlags;
    out[1] = kOpusPayloadType;
    writeBe16(out + 2, sequence);
    writeBe32(out + 4, timestamp);
    writeBe32(out + 8, ssrc);
}

bool isRtcp(const uint8_t *data, std::size_t size)
{
    if (size < 2)
        return false;
    const uint8_t pt = data[1];
    return pt >= 200 && pt <= 204;
}

bool parseRtp(const uint8_t *data, std::size_t size, RtpInfo &info)
{
    if (size < kRtpHeaderSize)
        return false;
    if ((data[0] >> 6) != 2) // RTP version 2
        return false;
    if (isRtcp(data, size))
        return false;

    const int csrcCount = data[0] & 0x0F;
    const bool hasExtension = (data[0] & 0x10) != 0;

    info.marker = (data[1] & 0x80) != 0;
    info.payloadType = data[1] & 0x7F;
    info.sequence = readBe16(data + 2);
    info.timestamp = readBe32(data + 4);
    info.ssrc = readBe32(data + 8);

    std::size_t header = kRtpHeaderSize + static_cast<std::size_t>(csrcCount) * 4;
    info.extensionBodySize = 0;
    if (hasExtension) {
        if (size < header + 4)
            return false;
        const uint16_t words = readBe16(data + header + 2);
        info.extensionBodySize = static_cast<std::size_t>(words) * 4;
        header += 4; // preamble only; the body is encrypted
    }
    if (size < header)
        return false;
    info.unencryptedHeaderSize = header;
    return true;
}

} // namespace lightcord::voice
