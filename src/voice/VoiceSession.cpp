// SPDX-License-Identifier: MIT
#include "VoiceSession.h"

#include "core/Gateway.h"
#include "core/Settings.h"

#ifdef LIGHTCORD_HAS_VOICE
#include "VoiceEngine.h"
#endif

namespace lightcord {

VoiceSession::VoiceSession(Gateway *gateway, QObject *parent)
    : QObject(parent)
    , m_gateway(gateway)
{
#ifdef LIGHTCORD_HAS_VOICE
    m_thread.setObjectName(QStringLiteral("LightcordVoice"));
    m_engine = new VoiceEngine;
    m_engine->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_engine, &QObject::deleteLater);

    connect(m_engine, &VoiceEngine::statusChanged, this, &VoiceSession::statusText);
    connect(m_engine, &VoiceEngine::encryptionChanged, this, &VoiceSession::encryptionChanged);
    connect(m_engine, &VoiceEngine::speakingChanged, this, &VoiceSession::speakingChanged);
    connect(m_engine, &VoiceEngine::connectedChanged, this, [this](bool connected) {
        if (connected)
            setState(State::Connected);
        else if (m_state == State::Connected)
            setState(State::Connecting);
    });
    connect(m_engine, &VoiceEngine::failed, this, [this](const QString &reason) {
        emit errorOccurred(reason);
        leave();
    });
    // Above normal priority, so that a busy UI or other programs do not delay
    // the 10 ms playback ticks and the 20 ms microphone frames.
    m_thread.start(QThread::HighestPriority);

    connect(&Settings::instance(), &Settings::noiseSuppressionChanged, this, &VoiceSession::reloadAudioSettings);
    connect(&Settings::instance(), &Settings::audioDevicesChanged, this, &VoiceSession::reloadAudioSettings);
#endif
}

VoiceSession::~VoiceSession()
{
#ifdef LIGHTCORD_HAS_VOICE
    if (m_engine)
        QMetaObject::invokeMethod(m_engine, [e = m_engine]() { e->stop(); }, Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
#endif
}

bool VoiceSession::isMediaAvailable()
{
#ifdef LIGHTCORD_HAS_VOICE
    return true;
#else
    return false;
#endif
}

void VoiceSession::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged(s);
}

void VoiceSession::sendState()
{
    m_gateway->updateVoiceState(m_info.guildId, m_state == State::Idle ? QString() : m_info.channelId, m_selfMute,
                                m_selfDeaf);
}

void VoiceSession::join(const QString &guildId, const QString &channelId, const QString &myUserId)
{
    stopMedia();
    m_info = TransportInfo{};
    m_info.guildId = guildId;
    m_info.channelId = channelId;
    m_info.userId = myUserId;
    setState(State::Requested);
    sendState();
}

void VoiceSession::leave()
{
    if (m_state == State::Idle)
        return;
    stopMedia();
    const QString guildId = m_info.guildId;
    setState(State::Idle);
    m_gateway->updateVoiceState(guildId, QString(), m_selfMute, m_selfDeaf);
    m_info = TransportInfo{};
    emit statusText(QString());
}

void VoiceSession::setSelfMute(bool mute)
{
    m_selfMute = mute;
    if (m_state != State::Idle)
        sendState();
#ifdef LIGHTCORD_HAS_VOICE
    QMetaObject::invokeMethod(m_engine, [e = m_engine, mute]() { e->setSelfMute(mute); }, Qt::QueuedConnection);
#endif
}

void VoiceSession::setSelfDeaf(bool deaf)
{
    m_selfDeaf = deaf;
    if (m_state != State::Idle)
        sendState();
#ifdef LIGHTCORD_HAS_VOICE
    QMetaObject::invokeMethod(m_engine, [e = m_engine, deaf]() { e->setSelfDeaf(deaf); }, Qt::QueuedConnection);
#endif
}

void VoiceSession::handleVoiceStateUpdate(const QJsonObject &data)
{
    if (m_state == State::Idle || data.value("user_id").toString() != m_info.userId)
        return;
    if (data.value("channel_id").isNull()) {
        // Disconnected by someone else (kicked, channel deleted, ...).
        stopMedia();
        setState(State::Idle);
        m_info = TransportInfo{};
        emit statusText(QString());
        return;
    }
    const QString channel = data.value("channel_id").toString();
    if (!channel.isEmpty() && channel != m_info.channelId) {
        // Moved to another channel: the voice server sends new credentials.
        m_info.channelId = channel;
        stopMedia();
    }
    m_info.sessionId = data.value("session_id").toString();
    maybeStartMedia();
}

void VoiceSession::handleVoiceServerUpdate(const QJsonObject &data)
{
    if (m_state == State::Idle || data.value("guild_id").toString() != m_info.guildId)
        return;
    if (data.value("endpoint").isNull()) {
        // The voice server is being reallocated; a new update will follow.
        stopMedia();
        return;
    }
    m_info.endpoint = data.value("endpoint").toString();
    m_info.token = data.value("token").toString();
    maybeStartMedia();
}

void VoiceSession::maybeStartMedia()
{
    if (m_info.sessionId.isEmpty() || m_info.endpoint.isEmpty() || m_info.token.isEmpty())
        return;
    if (m_info.endpoint == m_startedEndpoint && m_info.token == m_startedToken)
        return; // already running with these credentials
    m_startedEndpoint = m_info.endpoint;
    m_startedToken = m_info.token;

#ifdef LIGHTCORD_HAS_VOICE
    setState(State::Connecting);
    const TransportInfo info = m_info;
    const bool mute = m_selfMute;
    const bool deaf = m_selfDeaf;
    QMetaObject::invokeMethod(
        m_engine,
        [e = m_engine, info, mute, deaf]() {
            e->setSelfMute(mute);
            e->setSelfDeaf(deaf);
            e->start(info.endpoint, info.guildId, info.channelId, info.userId, info.sessionId, info.token);
        },
        Qt::QueuedConnection);
#else
    emit statusText(tr("This build of Lightcord was compiled without voice support."));
#endif
}

void VoiceSession::stopMedia()
{
    m_startedEndpoint.clear();
    m_startedToken.clear();
#ifdef LIGHTCORD_HAS_VOICE
    QMetaObject::invokeMethod(m_engine, [e = m_engine]() { e->stop(); }, Qt::QueuedConnection);
#endif
}

void VoiceSession::reloadAudioSettings()
{
#ifdef LIGHTCORD_HAS_VOICE
    QMetaObject::invokeMethod(m_engine, [e = m_engine]() { e->reloadAudioSettings(); }, Qt::QueuedConnection);
#endif
}

} // namespace lightcord
