// SPDX-License-Identifier: MIT
#include "Gateway.h"

#include <QJsonDocument>
#include <QRandomGenerator>
#include <QSysInfo>
#include <QUrlQuery>

namespace lightcord {

namespace {
// GUILDS | GUILD_VOICE_STATES | GUILD_MESSAGES | DIRECT_MESSAGES | MESSAGE_CONTENT
constexpr int kBotIntents = (1 << 0) | (1 << 7) | (1 << 9) | (1 << 12) | (1 << 15);

QString osName()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("Windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("Mac OS X");
#else
    return QStringLiteral("Linux");
#endif
}

bool isFatalCloseCode(int code)
{
    // 4004 auth failed, 4010 invalid shard, 4011 sharding required,
    // 4012 invalid API version, 4013 invalid intents, 4014 disallowed intents
    return code == 4004 || (code >= 4010 && code <= 4014);
}
} // namespace

Gateway::Gateway(QObject *parent)
    : QObject(parent)
{
    m_heartbeatTimer.setTimerType(Qt::PreciseTimer);
    m_reconnectTimer.setSingleShot(true);

    connect(&m_socket, &QWebSocket::connected, this, &Gateway::onConnected);
    connect(&m_socket, &QWebSocket::disconnected, this, &Gateway::onDisconnected);
    connect(&m_socket, &QWebSocket::textMessageReceived, this, &Gateway::onTextMessage);
    connect(&m_heartbeatTimer, &QTimer::timeout, this, &Gateway::sendHeartbeat);
    connect(&m_reconnectTimer, &QTimer::timeout, this, [this]() {
        if (!m_wantOpen)
            return;
        const QUrl target = (m_resumeNext && m_resumeUrl.isValid()) ? m_resumeUrl : m_baseUrl;
        setState(State::Connecting);
        m_socket.open(withQuery(target));
    });
}

Gateway::~Gateway()
{
    m_wantOpen = false;
    m_socket.abort();
}

void Gateway::setToken(const QString &token, bool isBot)
{
    m_token = token;
    m_isBot = isBot;
}

QUrl Gateway::withQuery(const QUrl &base) const
{
    QUrl url(base);
    QUrlQuery q;
    q.addQueryItem("v", "10");
    q.addQueryItem("encoding", "json");
    url.setQuery(q);
    return url;
}

void Gateway::open(const QUrl &baseUrl)
{
    m_baseUrl = baseUrl;
    m_wantOpen = true;
    m_resumeNext = false;
    m_sessionId.clear();
    m_lastSequence = QJsonValue::Null;
    m_reconnectAttempts = 0;
    setState(State::Connecting);
    m_socket.open(withQuery(m_baseUrl));
}

void Gateway::close()
{
    m_wantOpen = false;
    m_heartbeatTimer.stop();
    m_reconnectTimer.stop();
    m_socket.close(QWebSocketProtocol::CloseCodeNormal);
    setState(State::Disconnected);
}

void Gateway::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged(s);
}

