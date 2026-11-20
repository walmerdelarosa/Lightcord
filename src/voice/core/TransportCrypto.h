// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lightcord::voice {

// Transport encryption between the client and Discord's voice server (SFU).
//
// Both supported modes are "rtpsize" AEAD modes:
//   packet = [RTP header (unencrypted, used as AAD)] [ciphertext + 16-byte tag] [4-byte nonce]
// The nonce is a 32-bit big-endian counter appended to the packet; it is
// expanded to 12 bytes (AES-256-GCM) or 24 bytes (XChaCha20-Poly1305) by
// zero-padding on the right.
class TransportCrypto {
public:
    enum class Mode { None, Aes256Gcm, XChaCha20Poly1305 };

    static constexpr const char *kAesMode = "aead_aes256_gcm_rtpsize";
    static constexpr const char *kXChaChaMode = "aead_xchacha20_poly1305_rtpsize";
    static constexpr std::size_t kTagSize = 16;
    static constexpr std::size_t kNonceSuffixSize = 4;

    // Picks the preferred mode among those offered by the server.
    static Mode chooseMode(const std::vector<std::string> &offered);
    static Mode modeFromName(const std::string &name);
    static const char *modeName(Mode mode);

    TransportCrypto();
    ~TransportCrypto();
    TransportCrypto(const TransportCrypto &) = delete;
    TransportCrypto &operator=(const TransportCrypto &) = delete;

    bool setKey(Mode mode, const uint8_t *key, std::size_t keySize);
    Mode mode() const { return m_mode; }
    bool ready() const { return m_mode != Mode::None; }

    // Builds a complete encrypted packet from an RTP header and a payload.
    bool seal(const uint8_t *header, std::size_t headerSize, const uint8_t *payload, std::size_t payloadSize,
              std::vector<uint8_t> &packet);

    // Decrypts an incoming packet. `unencryptedHeaderSize` and `extensionBodySize`
    // come from parseRtp(). On success `payload` holds the media payload
    // (the RTP extension body is removed).
    bool open(const uint8_t *packet, std::size_t size, std::size_t unencryptedHeaderSize,
              std::size_t extensionBodySize, std::vector<uint8_t> &payload) const;

    // Exposed for tests.
    void setNonceCounter(uint32_t value) { m_nonce = value; }

private:
    bool aesEncrypt(const uint8_t *nonce12, const uint8_t *aad, std::size_t aadSize, const uint8_t *in,
                    std::size_t inSize, uint8_t *out) const;
    bool aesDecrypt(const uint8_t *nonce12, const uint8_t *aad, std::size_t aadSize, const uint8_t *in,
                    std::size_t inSize, uint8_t *out) const;

    Mode m_mode = Mode::None;
    std::array<uint8_t, 32> m_key{};
    uint32_t m_nonce = 0;
};

} // namespace lightcord::voice
