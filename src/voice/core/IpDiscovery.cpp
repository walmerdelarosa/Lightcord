// SPDX-License-Identifier: MIT
#include "IpDiscovery.h"

#include "Rtp.h"

namespace lightcord::voice {

std::array<uint8_t, kIpDiscoverySize> buildIpDiscoveryRequest(uint32_t ssrc)
{
    std::array<uint8_t, kIpDiscoverySize> packet{};
    writeBe16(packet.data(), 0x1);
    writeBe16(packet.data() + 2, 70);
    writeBe32(packet.data() + 4, ssrc);
    return packet;
}

bool parseIpDiscoveryResponse(const uint8_t *data, std::size_t size, std::string &address, uint16_t &port)
{
    if (size < kIpDiscoverySize || readBe16(data) != 0x2 || readBe16(data + 2) != 70)
        return false;
    const char *addr = reinterpret_cast<const char *>(data + 8);
    std::size_t len = 0;
    while (len < 64 && addr[len] != '\0')
        ++len;
    if (len == 0 || len == 64)
        return false;
    address.assign(addr, len);
    port = readBe16(data + 72);
    return true;
}

} // namespace lightcord::voice
