// SPDX-License-Identifier: MIT
#include "VoiceEngine.h"

#include "VoiceGateway.h"
#include "audio/AudioPipeline.h"
#include "audio/PlaybackMixer.h"
#include "core/Settings.h"
#include "voice/core/DaveSession.h"
#include "voice/core/IpDiscovery.h"
#include "voice/core/Rtp.h"

#include <QJsonObject>
#include <QMediaDevices>
#include <QNetworkDatagram>
#include <QUdpSocket>

#include <cmath>
#include <cstring>

namespace lightcord {

using namespace voice;

namespace {
constexpr int kDiscoveryAttempts = 5;
constexpr int kKeepAliveMs = 5000;
constexpr int kSilenceFramesOnStop = 5;
constexpr int kRemoteSpeakingTimeoutMs = 300;
constexpr double kVoiceRmsThreshold = 0.0035; // about -49 dBFS, used when the voice gate is off
constexpr int kVoiceHoldFrames = 15;          // 300 ms

QAudioDevice findDevice(const QList<QAudioDevice> &devices, const QByteArray &id)
{
    if (id.isEmpty())
        return {};
    for (const QAudioDevice &d : devices) {
        if (d.id() == id)
            return d;
    }
    return {};
}
} // namespace

VoiceEngine::VoiceEngine(QObject *parent)
    : QObject(parent)
    , m_discoveryTimer(this)
    , m_keepAliveTimer(this)
    , m_speakingTimer(this)
{
    // The timers are children so that moveToThread() takes them along.
    m_discoveryTimer.setInterval(1000);
    connect(&m_discoveryTimer, &QTimer::timeout, this, &VoiceEngine::sendIpDiscovery);
    m_keepAliveTimer.setInterval(kKeepAliveMs);
    connect(&m_keepAliveTimer, &QTimer::timeout, this, &VoiceEngine::sendKeepAlive);
    m_speakingTimer.setInterval(100);
    connect(&m_speakingTimer, &QTimer::timeout, this, &VoiceEngine::checkRemoteSpeaking);
}

VoiceEngine::~VoiceEngine()
{
    stop();
}

void VoiceEngine::start(const QString &endpoint, const QString &guildId, const QString &channelId,
                        const QString &userId, const QString &sessionId, const QString &token)
{
    stop();
    m_userId = userId;
    m_channelId = channelId;
    m_phase = Phase::Connecting;
    emit statusChanged(tr("Connecting to the voice server…"));

    DaveSession::Callbacks cb;
    cb.sendBinary = [this](int op, const std::vector<uint8_t> &payload) {
        if (m_gateway)
            m_gateway->sendBinary(op, QByteArray(reinterpret_cast<const char *>(payload.data()),
                                                 static_cast<qsizetype>(payload.size())));
    };
    cb.sendTransition = [this](int op, uint16_t transitionId) {
        if (m_gateway)
            m_gateway->sendJson(op, QJsonObject{{"transition_id", transitionId}});
    };
    cb.log = [](const std::string &msg) { qInfo("[voice] %s", msg.c_str()); };
    cb.protocolChanged = [this](uint16_t version) {
        emit encryptionChanged(version, QString::fromLatin1(TransportCrypto::modeName(m_mode)));
    };
    m_dave = std::make_unique<DaveSession>(userId.toStdString(), channelId.toULongLong(), cb);

    m_gateway = new VoiceGateway(this);
    connect(m_gateway, &VoiceGateway::ready, this, &VoiceEngine::onReady);
    connect(m_gateway, &VoiceGateway::sessionDescription, this, &VoiceEngine::onSessionDescription);
    connect(m_gateway, &VoiceGateway::daveJson, this, &VoiceEngine::onDaveJson);
    connect(m_gateway, &VoiceGateway::daveBinary, this, &VoiceEngine::onDaveBinary);
    connect(m_gateway, &VoiceGateway::speaking, this, &VoiceEngine::onSpeaking);
    connect(m_gateway, &VoiceGateway::clientsConnected, this, &VoiceEngine::onClientsConnected);
    connect(m_gateway, &VoiceGateway::clientDisconnected, this, &VoiceEngine::onClientDisconnected);
    connect(m_gateway, &VoiceGateway::fatalError, this, &VoiceEngine::fail);
    connect(m_gateway, &VoiceGateway::reconnecting, this,
            [this]() { emit statusChanged(tr("Voice connection lost, reconnecting…")); });
    connect(m_gateway, &VoiceGateway::resumed, this, [this]() { emit statusChanged(tr("Voice connected")); });

    VoiceGateway::Params params;
    params.endpoint = endpoint;
    params.serverId = guildId;
    params.userId = userId;
    params.sessionId = sessionId;
    params.token = token;
    params.maxDaveVersion = DaveSession::maxProtocolVersion();
    m_gateway->open(params);
}

void VoiceEngine::stop()
{
    const bool wasActive = m_phase != Phase::Idle;
    if (m_transmitting && m_phase == Phase::Connected)
        setTransmitting(false);
    stopAudio();
    m_discoveryTimer.stop();
    m_keepAliveTimer.stop();
    m_speakingTimer.stop();
    if (m_gateway) {
        m_gateway->close();
        m_gateway->deleteLater();
        m_gateway = nullptr;
    }
    if (m_udp) {
        m_udp->close();
        m_udp->deleteLater();
        m_udp = nullptr;
    }
    for (auto &entry : m_remotes) {
        if (entry.second.speaking)
            emit speakingChanged(entry.second.userId, false);
    }
    m_remotes.clear();
    m_ssrcByUser.clear();
    m_users.clear();
    m_dave.reset();
    m_pcmAccumulator.clear();
    m_transmitting = false;
    m_phase = Phase::Idle;
    if (wasActive) {
        emit connectedChanged(false);
        emit statusChanged(QString());
    }
}

void VoiceEngine::fail(const QString &reason)
{
    stop();
    emit failed(reason);
}

void VoiceEngine::setSelfMute(bool mute)
{
    m_selfMute = mute;
    if (m_capture)
        m_capture->setMuted(mute);
}

void VoiceEngine::setSelfDeaf(bool deaf)
{
    m_selfDeaf = deaf;
    if (m_playback) {
        m_playback->setMuted(deaf);
        if (deaf)
            m_playback->clear();
    }
}

void VoiceEngine::reloadAudioSettings()
{
    const auto ns = Settings::instance().noiseSuppression();
    m_useGate = ns.enabled && ns.voiceGate;
    if (m_capture) {
        m_capture->setSuppressorSettings(ns);
        m_capture->setInputDevice(findDevice(QMediaDevices::audioInputs(), Settings::instance().inputDeviceId()));
    }
    if (m_playback) {
        m_playback->start(findDevice(QMediaDevices::audioOutputs(), Settings::instance().outputDeviceId()));
        m_playback->setMuted(m_selfDeaf);
    }
}

// ---------------------------------------------------------------------------
// Handshake
// ---------------------------------------------------------------------------

void VoiceEngine::onReady(quint32 ssrc, const QString &ip, quint16 port, const QStringList &modes)
{
    m_ssrc = ssrc;
    m_dave->setSelfSsrc(ssrc);
    m_serverAddress = QHostAddress(ip);
    m_serverPort = port;

    std::vector<std::string> offered;
    for (const QString &m : modes)
        offered.push_back(m.toStdString());
    m_mode = TransportCrypto::chooseMode(offered);
    if (m_mode == TransportCrypto::Mode::None) {
        fail(tr("The voice server offered no supported encryption mode."));
        return;
    }

    m_udp = new QUdpSocket(this);
    if (!m_udp->bind(QHostAddress::AnyIPv4, 0)) {
        fail(tr("Could not open a UDP socket for voice."));
        return;
    }
    m_udp->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 256 * 1024);
    connect(m_udp, &QUdpSocket::readyRead, this, &VoiceEngine::onUdpReadyRead);

