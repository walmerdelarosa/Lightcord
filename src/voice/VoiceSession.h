// SPDX-License-Identifier: MIT
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QThread>

namespace lightcord {

class Gateway;
class VoiceEngine;

// Joins and leaves voice channels.
//
//  1. Signalling on the main gateway (opcode 4). Discord answers with
//     VOICE_STATE_UPDATE (our voice session id) and VOICE_SERVER_UPDATE
//     (voice endpoint + token).
//  2. Media: a VoiceEngine running in its own thread connects to the voice
//     server, negotiates transport encryption and DAVE end-to-end encryption,
//     and streams Opus audio both ways.
class VoiceSession : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Requested, Connecting, Connected };
    Q_ENUM(State)

    struct TransportInfo {
        QString guildId;
        QString channelId;
        QString userId;
        QString sessionId;
        QString endpoint;
        QString token;
    };

    explicit VoiceSession(Gateway *gateway, QObject *parent = nullptr);
    ~VoiceSession() override;

    static bool isMediaAvailable();

    void join(const QString &guildId, const QString &channelId, const QString &myUserId);
    void leave();
    void setSelfMute(bool mute);
    void setSelfDeaf(bool deaf);

    State state() const { return m_state; }
    QString channelId() const { return m_info.channelId; }
    QString guildId() const { return m_info.guildId; }
    bool selfMute() const { return m_selfMute; }
    bool selfDeaf() const { return m_selfDeaf; }

    void handleVoiceStateUpdate(const QJsonObject &data);
    void handleVoiceServerUpdate(const QJsonObject &data);

signals:
    void stateChanged(lightcord::VoiceSession::State state);
    void statusText(const QString &text);
    void encryptionChanged(int daveVersion, const QString &transportMode);
    void speakingChanged(const QString &userId, bool speaking);
    void errorOccurred(const QString &message);

private:
    void setState(State s);
    void sendState();
    void maybeStartMedia();
    void stopMedia();
    void reloadAudioSettings();

    Gateway *m_gateway;
    State m_state = State::Idle;
    TransportInfo m_info;
    bool m_selfMute = false;
    bool m_selfDeaf = false;

    QThread m_thread;
    VoiceEngine *m_engine = nullptr;
    QString m_startedEndpoint;
    QString m_startedToken;
};

} // namespace lightcord