void Gateway::sendOp(int op, const QJsonValue &d)
{
    if (m_socket.state() != QAbstractSocket::ConnectedState)
        return;
    QJsonObject payload{{"op", op}, {"d", d}};
    m_socket.sendTextMessage(QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
}

void Gateway::updateVoiceState(const QString &guildId, const QString &channelId, bool selfMute, bool selfDeaf)
{
    QJsonObject d{
        {"guild_id", guildId.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(guildId)},
        {"channel_id", channelId.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(channelId)},
        {"self_mute", selfMute},
        {"self_deaf", selfDeaf},
    };
    sendOp(VoiceStateUpdate, d);
}

void Gateway::onConnected()
{
    // Nothing to do yet: we wait for HELLO before identifying or resuming.
}

void Gateway::onDisconnected()
{
    m_heartbeatTimer.stop();
    const int code = static_cast<int>(m_socket.closeCode());

    if (!m_wantOpen) {
        setState(State::Disconnected);
        return;
    }

    if (code == 4004) {
        m_wantOpen = false;
        setState(State::Disconnected);
        emit authenticationFailed(tr("The token was rejected by Discord (authentication failed)."));
        return;
    }
    if (isFatalCloseCode(code)) {
        m_wantOpen = false;
        setState(State::Disconnected);
        QString reason = m_socket.closeReason();
        if (code == 4014)
            reason = tr("Disallowed intents. Enable the \"Message Content\" privileged intent for your bot "
                        "in the Discord Developer Portal.");
        emit fatalError(tr("Gateway closed (%1): %2").arg(code).arg(reason));
        return;
    }

    // 4007 invalid seq and 4009 session timed out require a fresh IDENTIFY.
    const bool canResume = !m_sessionId.isEmpty() && code != 4007 && code != 4009;
    scheduleReconnect(canResume);
}

void Gateway::scheduleReconnect(bool canResume)
{
    m_resumeNext = canResume;
    if (!canResume) {
        m_sessionId.clear();
        m_lastSequence = QJsonValue::Null;
    }
    const int attempt = qMin(m_reconnectAttempts++, 6);
    const int delay = (1000 << attempt) + static_cast<int>(QRandomGenerator::global()->bounded(1000));
    setState(State::Connecting);
    m_reconnectTimer.start(qMin(delay, 60000));
}

void Gateway::onTextMessage(const QString &message)
{
    const QJsonObject payload = QJsonDocument::fromJson(message.toUtf8()).object();
    const int op = payload.value("op").toInt(-1);
    const QJsonValue d = payload.value("d");

    if (payload.contains("s") && !payload.value("s").isNull())
        m_lastSequence = payload.value("s");

    switch (op) {
    case Hello: {
        const int interval = d.toObject().value("heartbeat_interval").toInt(41250);
        m_heartbeatAcked = true;
        m_heartbeatTimer.start(interval);
        // First heartbeat after interval * jitter, as recommended by Discord.
        const int jitter = static_cast<int>(interval * QRandomGenerator::global()->generateDouble());
        QTimer::singleShot(jitter, this, [this]() {
            if (m_heartbeatTimer.isActive())
                sendHeartbeat();
        });
        if (m_resumeNext && !m_sessionId.isEmpty())
            resume();
        else
            identify();
        break;
    }
    case HeartbeatAck:
        m_heartbeatAcked = true;
        break;
    case Heartbeat:
        sendHeartbeat();
        break;
    case Reconnect:
        m_socket.close(QWebSocketProtocol::CloseCode(4000));
        break;
    case InvalidSession: {
        const bool resumable = d.toBool();
        if (!resumable) {
            m_sessionId.clear();
            m_lastSequence = QJsonValue::Null;
        }
        m_resumeNext = resumable;
        QTimer::singleShot(1000 + static_cast<int>(QRandomGenerator::global()->bounded(4000)), this, [this]() {
            if (m_resumeNext && !m_sessionId.isEmpty())
                resume();
            else
                identify();
        });
        break;
    }
    case Dispatch: {
        const QString type = payload.value("t").toString();
        const QJsonObject data = d.toObject();
        if (type == QLatin1String("READY")) {
            m_sessionId = data.value("session_id").toString();
            const QString resumeUrl = data.value("resume_gateway_url").toString();
            if (!resumeUrl.isEmpty())
                m_resumeUrl = QUrl(resumeUrl);
            m_reconnectAttempts = 0;
            setState(State::Connected);
        } else if (type == QLatin1String("RESUMED")) {
            m_reconnectAttempts = 0;
            setState(State::Connected);
        }
        emit dispatch(type, data);
        break;
    }
    default:
        break;
    }
}

void Gateway::sendHeartbeat()
{
    if (!m_heartbeatAcked) {
        // Zombied connection: no ACK since the last heartbeat. Reconnect and resume.
        m_heartbeatTimer.stop();
        m_socket.close(QWebSocketProtocol::CloseCode(4000));
        return;
    }
    m_heartbeatAcked = false;
    sendOp(Heartbeat, m_lastSequence);
}

void Gateway::identify()
{
    setState(State::Identifying);
    QJsonObject props{
        {"os", osName()},
        {"browser", "Lightcord"},
        {"device", "Lightcord"},
    };
    QJsonObject d{
        {"token", m_token},
        {"properties", props},
        {"compress", false},
        {"large_threshold", 100},
    };
    if (m_isBot)
        d.insert("intents", kBotIntents);
    sendOp(Identify, d);
}

void Gateway::resume()
{
    setState(State::Resuming);
    QJsonObject d{
        {"token", m_token},
        {"session_id", m_sessionId},
        {"seq", m_lastSequence},
    };
    sendOp(Resume, d);
}

} // namespace lightcord
