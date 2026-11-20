// SPDX-License-Identifier: MIT
#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QWebSocket>

namespace lightcord {

// Discord gateway (v10, JSON encoding) connection.
//
// Implements HELLO / HEARTBEAT / IDENTIFY / RESUME, zombie-connection
// detection through heartbeat ACKs and automatic reconnection with
// exponential back-off. Dispatch events are forwarded as (name, data) pairs.
class Gateway : public QObject {
    Q_OBJECT
public:
    enum class State { Disconnected, Connecting, Identifying, Resuming, Connected };
    Q_ENUM(State)

    enum Opcode {
        Dispatch = 0,
        Heartbeat = 1,
        Identify = 2,
        PresenceUpdate = 3,
        VoiceStateUpdate = 4,
        Resume = 6,
        Reconnect = 7,
        InvalidSession = 9,
        Hello = 10,
        HeartbeatAck = 11,
    };

    static constexpr const char *kDefaultUrl = "wss://gateway.discord.gg";

    explicit Gateway(QObject *parent = nullptr);
    ~Gateway() override;

    void setToken(const QString &token, bool isBot);
    void open(const QUrl &baseUrl = QUrl(QString::fromLatin1(kDefaultUrl)));
    void close();

    State state() const { return m_state; }

    void sendOp(int op, const QJsonValue &d);

    // Joins (channelId non-empty) or leaves (channelId empty) a voice channel.
    void updateVoiceState(const QString &guildId, const QString &channelId, bool selfMute, bool selfDeaf);

signals:
    void dispatch(const QString &event, const QJsonObject &data);
    void stateChanged(lightcord::Gateway::State state);
    void authenticationFailed(const QString &reason);
    void fatalError(const QString &reason);

private:
    void onConnected();
    void onDisconnected();
    void onTextMessage(const QString &message);
    void sendHeartbeat();
    void identify();
    void resume();
    void scheduleReconnect(bool canResume);
    void setState(State s);
    QUrl withQuery(const QUrl &base) const;

    QWebSocket m_socket;
    QTimer m_heartbeatTimer;
    QTimer m_reconnectTimer;

    QString m_token;
    bool m_isBot = false;

    QUrl m_baseUrl;
    QUrl m_resumeUrl;
    QString m_sessionId;
    QJsonValue m_lastSequence = QJsonValue::Null;

    bool m_heartbeatAcked = true;
    bool m_wantOpen = false;
    bool m_resumeNext = false;
    int m_reconnectAttempts = 0;
    State m_state = State::Disconnected;
};

} // namespace lightcord
