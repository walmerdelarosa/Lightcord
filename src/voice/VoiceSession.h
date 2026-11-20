// SPDX-License-Identifier: MIT
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

namespace lightcord {

class Gateway;

// Voice channel signalling (main gateway side).
//
// Joining a voice channel on Discord takes two stages:
//   1. Signalling on the main gateway (opcode 4) - implemented here. Discord
//      answers with VOICE_STATE_UPDATE (our session_id) and
//      VOICE_SERVER_UPDATE (voice endpoint + token).
//   2. Media transport: voice WebSocket, UDP IP discovery, transport
//      encryption, the mandatory DAVE end-to-end encryption (MLS) and Opus
//      audio. This is planned for v0.2 - see docs/ROADMAP.md.
//
// Because stage 2 is not implemented yet, joining is only possible when the
// user enables "Experimental voice signalling" in the settings. The UI makes
// it clear that no audio is transmitted.
class VoiceSession : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Requested, ServerAssigned };
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

    void join(const QString &guildId, const QString &channelId, const QString &myUserId);
    void leave();
    void setSelfMute(bool mute);
    void setSelfDeaf(bool deaf);

    State state() const { return m_state; }
    QString channelId() const { return m_info.channelId; }
    bool selfMute() const { return m_selfMute; }
    bool selfDeaf() const { return m_selfDeaf; }

    void handleVoiceStateUpdate(const QJsonObject &data);
    void handleVoiceServerUpdate(const QJsonObject &data);

signals:
    void stateChanged(lightcord::VoiceSession::State state);
    void transportInfoReady(const lightcord::VoiceSession::TransportInfo &info);

private:
    void setState(State s);
    void sendState();
    void maybeReady();

    Gateway *m_gateway;
    State m_state = State::Idle;
    TransportInfo m_info;
    bool m_selfMute = false;
    bool m_selfDeaf = false;
};

} // namespace lightcord
