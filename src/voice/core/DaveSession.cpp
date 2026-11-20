// SPDX-License-Identifier: MIT
#include "DaveSession.h"

#include <dave/dave.h>

#include <cstdio>
#include <cstring>

namespace lightcord::voice {

namespace {
constexpr uint16_t kInitTransitionId = 0;
constexpr uint16_t kDisabledVersion = 0;
constexpr uint64_t kNewGroupEpoch = 1;

std::function<void(const std::string &)> &globalLogHandler()
{
    static std::function<void(const std::string &)> handler;
    return handler;
}

void emitLog(const std::string &text)
{
    if (globalLogHandler())
        globalLogHandler()(text);
    else
        std::fprintf(stderr, "%s\n", text.c_str());
}

void mlsFailure(const char *source, const char *reason, void *userData)
{
    (void)userData;
    emitLog(std::string("[DAVE] MLS failure in ") + (source ? source : "?") + ": " + (reason ? reason : "?"));
}

void logSink(DAVELoggingSeverity severity, const char *file, int line, const char *message)
{
    if (severity < DAVE_LOGGING_SEVERITY_WARNING || severity == DAVE_LOGGING_SEVERITY_NONE)
        return;
    emitLog(std::string("[DAVE] ") + (file ? file : "") + ":" + std::to_string(line) + " " + (message ? message : ""));
}

struct KeyRatchet {
    explicit KeyRatchet(DAVEKeyRatchetHandle h)
        : handle(h)
    {
    }
    ~KeyRatchet()
    {
        if (handle)
            daveKeyRatchetDestroy(handle);
    }
    KeyRatchet(const KeyRatchet &) = delete;
    KeyRatchet &operator=(const KeyRatchet &) = delete;
    DAVEKeyRatchetHandle handle;
};
} // namespace

void DaveSession::setGlobalLogHandler(std::function<void(const std::string &)> handler)
{
    globalLogHandler() = std::move(handler);
}

uint16_t DaveSession::maxProtocolVersion()
{
    return daveMaxSupportedProtocolVersion();
}

DaveSession::DaveSession(std::string selfUserId, uint64_t groupId, Callbacks callbacks)
    : m_selfUserId(std::move(selfUserId))
    , m_groupId(groupId)
    , m_cb(std::move(callbacks))
{
    static bool sinkInstalled = false;
    if (!sinkInstalled) {
        daveSetLogSinkCallback(&logSink);
        sinkInstalled = true;
    }
    m_session = daveSessionCreate(nullptr, nullptr, &mlsFailure, this);
    m_encryptor = daveEncryptorCreate();
}

DaveSession::~DaveSession()
{
    for (auto &entry : m_decryptors)
        daveDecryptorDestroy(entry.second);
    if (m_encryptor)
        daveEncryptorDestroy(m_encryptor);
    if (m_session)
        daveSessionDestroy(m_session);
}

void DaveSession::log(const std::string &msg) const
{
    if (m_cb.log)
        m_cb.log(msg);
}

void DaveSession::setActiveVersion(uint16_t version)
{
    if (m_activeVersion == version)
        return;
    m_activeVersion = version;
    if (m_cb.protocolChanged)
        m_cb.protocolChanged(version);
}

// ---------------------------------------------------------------------------
// Membership
// ---------------------------------------------------------------------------

void DaveSession::addUser(const std::string &userId)
{
    if (userId.empty() || userId == m_selfUserId)
        return;
    m_users.insert(userId);
    daveSessionAddRecognizedUser(m_session, userId.c_str());
    setupKeyRatchet(userId, m_latestPreparedVersion);
}

void DaveSession::removeUser(const std::string &userId)
{
    m_users.erase(userId);
    daveSessionRemoveRecognizedUser(m_session, userId.c_str());
    auto it = m_decryptors.find(userId);
    if (it != m_decryptors.end()) {
        daveDecryptorDestroy(it->second);
        m_decryptors.erase(it);
    }
}

// ---------------------------------------------------------------------------
// Gateway events
// ---------------------------------------------------------------------------

void DaveSession::onSessionDescription(uint16_t protocolVersion)
{
    log("DAVE protocol version from session description: " + std::to_string(protocolVersion));
    handleProtocolInit(protocolVersion);
}

void DaveSession::onPrepareTransition(uint16_t transitionId, uint16_t protocolVersion)
{
    prepareRatchets(transitionId, protocolVersion);
    maybeSendReady(transitionId);
}

void DaveSession::onExecuteTransition(uint16_t transitionId)
{
    executeTransition(transitionId);
}

void DaveSession::onPrepareEpoch(uint64_t epoch, uint16_t protocolVersion)
{
    prepareEpoch(epoch, protocolVersion);
    if (epoch == kNewGroupEpoch)
        sendKeyPackage();
}

void DaveSession::onExternalSender(const uint8_t *data, std::size_t size)
{
    daveSessionSetExternalSender(m_session, data, size);
}

void DaveSession::onProposals(const uint8_t *data, std::size_t size)
{
    uint8_t *commitWelcome = nullptr;
    std::size_t length = 0;
    daveSessionProcessProposals(m_session, data, size, &commitWelcome, &length);
    if (commitWelcome && length > 0 && m_cb.sendBinary)
        m_cb.sendBinary(DaveOp::MlsCommitWelcome, std::vector<uint8_t>(commitWelcome, commitWelcome + length));
    daveFree(commitWelcome);
}

void DaveSession::onAnnounceCommit(uint16_t transitionId, const uint8_t *data, std::size_t size)
{
    DAVECommitResultHandle result = daveSessionProcessCommit(m_session, data, size);
    if (!result) {
        log("MLS commit could not be processed");
        if (m_cb.sendTransition)
            m_cb.sendTransition(DaveOp::MlsInvalidCommitWelcome, transitionId);
        handleProtocolInit(daveSessionGetProtocolVersion(m_session));
        return;
    }
    const bool ignored = daveCommitResultIsIgnored(result);
    const bool failed = daveCommitResultIsFailed(result);
    daveCommitResultDestroy(result);

    if (ignored)
        return;
    if (failed) {
        log("MLS commit rejected; requesting re-add");
        if (m_cb.sendTransition)
            m_cb.sendTransition(DaveOp::MlsInvalidCommitWelcome, transitionId);
        handleProtocolInit(daveSessionGetProtocolVersion(m_session));
        return;
    }
    prepareRatchets(transitionId, daveSessionGetProtocolVersion(m_session));
    maybeSendReady(transitionId);
}

void DaveSession::onWelcome(uint16_t transitionId, const uint8_t *data, std::size_t size)
{
    DAVEWelcomeResultHandle result = daveSessionProcessWelcome(m_session, data, size);
    if (!result) {
        log("MLS welcome could not be processed; requesting re-add");
        if (m_cb.sendTransition)
            m_cb.sendTransition(DaveOp::MlsInvalidCommitWelcome, transitionId);
        sendKeyPackage();
        return;
    }
    daveWelcomeResultDestroy(result);
    prepareRatchets(transitionId, daveSessionGetProtocolVersion(m_session));
    maybeSendReady(transitionId);
}

// ---------------------------------------------------------------------------
// Internal state machine
// ---------------------------------------------------------------------------

void DaveSession::handleProtocolInit(uint16_t version)
{
    if (version > kDisabledVersion) {
        prepareEpoch(kNewGroupEpoch, version);
        sendKeyPackage();
    } else {
        prepareRatchets(kInitTransitionId, version);
        executeTransition(kInitTransitionId);
    }
}

void DaveSession::prepareEpoch(uint64_t epoch, uint16_t version)
{
    if (epoch == kNewGroupEpoch)
        daveSessionInit(m_session, version, m_groupId, m_selfUserId.c_str());
}

void DaveSession::prepareRatchets(uint16_t transitionId, uint16_t version)
{
    for (const std::string &user : m_users)
        setupKeyRatchet(user, version);

    if (transitionId == kInitTransitionId)
        setupKeyRatchet(m_selfUserId, version);
    else
        m_pendingTransitions[transitionId] = version;

    m_latestPreparedVersion = version;
}

void DaveSession::executeTransition(uint16_t transitionId)
{
    auto it = m_pendingTransitions.find(transitionId);
    if (it == m_pendingTransitions.end()) {
        // The init transition is applied immediately in prepareRatchets().
        return;
    }
    const uint16_t version = it->second;
    m_pendingTransitions.erase(it);

    if (version == kDisabledVersion)
        daveSessionReset(m_session);
    setupKeyRatchet(m_selfUserId, version);
}

void DaveSession::maybeSendReady(uint16_t transitionId)
{
    if (transitionId != kInitTransitionId && m_cb.sendTransition)
        m_cb.sendTransition(DaveOp::TransitionReady, transitionId);
}

void DaveSession::sendKeyPackage()
{
    uint8_t *keyPackage = nullptr;
    std::size_t length = 0;
    daveSessionGetMarshalledKeyPackage(m_session, &keyPackage, &length);
    if (keyPackage && length > 0 && m_cb.sendBinary)
        m_cb.sendBinary(DaveOp::MlsKeyPackage, std::vector<uint8_t>(keyPackage, keyPackage + length));
    else
        log("Could not create an MLS key package");
    daveFree(keyPackage);
}

DAVEDecryptorHandle DaveSession::decryptorFor(const std::string &userId)
{
    auto it = m_decryptors.find(userId);
    if (it != m_decryptors.end())
        return it->second;
    DAVEDecryptorHandle d = daveDecryptorCreate();
    m_decryptors.emplace(userId, d);
    return d;
}

void DaveSession::setupKeyRatchet(const std::string &userId, uint16_t version)
{
    const bool self = userId == m_selfUserId;

    if (version == kDisabledVersion) {
        if (self) {
            daveEncryptorSetPassthroughMode(m_encryptor, true);
            setActiveVersion(kDisabledVersion);
        } else {
            daveDecryptorTransitionToPassthroughMode(decryptorFor(userId), true);
        }
        return;
    }

    KeyRatchet ratchet(daveSessionGetKeyRatchet(m_session, userId.c_str()));
    if (!ratchet.handle) {
        // The user is not (yet) part of the MLS group.
        return;
    }
    if (self) {
        daveEncryptorSetKeyRatchet(m_encryptor, ratchet.handle);
        daveEncryptorSetPassthroughMode(m_encryptor, false);
        setActiveVersion(version);
    } else {
        DAVEDecryptorHandle d = decryptorFor(userId);
        daveDecryptorTransitionToKeyRatchet(d, ratchet.handle);
        daveDecryptorTransitionToPassthroughMode(d, false);
    }
}

// ---------------------------------------------------------------------------
// Media
// ---------------------------------------------------------------------------

void DaveSession::setSelfSsrc(uint32_t ssrc)
{
    m_selfSsrc = ssrc;
    daveEncryptorAssignSsrcToCodec(m_encryptor, ssrc, DAVE_CODEC_OPUS);
}

bool DaveSession::encryptOpus(const uint8_t *frame, std::size_t size, std::vector<uint8_t> &out)
{
    if (daveEncryptorIsPassthroughMode(m_encryptor)) {
        out.assign(frame, frame + size);
        return true;
    }
    out.resize(daveEncryptorGetMaxCiphertextByteSize(m_encryptor, DAVE_MEDIA_TYPE_AUDIO, size));
    std::size_t written = 0;
    const DAVEEncryptorResultCode rc = daveEncryptorEncrypt(m_encryptor, DAVE_MEDIA_TYPE_AUDIO, m_selfSsrc, frame,
                                                            size, out.data(), out.size(), &written);
    if (rc != DAVE_ENCRYPTOR_RESULT_CODE_SUCCESS) {
        out.clear();
        return false;
    }
    out.resize(written);
    return true;
}

bool DaveSession::decryptOpus(const std::string &userId, const uint8_t *frame, std::size_t size,
                              std::vector<uint8_t> &out)
{
    auto it = m_decryptors.find(userId);
    if (it == m_decryptors.end()) {
        if (m_activeVersion == kDisabledVersion) {
            out.assign(frame, frame + size);
            return true;
        }
        return false;
    }
    out.resize(daveDecryptorGetMaxPlaintextByteSize(it->second, DAVE_MEDIA_TYPE_AUDIO, size));
    std::size_t written = 0;
    const DAVEDecryptorResultCode rc =
        daveDecryptorDecrypt(it->second, DAVE_MEDIA_TYPE_AUDIO, frame, size, out.data(), out.size(), &written);
    if (rc != DAVE_DECRYPTOR_RESULT_CODE_SUCCESS) {
        out.clear();
        return false;
    }
    out.resize(written);
    return true;
}

} // namespace lightcord::voice
