// SPDX-License-Identifier: MIT
// Unit tests for the Qt-free voice core:
//  * RTP header writing/parsing (CSRCs, header extensions, RTCP rejection)
//  * UDP IP discovery packets
//  * Transport encryption in both Discord "rtpsize" AEAD modes
//  * Opus encode/decode round trip
//  * A complete DAVE (MLS) handshake between two clients, driven through
//    DaveSession exactly as the voice gateway would, followed by end-to-end
//    encrypted Opus frames in both directions.

#include "external_sender.h"
#include "voice/core/DaveSession.h"
#include "voice/core/IpDiscovery.h"
#include "voice/core/OpusCodec.h"
#include "voice/core/Rtp.h"
#include "voice/core/TransportCrypto.h"

#include <sodium.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace lightcord::voice;

namespace {

int g_failures = 0;

void check(bool ok, const char *what)
{
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        ++g_failures;
}

std::vector<uint8_t> bytes(std::initializer_list<int> list)
{
    std::vector<uint8_t> v;
    for (int b : list)
        v.push_back(static_cast<uint8_t>(b));
    return v;
}

void testRtp()
{
    uint8_t header[kRtpHeaderSize];
    writeRtpHeader(header, 0xBEEF, 0x01020304, 0xCAFEBABE);
    RtpInfo info;
    check(parseRtp(header, sizeof header, info) && info.sequence == 0xBEEF && info.timestamp == 0x01020304 &&
              info.ssrc == 0xCAFEBABE && info.payloadType == kOpusPayloadType &&
              info.unencryptedHeaderSize == kRtpHeaderSize && info.extensionBodySize == 0,
          "RTP header round trip");

    // One CSRC + one-byte header extension with 2 words of extension body.
    std::vector<uint8_t> pkt = bytes({0x91, 0x78, 0, 1, 0, 0, 0, 2, 0, 0, 0, 3, /*csrc*/ 0, 0, 0, 9,
                                      /*ext preamble*/ 0xBE, 0xDE, 0, 2, /*ext body*/ 1, 2, 3, 4, 5, 6, 7, 8});
    check(parseRtp(pkt.data(), pkt.size(), info) && info.unencryptedHeaderSize == 20 &&
              info.extensionBodySize == 8,
          "RTP with CSRC and extension: rtpsize header = 20 bytes, 8-byte encrypted extension body");

    std::vector<uint8_t> rtcp = bytes({0x81, 0xC9, 0, 7, 0, 0, 0, 1, 0, 0, 0, 0});
    check(!parseRtp(rtcp.data(), rtcp.size(), info) && isRtcp(rtcp.data(), rtcp.size()), "RTCP packets are rejected");
}

void testIpDiscovery()
{
    auto req = buildIpDiscoveryRequest(0x11223344);
    check(req.size() == 74 && req[0] == 0 && req[1] == 1 && req[2] == 0 && req[3] == 70 && req[4] == 0x11 &&
              req[7] == 0x44,
          "IP discovery request layout");

    std::array<uint8_t, 74> resp{};
    writeBe16(resp.data(), 2);
    writeBe16(resp.data() + 2, 70);
    writeBe32(resp.data() + 4, 0x11223344);
    std::memcpy(resp.data() + 8, "203.0.113.7", 11);
    writeBe16(resp.data() + 72, 50123);
    std::string ip;
    uint16_t port = 0;
    check(parseIpDiscoveryResponse(resp.data(), resp.size(), ip, port) && ip == "203.0.113.7" && port == 50123,
          "IP discovery response parsing");
}

void testTransport(TransportCrypto::Mode mode, const char *name)
{
    uint8_t key[32];
    for (int i = 0; i < 32; ++i)
        key[i] = static_cast<uint8_t>(i * 7 + 1);

    TransportCrypto tx, rx;
    tx.setKey(mode, key, sizeof key);
    rx.setKey(mode, key, sizeof key);
    tx.setNonceCounter(0x01020304);

    uint8_t header[kRtpHeaderSize];
    writeRtpHeader(header, 10, 9600, 42);
    const std::vector<uint8_t> opus = bytes({0xFC, 0xFF, 0xFE, 1, 2, 3, 4, 5, 6, 7});

    std::vector<uint8_t> packet;
    const bool sealed = tx.seal(header, sizeof header, opus.data(), opus.size(), packet);
    const bool layout = packet.size() == sizeof header + opus.size() + 16 + 4 &&
                        std::memcmp(packet.data(), header, sizeof header) == 0 && packet[packet.size() - 4] == 1 &&
                        packet[packet.size() - 1] == 4;

    RtpInfo info;
    std::vector<uint8_t> out;
    const bool opened = parseRtp(packet.data(), packet.size(), info) &&
                        rx.open(packet.data(), packet.size(), info.unencryptedHeaderSize, info.extensionBodySize, out);
    std::string label = std::string(name) + ": seal/open round trip with big-endian nonce suffix";
    check(sealed && layout && opened && out == opus, label.c_str());

    // Any modification of the header (AAD) must be detected.
    packet[3] ^= 0x01;
    label = std::string(name) + ": tampered header is rejected";
    check(!rx.open(packet.data(), packet.size(), info.unencryptedHeaderSize, 0, out), label.c_str());
}

void testAesInterop()
{
    // Cross-check the AES-256-GCM nonce/AAD layout with libsodium's independent
    // implementation (only available on CPUs with AES-NI).
    if (sodium_init() < 0 || !crypto_aead_aes256gcm_is_available()) {
        std::printf("[SKIP] AES-256-GCM interop (no hardware AES in libsodium)\n");
        return;
    }
    uint8_t key[32] = {9};
    TransportCrypto tx;
    tx.setKey(TransportCrypto::Mode::Aes256Gcm, key, sizeof key);
    tx.setNonceCounter(77);
    uint8_t header[kRtpHeaderSize];
    writeRtpHeader(header, 1, 2, 3);
    const std::vector<uint8_t> opus(100, 0xAB);
    std::vector<uint8_t> packet;
    tx.seal(header, sizeof header, opus.data(), opus.size(), packet);

    uint8_t nonce[crypto_aead_aes256gcm_NPUBBYTES] = {};
    std::memcpy(nonce, packet.data() + packet.size() - 4, 4);
    std::vector<uint8_t> plain(opus.size());
    unsigned long long plainLen = 0;
    const int rc = crypto_aead_aes256gcm_decrypt(plain.data(), &plainLen, nullptr, packet.data() + kRtpHeaderSize,
                                                 packet.size() - kRtpHeaderSize - 4, header, sizeof header, nonce, key);
    check(rc == 0 && plain == opus, "AES-256-GCM packets decrypt with libsodium (independent implementation)");
}

void testOpus()
{
    OpusFrameEncoder enc;
    OpusFrameDecoder dec;
    std::vector<int16_t> pcm(kFrameSamples * kChannels);
    double phase = 0.0;
    std::vector<int16_t> lastDecoded;
    bool ok = enc.isValid() && dec.isValid();
    for (int frame = 0; frame < 10 && ok; ++frame) {
        for (int i = 0; i < kFrameSamples; ++i) {
            const int16_t s = static_cast<int16_t>(8000 * std::sin(phase));
            phase += 2 * 3.14159265358979 * 440.0 / kSampleRate;
            pcm[2 * i] = s;
            pcm[2 * i + 1] = s;
        }
        std::vector<uint8_t> packet;
        ok = enc.encode(pcm.data(), packet) && !packet.empty() && packet.size() < 400;
        ok = ok && dec.decode(packet.data(), packet.size(), lastDecoded) == kFrameSamples;
    }
    double energy = 0;
    for (int16_t s : lastDecoded)
        energy += double(s) * s;
    const double rms = lastDecoded.empty() ? 0 : std::sqrt(energy / lastDecoded.size());
    std::printf("    decoded tone RMS = %.0f (input RMS ~ 5657)\n", rms);
    check(ok && rms > 3000 && rms < 8000, "Opus stereo 20 ms encode/decode round trip");
}

// ---------------------------------------------------------------------------
// DAVE
// ---------------------------------------------------------------------------

struct FakeClient {
    std::string userId;
    std::unique_ptr<DaveSession> dave;
    std::vector<uint8_t> lastKeyPackage;
    std::vector<uint8_t> lastCommitWelcome;
    std::vector<std::pair<int, uint16_t>> transitions;
    uint16_t version = 0;

