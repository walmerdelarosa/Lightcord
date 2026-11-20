// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace lightcord::voice {

// Discord UDP IP discovery (all fields big-endian):
//   type (2) = 0x1 request / 0x2 response
//   length (2) = 70
//   ssrc (4)
//   address (64, NUL-terminated string in the response)
//   port (2)
constexpr std::size_t kIpDiscoverySize = 74;

std::array<uint8_t, kIpDiscoverySize> buildIpDiscoveryRequest(uint32_t ssrc);

// Returns true and fills `address`/`port` if `data` is a valid response.
bool parseIpDiscoveryResponse(const uint8_t *data, std::size_t size, std::string &address, uint16_t &port);

} // namespace lightcord::voice
