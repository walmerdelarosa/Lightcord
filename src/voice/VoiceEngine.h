// SPDX-License-Identifier: MIT
#pragma once

#include "voice/core/OpusCodec.h"
#include "voice/core/TransportCrypto.h"

#include <QElapsedTimer>
#include <QHash>
#include <QHostAddress>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>

#include <map>
#include <memory>
#include <vector>

class QUdpSocket;

namespace lightcord {

class VoiceGateway;

namespace audio {
class AudioPipeline;
class PlaybackMixer;
} // namespace audio

namespace voice {
class DaveSession;
}

// The complete voice client for one call. Lives in its own thread (see
// VoiceSession) and talks to the UI only through queued signals.
//
// Send path:    microphone -> AudioPipeline (noise suppression) -> 20 ms frames
//               -> Opus -> DAVE E2EE -> RTP + transport AEAD -> UDP
// Receive path: UDP -> transport AEAD -> DAVE E2EE (per sender) -> Opus
//               -> per-speaker jitter buffer -> PlaybackMixer -> speakers
class VoiceEngine : public QObject {
    Q_OBJECT
public:
    explicit VoiceEngine(QObject *parent = nullptr);
    ~VoiceEngine() override;

    // All methods must be called from the engine's thread (use QMetaObject::invokeMethod).
    void start(const QString &endpoint, const QString &guildId, const QString &channelId, const QString &userId,
               const QString &sessionId, const QString &token);
    void stop();
    void setSelfMute(bool mute);
    void setSelfDeaf(bool deaf);
    void reloadAudioSettings();

signals:
    void statusChanged(const QString &text);
    void connectedChanged(bool connected);
    void encryptionChanged(int daveVersion, const QString &transportMode);
    void speakingChanged(const QString &userId, bool speaking);
    void failed(const QString &reason);

private:
    enum class Phase { Idle, Connecting, Discovering, Selecting, Connected };

    void onReady(quint32 ssrc, const QString &ip, quint16 port, const QStringList &modes);
    void onSessionDescription(const QString &mode, const QByteArray &key, int daveVersion);
    void onDaveJson(int op, const QJsonObject &d);
    void onDaveBinary(int op, const QByteArray &payload);
    void onSpeaking(const QString &userId, quint32 ssrc, int flags);
    void onClientsConnected(const QStringList &userIds);
    void onClientDisconnected(const QString &userId);
    void onUdpReadyRead();
    void sendIpDiscovery();
    void sendKeepAlive();
    void checkRemoteSpeaking();
    void startAudio();
    void stopAudio();

    void onCapturedAudio(const QByteArray &pcm);
    void onCaptureLevels(float in, float out, float vad, bool gateOpen);
    void processFrame(const int16_t *mono);
    void setTransmitting(bool on);
    void sendOpusPacket(const uint8_t *data, std::size_t size, bool alreadyFinal);
    void handleRtp(const QByteArray &datagram);
    void fail(const QString &reason);

    Phase m_phase = Phase::Idle;
    VoiceGateway *m_gateway = nullptr;
    QUdpSocket *m_udp = nullptr;
    audio::AudioPipeline *m_capture = nullptr;
    audio::PlaybackMixer *m_playback = nullptr;
    std::unique_ptr<voice::DaveSession> m_dave;
    voice::TransportCrypto m_crypto;
    voice::OpusFrameEncoder m_encoder;
    voice::TransportCrypto::Mode m_mode = voice::TransportCrypto::Mode::None;

    QString m_userId;
    QString m_channelId;
    quint32 m_ssrc = 0;
    QHostAddress m_serverAddress;
    quint16 m_serverPort = 0;

    QTimer m_discoveryTimer;
    int m_discoveryAttempts = 0;
    QTimer m_keepAliveTimer;
    quint32 m_keepAliveCounter = 0;
    QTimer m_speakingTimer;

    // Sending state
    std::vector<int16_t> m_pcmAccumulator; // mono 48 kHz
    quint16 m_sequence = 0;
    quint32 m_timestamp = 0;
    bool m_selfMute = false;
    bool m_selfDeaf = false;
    bool m_transmitting = false;
    bool m_gateOpen = true;
    bool m_useGate = true;
    int m_holdFrames = 0;

    // Receiving state
    struct Remote {
        QString userId;
        std::unique_ptr<voice::OpusFrameDecoder> decoder;
        quint16 lastSequence = 0;
        bool hasSequence = false;
        bool speaking = false;
        QElapsedTimer lastPacket;
    };
    std::map<quint32, Remote> m_remotes;     // by SSRC
    QHash<QString, quint32> m_ssrcByUser;
    QSet<QString> m_users;
};

} // namespace lightcord
