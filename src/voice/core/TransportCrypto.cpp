// SPDX-License-Identifier: MIT
#include "TransportCrypto.h"

#include "Rtp.h"

#include <openssl/evp.h>
#include <sodium.h>

#include <algorithm>
#include <cstring>
#include <memory>

namespace lightcord::voice {

namespace {
struct CipherCtxDeleter {
    void operator()(EVP_CIPHER_CTX *ctx) const { EVP_CIPHER_CTX_free(ctx); }
};
using CipherCtx = std::unique_ptr<EVP_CIPHER_CTX, CipherCtxDeleter>;

bool sodiumReady()
{
    static const bool ok = sodium_init() >= 0;
    return ok;
}
} // namespace

TransportCrypto::Mode TransportCrypto::modeFromName(const std::string &name)
{
    if (name == kAesMode)
        return Mode::Aes256Gcm;
    if (name == kXChaChaMode)
        return Mode::XChaCha20Poly1305;
    return Mode::None;
}

const char *TransportCrypto::modeName(Mode mode)
{
    switch (mode) {
    case Mode::Aes256Gcm:
        return kAesMode;
    case Mode::XChaCha20Poly1305:
        return kXChaChaMode;
    default:
        return "";
    }
}

TransportCrypto::Mode TransportCrypto::chooseMode(const std::vector<std::string> &offered)
{
    const auto has = [&](const char *m) { return std::find(offered.begin(), offered.end(), m) != offered.end(); };
    // Discord: prefer AES-256-GCM when offered, XChaCha20-Poly1305 is always available.
    if (has(kAesMode))
        return Mode::Aes256Gcm;
    if (has(kXChaChaMode))
        return Mode::XChaCha20Poly1305;
    return Mode::None;
}

TransportCrypto::TransportCrypto() = default;

TransportCrypto::~TransportCrypto()
{
    std::fill(m_key.begin(), m_key.end(), uint8_t{0});
}

bool TransportCrypto::setKey(Mode mode, const uint8_t *key, std::size_t keySize)
{
    if (keySize != m_key.size() || mode == Mode::None)
        return false;
    if (mode == Mode::XChaCha20Poly1305 && !sodiumReady())
        return false;
    std::memcpy(m_key.data(), key, keySize);
    m_mode = mode;
    m_nonce = 0;
    return true;
}

bool TransportCrypto::aesEncrypt(const uint8_t *nonce12, const uint8_t *aad, std::size_t aadSize, const uint8_t *in,
                                 std::size_t inSize, uint8_t *out) const
{
    CipherCtx ctx(EVP_CIPHER_CTX_new());
    if (!ctx)
        return false;
    int len = 0;
    if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) != 1 ||
        EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, m_key.data(), nonce12) != 1 ||
        EVP_EncryptUpdate(ctx.get(), nullptr, &len, aad, static_cast<int>(aadSize)) != 1 ||
        EVP_EncryptUpdate(ctx.get(), out, &len, in, static_cast<int>(inSize)) != 1)
        return false;
    int finalLen = 0;
    if (EVP_EncryptFinal_ex(ctx.get(), out + len, &finalLen) != 1)
        return false;
    return EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, static_cast<int>(kTagSize),
                               out + inSize) == 1;
}

bool TransportCrypto::aesDecrypt(const uint8_t *nonce12, const uint8_t *aad, std::size_t aadSize, const uint8_t *in,
                                 std::size_t inSize, uint8_t *out) const
{
    if (inSize < kTagSize)
        return false;
    const std::size_t cipherSize = inSize - kTagSize;
    CipherCtx ctx(EVP_CIPHER_CTX_new());
    if (!ctx)
        return false;
    int len = 0;
    uint8_t tag[kTagSize];
    std::memcpy(tag, in + cipherSize, kTagSize);
    if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) != 1 ||
        EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, m_key.data(), nonce12) != 1 ||
        EVP_DecryptUpdate(ctx.get(), nullptr, &len, aad, static_cast<int>(aadSize)) != 1 ||
        EVP_DecryptUpdate(ctx.get(), out, &len, in, static_cast<int>(cipherSize)) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, static_cast<int>(kTagSize), tag) != 1)
        return false;
    int finalLen = 0;
    return EVP_DecryptFinal_ex(ctx.get(), out + len, &finalLen) == 1;
}

bool TransportCrypto::seal(const uint8_t *header, std::size_t headerSize, const uint8_t *payload,
                           std::size_t payloadSize, std::vector<uint8_t> &packet)
{
    if (!ready())
        return false;

    const uint32_t counter = m_nonce++;
    uint8_t nonceSuffix[kNonceSuffixSize];
    writeBe32(nonceSuffix, counter);

    packet.resize(headerSize + payloadSize + kTagSize + kNonceSuffixSize);
    std::memcpy(packet.data(), header, headerSize);
    uint8_t *cipher = packet.data() + headerSize;

    bool ok = false;
    if (m_mode == Mode::Aes256Gcm) {
        uint8_t nonce[12] = {};
        std::memcpy(nonce, nonceSuffix, kNonceSuffixSize);
        ok = aesEncrypt(nonce, header, headerSize, payload, payloadSize, cipher);
    } else {
        uint8_t nonce[crypto_aead_xchacha20poly1305_ietf_NPUBBYTES] = {};
        std::memcpy(nonce, nonceSuffix, kNonceSuffixSize);
        unsigned long long cipherLen = 0;
        ok = crypto_aead_xchacha20poly1305_ietf_encrypt(cipher, &cipherLen, payload, payloadSize, header, headerSize,
                                                        nullptr, nonce, m_key.data()) == 0 &&
             cipherLen == payloadSize + kTagSize;
    }
    if (!ok)
        return false;
    std::memcpy(packet.data() + headerSize + payloadSize + kTagSize, nonceSuffix, kNonceSuffixSize);
    return true;
}

bool TransportCrypto::open(const uint8_t *packet, std::size_t size, std::size_t unencryptedHeaderSize,
                           std::size_t extensionBodySize, std::vector<uint8_t> &payload) const
{
    if (!ready() || size < unencryptedHeaderSize + kTagSize + kNonceSuffixSize)
        return false;

    const uint8_t *nonceSuffix = packet + size - kNonceSuffixSize;
    const uint8_t *cipher = packet + unencryptedHeaderSize;
    const std::size_t cipherSize = size - unencryptedHeaderSize - kNonceSuffixSize; // includes tag
    const std::size_t plainSize = cipherSize - kTagSize;

    std::vector<uint8_t> plain(plainSize);
    bool ok = false;
    if (m_mode == Mode::Aes256Gcm) {
        uint8_t nonce[12] = {};
        std::memcpy(nonce, nonceSuffix, kNonceSuffixSize);
        ok = aesDecrypt(nonce, packet, unencryptedHeaderSize, cipher, cipherSize, plain.data());
    } else {
        uint8_t nonce[crypto_aead_xchacha20poly1305_ietf_NPUBBYTES] = {};
        std::memcpy(nonce, nonceSuffix, kNonceSuffixSize);
        unsigned long long plainLen = 0;
        ok = crypto_aead_xchacha20poly1305_ietf_decrypt(plain.data(), &plainLen, nullptr, cipher, cipherSize, packet,
                                                        unencryptedHeaderSize, nonce, m_key.data()) == 0 &&
             plainLen == plainSize;
    }
    if (!ok || extensionBodySize > plain.size())
        return false;

    payload.assign(plain.begin() + static_cast<std::ptrdiff_t>(extensionBodySize), plain.end());
    return true;
}

} // namespace lightcord::voice