    m_phase = Phase::Discovering;
    m_discoveryAttempts = 0;
    sendIpDiscovery();
    m_discoveryTimer.start();
}

void VoiceEngine::sendIpDiscovery()
{
    if (m_phase != Phase::Discovering)
        return;
    if (++m_discoveryAttempts > kDiscoveryAttempts) {
        fail(tr("No answer from the voice server over UDP. A firewall may be blocking voice traffic."));
        return;
    }
    const auto packet = buildIpDiscoveryRequest(m_ssrc);
    m_udp->writeDatagram(reinterpret_cast<const char *>(packet.data()), static_cast<qint64>(packet.size()),
                         m_serverAddress, m_serverPort);
}

void VoiceEngine::onSessionDescription(const QString &mode, const QByteArray &key, int daveVersion)
{
    const TransportCrypto::Mode selected = TransportCrypto::modeFromName(mode.toStdString());
    if (selected == TransportCrypto::Mode::None ||
        !m_crypto.setKey(selected, reinterpret_cast<const uint8_t *>(key.constData()),
                         static_cast<std::size_t>(key.size()))) {
        fail(tr("Invalid voice session key."));
        return;
    }
    m_mode = selected;
    m_dave->onSessionDescription(static_cast<uint16_t>(daveVersion));

    m_phase = Phase::Connected;
    startAudio();
    m_keepAliveTimer.start();
    m_speakingTimer.start();
    emit connectedChanged(true);
    emit statusChanged(tr("Voice connected"));
    emit encryptionChanged(m_dave->activeProtocolVersion(), mode);
}

