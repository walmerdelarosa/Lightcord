// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

// Opaque libdave handles (see third_party/libdave/includes/dave/dave.h).
typedef struct DAVESessionHandle_s *DAVESessionHandle;
typedef struct DAVEEncryptorHandle_s *DAVEEncryptorHandle;
typedef struct DAVEDecryptorHandle_s *DAVEDecryptorHandle;

namespace lightcord::voice {

// Voice gateway opcodes used by the DAVE protocol.
namespace DaveOp {
constexpr int PrepareTransition = 21;
constexpr int ExecuteTransition = 22;
constexpr int TransitionReady = 23;
constexpr int PrepareEpoch = 24;
constexpr int MlsExternalSender = 25;     // binary, server -> client
constexpr int MlsKeyPackage = 26;         // binary, client -> server
constexpr int MlsProposals = 27;          // binary, server -> client
constexpr int MlsCommitWelcome = 28;      // binary, client -> server
constexpr int MlsAnnounceCommit = 29;     // binary, server -> client
constexpr int MlsWelcome = 30;            // binary, server -> client
constexpr int MlsInvalidCommitWelcome = 31;
} // namespace DaveOp

// Drives Discord's Audio & Video End-to-End Encryption (DAVE) for one voice
// call, on top of libdave's C API.
//
// The state machine mirrors the reference DaveSessionManager published with
// libdave: it reacts to the DAVE voice-gateway opcodes, keeps the MLS group
// in sync, and installs per-sender key ratchets on the media encryptor
// (our own audio) and decryptors (one per remote user).
//
// This class does no I/O: outgoing gateway messages are handed to the
// callbacks, so it can be unit-tested without a network.
class DaveSession {
public:
    struct Callbacks {
        // Binary opcode (26 or 28) with its payload, without the opcode byte.
        std::function<void(int opcode, const std::vector<uint8_t> &payload)> sendBinary;
        // JSON opcode 23 (ready) or 31 (invalid commit/welcome) with a transition id.
        std::function<void(int opcode, uint16_t transitionId)> sendTransition;
        // Informational / error messages.
        std::function<void(const std::string &message)> log;
        // Called whenever the active protocol version changes (0 = transport encryption only).
        std::function<void(uint16_t version)> protocolChanged;
    };

    DaveSession(std::string selfUserId, uint64_t groupId, Callbacks callbacks);
    ~DaveSession();
    DaveSession(const DaveSession &) = delete;
    DaveSession &operator=(const DaveSession &) = delete;

    static uint16_t maxProtocolVersion();

    // Receives libdave warnings/errors and MLS failures (stderr by default).
    static void setGlobalLogHandler(std::function<void(const std::string &)> handler);

    // Call membership (voice gateway opcodes 11 and 13).
    void addUser(const std::string &userId);
    void removeUser(const std::string &userId);

    // Voice gateway events.
    void onSessionDescription(uint16_t protocolVersion);                     // op 4
    void onPrepareTransition(uint16_t transitionId, uint16_t protocolVersion); // op 21
    void onExecuteTransition(uint16_t transitionId);                          // op 22
    void onPrepareEpoch(uint64_t epoch, uint16_t protocolVersion);            // op 24
    void onExternalSender(const uint8_t *data, std::size_t size);             // op 25
    void onProposals(const uint8_t *data, std::size_t size);                  // op 27
    void onAnnounceCommit(uint16_t transitionId, const uint8_t *data, std::size_t size); // op 29
    void onWelcome(uint16_t transitionId, const uint8_t *data, std::size_t size);        // op 30

    // Media. `ssrc` is our own SSRC from the voice READY payload.
    void setSelfSsrc(uint32_t ssrc);
    bool encryptOpus(const uint8_t *frame, std::size_t size, std::vector<uint8_t> &out);
    bool decryptOpus(const std::string &userId, const uint8_t *frame, std::size_t size, std::vector<uint8_t> &out);

    uint16_t activeProtocolVersion() const { return m_activeVersion; }
    bool isEncrypted() const { return m_activeVersion > 0; }
    const std::string &selfUserId() const { return m_selfUserId; }

private:
    void handleProtocolInit(uint16_t version);
    void prepareEpoch(uint64_t epoch, uint16_t version);
    void prepareRatchets(uint16_t transitionId, uint16_t version);
    void executeTransition(uint16_t transitionId);
    void maybeSendReady(uint16_t transitionId);
    void sendKeyPackage();
    void setupKeyRatchet(const std::string &userId, uint16_t version);
    DAVEDecryptorHandle decryptorFor(const std::string &userId);
    void setActiveVersion(uint16_t version);
    void log(const std::string &msg) const;

    std::string m_selfUserId;
    uint64_t m_groupId;
    Callbacks m_cb;

    DAVESessionHandle m_session = nullptr;
    DAVEEncryptorHandle m_encryptor = nullptr;
    std::map<std::string, DAVEDecryptorHandle> m_decryptors;

    std::set<std::string> m_users; // remote users in the call
    std::map<uint16_t, uint16_t> m_pendingTransitions; // transition id -> protocol version
    uint16_t m_latestPreparedVersion = 0;
    uint16_t m_activeVersion = 0;
    uint32_t m_selfSsrc = 0;
};

} // namespace lightcord::voice
