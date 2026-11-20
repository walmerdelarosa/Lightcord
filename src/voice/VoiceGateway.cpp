// SPDX-License-Identifier: MIT
#include "VoiceGateway.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>

namespace lightcord {

namespace {
constexpr int kMaxReconnects = 5;

bool isFatal(int code)
{
    switch (code) {
    case 4004: // authentication failed
    case 4006: // session no longer valid
    case 4011: // server not found
    case 4012: // unknown protocol
    case 4014: // disconnected (kicked, moved, main session dropped)
    case 4016: // unknown encryption mode
    case 4017: // DAVE required
    case 4020: // bad request
    case 4021: // rate limited
    case 4022: // call terminated
        return true;
    default:
        return false;
    }
}

QString describe(int code)
{
    switch (code) {
    case 4014: return QObject::tr("Disconnected from the voice channel.");
    case 4017: return QObject::tr("This channel requires end-to-end encryption (DAVE).");
    case 4021: return QObject::tr("Voice connection rate limited by Discord.");
    case 4022: return QObject::tr("The call ended.");
    case 4004: return QObject::tr("Voice authentication failed.");
    default: return QObject::tr("Voice connection closed (code %1).").arg(code);
    }
}
} // namespace

VoiceGateway::VoiceGateway(QObject *parent)
    : QObject(parent)
{
    connect(&m_socket, &QWebSocket::connected, this, &VoiceGateway::onConnected);
    connect(&m_socket, &QWebSocket::disconnected, this, &VoiceGateway::onDisconnected);
    connect(&m_socket, &QWebSocket::textMessageReceived, this, &VoiceGateway::onText);
    connect(&m_socket, &QWebSocket::binaryMessageReceived, this, &VoiceGateway::onBinary);
    connect(&m_heartbeat, &QTimer::timeout, this, &VoiceGateway::sendHeartbeat);
}

VoiceGateway::~VoiceGateway()
{
    m_wantOpen = false;
    m_socket.abort();
}

QUrl VoiceGateway::url() const
{
    QString host = m_params.endpoint;
    if (host.startsWith(QLatin1String("wss://")))
        host = host.mid(6);
    if (host.endsWith(QLatin1String(":80")))
        host.chop(3);
    return QUrl(QStringLiteral("wss://%1/?v=8").arg(host));
}

void VoiceGateway::open(const Params &params)
{
    m_params = params;
    m_wantOpen = true;
    m_canResume = false;
    m_resuming = false;
    m_seqAck = -1;
    m_reconnects = 0;
    m_socket.open(url());
}

void VoiceGateway::close()
{
    m_wantOpen = false;
    m_heartbeat.stop();
    m_socket.close();
}

void VoiceGateway::sendJson(int op, const QJsonValue &d)
{
    if (m_socket.state() != QAbstractSocket::ConnectedState)
        return;
    const QJsonObject payload{{"op", op}, {"d", d}};
    m_socket.sendTextMessage(QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
}

void VoiceGateway::sendBinary(int op, const QByteArray &payload)
{
    if (m_socket.state() != QAbstractSocket::ConnectedState)
        return;
    QByteArray frame;
    frame.reserve(payload.size() + 1);
    frame.append(static_cast<char>(op));
    frame.append(payload);
    m_socket.sendBinaryMessage(frame);
}

void VoiceGateway::selectProtocol(const QString &address, quint16 port, const QString &mode)
{
    const QJsonObject data{{"address", address}, {"port", port}, {"mode", mode}};
    sendJson(SelectProtocol, QJsonObject{{"protocol", "udp"}, {"data", data}});
}

void VoiceGateway::sendSpeaking(int flags, quint32 ssrc)
{
    sendJson(Speaking, QJsonObject{{"speaking", flags}, {"delay", 0}, {"ssrc", static_cast<double>(ssrc)}});
}

void VoiceGateway::onConnected()
{
    m_acked = true;
    if (m_resuming) {
        sendJson(Resume, QJsonObject{{"server_id", m_params.serverId},
                                     {"session_id", m_params.sessionId},
                                     {"token", m_params.token},
                                     {"seq_ack", m_seqAck}});
    } else {
        sendJson(Identify, QJsonObject{{"server_id", m_params.serverId},
                                       {"user_id", m_params.userId},
                                       {"session_id", m_params.sessionId},
                                       {"token", m_params.token},
                                       {"max_dave_protocol_version", m_params.maxDaveVersion}});
    }
}

void VoiceGateway::onDisconnected()
{
    m_heartbeat.stop();
    if (!m_wantOpen)
        return;
    const int code = static_cast<int>(m_socket.closeCode());
    if (isFatal(code) || !m_canResume || m_reconnects >= kMaxReconnects) {
        m_wantOpen = false;
        emit fatalError(describe(code));
        return;
    }
    ++m_reconnects;
    m_resuming = true;
    emit reconnecting();
    QTimer::singleShot(500 * m_reconnects, this, [this]() {
        if (m_wantOpen)
            m_socket.open(url());
    });
}

void VoiceGateway::sendHeartbeat()
{
    if (!m_acked) {
        // No ACK since the last heartbeat: the connection is dead, resume it.
        m_socket.abort();
        return;
    }
    m_acked = false;
    sendJson(Heartbeat, QJsonObject{{"t", QDateTime::currentMSecsSinceEpoch()}, {"seq_ack", m_seqAck}});
}

void VoiceGateway::onText(const QString &message)
{
    const QJsonObject payload = QJsonDocument::fromJson(message.toUtf8()).object();
    const int op = payload.value("op").toInt(-1);
    const QJsonObject d = payload.value("d").toObject();
    if (payload.contains("seq"))
        m_seqAck = payload.value("seq").toInt(m_seqAck);

    switch (op) {
    case Hello: {
        const double interval = d.value("heartbeat_interval").toDouble(13750);
        m_heartbeat.start(static_cast<int>(interval));
        break;
    }
    case HeartbeatAck:
        m_acked = true;
        break;
    case Ready: {
        QStringList modes;
        for (const QJsonValue &m : d.value("modes").toArray())
            modes << m.toString();
        m_canResume = true;
        m_reconnects = 0;
        emit ready(static_cast<quint32>(d.value("ssrc").toDouble()), d.value("ip").toString(),
                   static_cast<quint16>(d.value("port").toInt()), modes);
        break;
    }
    case SessionDescription: {
        QByteArray key;
        for (const QJsonValue &b : d.value("secret_key").toArray())
            key.append(static_cast<char>(b.toInt()));
        emit sessionDescription(d.value("mode").toString(), key, d.value("dave_protocol_version").toInt(0));
        break;
    }
    case Speaking:
        emit speaking(d.value("user_id").toString(), static_cast<quint32>(d.value("ssrc").toDouble()),
                      d.value("speaking").toInt());
        break;
    case Resumed:
        m_resuming = false;
        m_reconnects = 0;
        emit resumed();
        break;
    case ClientsConnect: {
        QStringList ids;
        for (const QJsonValue &v : d.value("user_ids").toArray())
            ids << v.toString();
        emit clientsConnected(ids);
        break;
    }
    case ClientDisconnect:
        emit clientDisconnected(d.value("user_id").toString());
        break;
    case 21: // DAVE prepare transition
    case 22: // DAVE execute transition
    case 24: // DAVE prepare epoch
        emit daveJson(op, d);
        break;
    default:
        break;
    }
}

void VoiceGateway::onBinary(const QByteArray &message)
{
    // Server binary frames: [sequence:u16 BE][opcode:u8][payload]
    if (message.size() < 3)
        return;
    const auto *p = reinterpret_cast<const uchar *>(message.constData());
    m_seqAck = (p[0] << 8) | p[1];
    const int op = p[2];
    emit daveBinary(op, message.mid(3));
}

} // namespace lightcord