void VoiceEngine::onDaveJson(int op, const QJsonObject &d)
{
    if (!m_dave)
        return;
    const auto transitionId = static_cast<uint16_t>(d.value("transition_id").toInt());
    const auto version = static_cast<uint16_t>(d.value("protocol_version").toInt());
    switch (op) {
    case DaveOp::PrepareTransition:
        m_dave->onPrepareTransition(transitionId, version);
        break;
    case DaveOp::ExecuteTransition:
        m_dave->onExecuteTransition(transitionId);
        break;
    case DaveOp::PrepareEpoch:
        m_dave->onPrepareEpoch(static_cast<uint64_t>(d.value("epoch").toDouble()), version);
        break;
    default:
        break;
    }
}

void VoiceEngine::onDaveBinary(int op, const QByteArray &payload)
{
    if (!m_dave)
        return;
    const auto *p = reinterpret_cast<const uint8_t *>(payload.constData());
    const auto size = static_cast<std::size_t>(payload.size());
    switch (op) {
    case DaveOp::MlsExternalSender:
        m_dave->onExternalSender(p, size);
        break;
    case DaveOp::MlsProposals:
        m_dave->onProposals(p, size);
        break;
    case DaveOp::MlsAnnounceCommit:
        if (size >= 2)
            m_dave->onAnnounceCommit(readBe16(p), p + 2, size - 2);
        break;
    case DaveOp::MlsWelcome:
        if (size >= 2)
            m_dave->onWelcome(readBe16(p), p + 2, size - 2);
        break;
    default:
        break;
    }
}

void VoiceEngine::onSpeaking(const QString &userId, quint32 ssrc, int flags)
{
    Q_UNUSED(flags);
    if (userId.isEmpty() || userId == m_userId)
        return;
    m_remotes[ssrc].userId = userId;
    m_ssrcByUser.insert(userId, ssrc);
    if (!m_users.contains(userId)) {
        m_users.insert(userId);
        if (m_dave)
            m_dave->addUser(userId.toStdString());
    }
}

void VoiceEngine::onClientsConnected(const QStringList &userIds)
{
    for (const QString &id : userIds) {
        if (id == m_userId || m_users.contains(id))
            continue;
        m_users.insert(id);
        if (m_dave)
            m_dave->addUser(id.toStdString());
    }
}

