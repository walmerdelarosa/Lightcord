// SPDX-License-Identifier: MIT
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QWebSocket>

namespace lightcord {

// Discord voice gateway, version 8.
//
// Handles IDENTIFY/RESUME, heartbeats with `seq_ack` (buffered resume),
// the JSON opcodes needed for audio, and the DAVE opcodes, including the
// binary frame format [seq:u16][opcode:u8][payload] used by opcodes 25-30.
class VoiceGateway : public QObject {
    Q_OBJECT
public:
    enum Op {
        Identify = 0,
        SelectProtocol = 1,
        Ready = 2,
        Heartbeat = 3,
        SessionDescription = 4,
        Speaking = 5,
        HeartbeatAck = 6,
        Resume = 7,
        Hello = 8,
        Resumed = 9,
        ClientsConnect = 11,
        ClientDisconnect = 13,
    };

    struct Params {
        QString endpoint;
        QString serverId;
        QString userId;
        QString sessionId;
        QString token;
        int maxDaveVersion = 0;
    };

    explicit VoiceGateway(QObject *parent = nullptr);
    ~VoiceGateway() override;

    void open(const Params &params);
    void close();

    void sendJson(int op, const QJsonValue &d);
    void sendBinary(int op, const QByteArray &payload);
    void selectProtocol(const QString &address, quint16 port, const QString &mode);
    void sendSpeaking(int flags, quint32 ssrc);

signals:
    void ready(quint32 ssrc, const QString &ip, quint16 port, const QStringList &modes);
    void sessionDescription(const QString &mode, const QByteArray &secretKey, int daveProtocolVersion);
    void speaking(const QString &userId, quint32 ssrc, int flags);
    void clientsConnected(const QStringList &userIds);
    void clientDisconnected(const QString &userId);
    void daveJson(int op, const QJsonObject &d);
    void daveBinary(int op, const QByteArray &payload);
    void resumed();
    void reconnecting();
    void fatalError(const QString &reason);

private:
    void onConnected();
    void onDisconnected();
    void onText(const QString &message);
    void onBinary(const QByteArray &message);
    void sendHeartbeat();
    QUrl url() const;

    QWebSocket m_socket;
    QTimer m_heartbeat;
    Params m_params;
    int m_seqAck = -1;
    bool m_wantOpen = false;
    bool m_canResume = false;
    bool m_resuming = false;
    bool m_acked = true;
    int m_reconnects = 0;
};

} // namespace lightcord