    FakeClient(const std::string &id, uint64_t groupId)
        : userId(id)
    {
        DaveSession::Callbacks cb;
        cb.sendBinary = [this](int op, const std::vector<uint8_t> &payload) {
            if (op == DaveOp::MlsKeyPackage)
                lastKeyPackage = payload;
            else if (op == DaveOp::MlsCommitWelcome)
                lastCommitWelcome = payload;
        };
        cb.sendTransition = [this](int op, uint16_t tid) { transitions.emplace_back(op, tid); };
        cb.protocolChanged = [this](uint16_t v) { version = v; };
        dave = std::make_unique<DaveSession>(id, groupId, cb);
    }
};

void testDave()
{
    const uint64_t groupId = 1300000000000000001ull; // the voice channel id
    discord::dave::test::ExternalSender gateway(1, groupId);
    const auto externalSender = gateway.GetMarshalledExternalSender();

    FakeClient a("1234123412341234", groupId);
    FakeClient b("5678567856785678", groupId);
    check(DaveSession::maxProtocolVersion() >= 1, "libdave supports DAVE protocol v1+");

    // Voice gateway: op 11 (clients connect), op 25 (external sender), op 4 (dave_protocol_version = 1).
    for (FakeClient *c : {&a, &b}) {
        c->dave->addUser(c == &a ? b.userId : a.userId);
        c->dave->onExternalSender(externalSender.data(), externalSender.size());
        c->dave->onSessionDescription(1);
    }
    check(!a.lastKeyPackage.empty() && !b.lastKeyPackage.empty(), "both clients send an MLS key package (op 26)");

    // op 27: the gateway proposes adding each other pending member.
    const auto proposalsForA = gateway.ProposeAdd(0, b.lastKeyPackage);
    const auto proposalsForB = gateway.ProposeAdd(0, a.lastKeyPackage);
    a.dave->onProposals(proposalsForA.data(), proposalsForA.size());
    b.dave->onProposals(proposalsForB.data(), proposalsForB.size());
    check(!a.lastCommitWelcome.empty(), "client A answers with commit + welcome (op 28)");

    // The gateway picks A's commit: op 29 to A, op 30 (welcome) to B.
    const uint16_t transitionId = 7;
    auto [commit, welcome] = gateway.SplitCommitWelcome(a.lastCommitWelcome);
    a.dave->onAnnounceCommit(transitionId, commit.data(), commit.size());
    b.dave->onWelcome(transitionId, welcome.data(), welcome.size());
    const auto ready = std::make_pair(int(DaveOp::TransitionReady), transitionId);
    check(!a.transitions.empty() && a.transitions.back() == ready && !b.transitions.empty() &&
              b.transitions.back() == ready,
          "both clients report transition ready (op 23)");

    // op 22: execute.
    a.dave->onExecuteTransition(transitionId);
    b.dave->onExecuteTransition(transitionId);
    check(a.version == 1 && b.version == 1 && a.dave->isEncrypted() && b.dave->isEncrypted(),
          "E2EE active on both sides after the transition");

    a.dave->setSelfSsrc(1001);
    b.dave->setSelfSsrc(1002);

    const std::vector<uint8_t> opus = bytes({0x78, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A});
    for (int round = 0; round < 3; ++round) {
        std::vector<uint8_t> e2ee, plain;
        const bool enc = a.dave->encryptOpus(opus.data(), opus.size(), e2ee);
        const bool marker = e2ee.size() > opus.size() && e2ee[e2ee.size() - 1] == 0xFA && e2ee[e2ee.size() - 2] == 0xFA;
        const bool changed = e2ee.size() >= opus.size() && std::memcmp(e2ee.data(), opus.data(), opus.size()) != 0;
        const bool dec = b.dave->decryptOpus(a.userId, e2ee.data(), e2ee.size(), plain);
        if (round == 0)
            check(enc && marker && changed, "A's Opus frame is E2E-encrypted with the 0xFAFA DAVE marker");
        if (round == 2)
            check(dec && plain == opus, "B decrypts A's frames (3 consecutive frames)");
    }

    std::vector<uint8_t> e2ee, plain;
    b.dave->encryptOpus(opus.data(), opus.size(), e2ee);
    check(a.dave->decryptOpus(b.userId, e2ee.data(), e2ee.size(), plain) && plain == opus,
          "A decrypts B's frames (reverse direction)");

    // A third party that is not in the group cannot read the media.
    FakeClient eve("9012901290129012", groupId);
    eve.dave->addUser(a.userId);
    eve.dave->onExternalSender(externalSender.data(), externalSender.size());
    eve.dave->onSessionDescription(1);
    a.dave->encryptOpus(opus.data(), opus.size(), e2ee);
    check(!eve.dave->decryptOpus(a.userId, e2ee.data(), e2ee.size(), plain), "an outsider cannot decrypt the frames");
}

} // namespace

int main()
{
    testRtp();
    testIpDiscovery();
    testTransport(TransportCrypto::Mode::Aes256Gcm, "AES-256-GCM");
    testTransport(TransportCrypto::Mode::XChaCha20Poly1305, "XChaCha20-Poly1305");
    testAesInterop();
    testOpus();
    testDave();
    std::printf("%s\n", g_failures == 0 ? "All voice tests passed." : "Some voice tests FAILED.");
    return g_failures == 0 ? 0 : 1;
}