void VoiceEngine::onClientDisconnected(const QString &userId)
{
    m_users.remove(userId);
    if (m_dave)
        m_dave->removeUser(userId.toStdString());
    if (m_ssrcByUser.contains(userId)) {
        const quint32 ssrc = m_ssrcByUser.take(userId);
        m_remotes.erase(ssrc);
        if (m_playback)
            m_playback->removeSource(ssrc);
    }
    emit speakingChanged(userId, false);
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------

void VoiceEngine::startAudio()
{
    stopAudio();
    const auto ns = Settings::instance().noiseSuppression();
    m_useGate = ns.enabled && ns.voiceGate;

    m_capture = new audio::AudioPipeline(this);
    m_capture->setInputDevice(findDevice(QMediaDevices::audioInputs(), Settings::instance().inputDeviceId()));
    m_capture->setSuppressorSettings(ns);
    m_capture->setMuted(m_selfMute);
    connect(m_capture, &audio::AudioPipeline::levels, this, &VoiceEngine::onCaptureLevels);
    connect(m_capture, &audio::AudioPipeline::processedAudio, this, &VoiceEngine::onCapturedAudio);
    connect(m_capture, &audio::AudioPipeline::errorOccurred, this, &VoiceEngine::statusChanged);
    m_capture->start();

    m_playback = new audio::PlaybackMixer(this);
    m_playback->start(findDevice(QMediaDevices::audioOutputs(), Settings::instance().outputDeviceId()));
    m_playback->setMuted(m_selfDeaf);
}

void VoiceEngine::stopAudio()
{
    if (m_capture) {
        m_capture->stop();
        m_capture->deleteLater();
        m_capture = nullptr;
    }
    if (m_playback) {
        m_playback->stop();
        m_playback->deleteLater();
        m_playback = nullptr;
    }
}

void VoiceEngine::onCaptureLevels(float, float, float, bool gateOpen)
{
    m_gateOpen = gateOpen;
}

void VoiceEngine::onCapturedAudio(const QByteArray &pcm)
{
    const auto *samples = reinterpret_cast<const int16_t *>(pcm.constData());
    const std::size_t count = static_cast<std::size_t>(pcm.size()) / sizeof(int16_t);
    m_pcmAccumulator.insert(m_pcmAccumulator.end(), samples, samples + count);

    std::size_t offset = 0;
    while (m_pcmAccumulator.size() - offset >= static_cast<std::size_t>(kFrameSamples)) {
        processFrame(m_pcmAccumulator.data() + offset);
        offset += kFrameSamples;
    }
    m_pcmAccumulator.erase(m_pcmAccumulator.begin(), m_pcmAccumulator.begin() + static_cast<std::ptrdiff_t>(offset));
}

void VoiceEngine::processFrame(const int16_t *mono)
{
    if (m_phase != Phase::Connected)
        return;

    bool voiceDetected = m_gateOpen;
    if (!m_useGate) {
        double acc = 0.0;
        for (int i = 0; i < kFrameSamples; ++i) {
            const double v = mono[i] / 32768.0;
            acc += v * v;
        }
        if (std::sqrt(acc / kFrameSamples) > kVoiceRmsThreshold)
            m_holdFrames = kVoiceHoldFrames;
        else if (m_holdFrames > 0)
            --m_holdFrames;
        voiceDetected = m_holdFrames > 0;
    }

    const bool active = !m_selfMute && !m_selfDeaf && voiceDetected;
    if (!active) {
        if (m_transmitting)
            setTransmitting(false);
        return;
    }
    if (!m_transmitting)
        setTransmitting(true);

    int16_t stereo[kFrameSamples * kChannels];
    for (int i = 0; i < kFrameSamples; ++i) {
        stereo[2 * i] = mono[i];
        stereo[2 * i + 1] = mono[i];
    }
    std::vector<uint8_t> opus;
    if (m_encoder.encode(stereo, opus))
        sendOpusPacket(opus.data(), opus.size(), false);
}

void VoiceEngine::setTransmitting(bool on)
{
    if (on == m_transmitting)
        return;
    m_transmitting = on;
    if (on) {
        // Opcode 5 must be sent before any audio.
        m_gateway->sendSpeaking(1, m_ssrc);
    } else {
        // Five frames of silence avoid Opus interpolation artefacts on the receivers.
        for (int i = 0; i < kSilenceFramesOnStop; ++i)
            sendOpusPacket(kOpusSilenceFrame, sizeof kOpusSilenceFrame, true);
        m_gateway->sendSpeaking(0, m_ssrc);
    }
    emit speakingChanged(m_userId, on);
}

void VoiceEngine::sendOpusPacket(const uint8_t *data, std::size_t size, bool isSilence)
{
    if (!m_udp || !m_crypto.ready())
        return;

    std::vector<uint8_t> media;
    if (isSilence) {
        // libdave receivers pass the 3-byte Opus silence frame through unencrypted.
        media.assign(data, data + size);
    } else if (!m_dave || !m_dave->encryptOpus(data, size, media)) {
        return; // E2EE keys not ready yet: never send unprotected media
    }

    uint8_t header[kRtpHeaderSize];
    writeRtpHeader(header, m_sequence, m_timestamp, m_ssrc);
    std::vector<uint8_t> packet;
    if (m_crypto.seal(header, sizeof header, media.data(), media.size(), packet)) {
        m_udp->writeDatagram(reinterpret_cast<const char *>(packet.data()), static_cast<qint64>(packet.size()),
                             m_serverAddress, m_serverPort);
    }
    ++m_sequence;
    m_timestamp += kFrameSamples;
}

void VoiceEngine::sendKeepAlive()
{
    if (!m_udp || m_phase != Phase::Connected)
        return;
    uint8_t packet[8] = {};
    const quint32 counter = m_keepAliveCounter++;
    std::memcpy(packet, &counter, sizeof counter); // little-endian on all supported targets
    m_udp->writeDatagram(reinterpret_cast<const char *>(packet), sizeof packet, m_serverAddress, m_serverPort);
}

// ---------------------------------------------------------------------------
// Receiving
// ---------------------------------------------------------------------------

void VoiceEngine::onUdpReadyRead()
{
    while (m_udp && m_udp->hasPendingDatagrams()) {
        const QNetworkDatagram datagram = m_udp->receiveDatagram();
        const QByteArray data = datagram.data();

        if (m_phase == Phase::Discovering) {
            std::string address;
            uint16_t port = 0;
            if (parseIpDiscoveryResponse(reinterpret_cast<const uint8_t *>(data.constData()),
                                         static_cast<std::size_t>(data.size()), address, port)) {
                m_discoveryTimer.stop();
                m_phase = Phase::Selecting;
                m_gateway->selectProtocol(QString::fromStdString(address), port,
                                          QString::fromLatin1(TransportCrypto::modeName(m_mode)));
            }
            continue;
        }
        if (m_phase == Phase::Connected)
            handleRtp(data);
    }
}

void VoiceEngine::handleRtp(const QByteArray &datagram)
{
    const auto *p = reinterpret_cast<const uint8_t *>(datagram.constData());
    const auto size = static_cast<std::size_t>(datagram.size());
    RtpInfo info;
    if (!parseRtp(p, size, info) || info.payloadType != kOpusPayloadType || info.ssrc == m_ssrc)
        return;

    std::vector<uint8_t> media;
    if (!m_crypto.open(p, size, info.unencryptedHeaderSize, info.extensionBodySize, media))
        return;

    Remote &remote = m_remotes[info.ssrc];
    if (remote.userId.isEmpty())
        return; // unknown sender: we cannot pick the right E2EE key yet

    // Drop late or duplicated packets; conceal small gaps.
    int gap = 0;
    if (remote.hasSequence) {
        const quint16 delta = static_cast<quint16>(info.sequence - remote.lastSequence);
        if (delta == 0 || delta > 0x8000)
            return;
        gap = delta - 1;
    }
    remote.lastSequence = info.sequence;
    remote.hasSequence = true;

    std::vector<uint8_t> opus;
    if (!m_dave || !m_dave->decryptOpus(remote.userId.toStdString(), media.data(), media.size(), opus))
        return;

    const bool silence = opus.size() == sizeof kOpusSilenceFrame &&
                         std::memcmp(opus.data(), kOpusSilenceFrame, sizeof kOpusSilenceFrame) == 0;
    if (silence) {
        if (remote.speaking) {
            remote.speaking = false;
            emit speakingChanged(remote.userId, false);
        }
        return;
    }

    remote.lastPacket.restart();
    if (!remote.speaking) {
        remote.speaking = true;
        emit speakingChanged(remote.userId, true);
    }
    if (m_selfDeaf || !m_playback)
        return;

    if (!remote.decoder)
        remote.decoder = std::make_unique<OpusFrameDecoder>();
    std::vector<int16_t> pcm;
    for (int i = 0; i < gap && i < 3; ++i) {
        if (remote.decoder->conceal(pcm) > 0)
            m_playback->push(info.ssrc, pcm.data(), pcm.size() / 2);
    }
    if (remote.decoder->decode(opus.data(), opus.size(), pcm) > 0)
        m_playback->push(info.ssrc, pcm.data(), pcm.size() / 2);
}

void VoiceEngine::checkRemoteSpeaking()
{
    for (auto &entry : m_remotes) {
        Remote &r = entry.second;
        if (r.speaking && r.lastPacket.isValid() && r.lastPacket.elapsed() > kRemoteSpeakingTimeoutMs) {
            r.speaking = false;
            emit speakingChanged(r.userId, false);
        }
    }
}

} // namespace lightcord
