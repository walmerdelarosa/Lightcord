// SPDX-License-Identifier: MIT
#include "VoiceSession.h"

#include "core/Gateway.h"

namespace lightcord {

VoiceSession::VoiceSession(Gateway *gateway, QObject *parent)
    : QObject(parent)
    , m_gateway(gateway)
{
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
    const QString guildId = m_info.guildId;
    setState(State::Idle);
    m_gateway->updateVoiceState(guildId, QString(), m_selfMute, m_selfDeaf);
    m_info = TransportInfo{};
}

void VoiceSession::setSelfMute(bool mute)
{
    m_selfMute = mute;
    if (m_state != State::Idle)
        sendState();
}

void VoiceSession::setSelfDeaf(bool deaf)
{
    m_selfDeaf = deaf;
    if (m_state != State::Idle)
        sendState();
}

void VoiceSession::handleVoiceStateUpdate(const QJsonObject &data)
{
    if (m_state == State::Idle || data.value("user_id").toString() != m_info.userId)
        return;
    if (data.value("channel_id").isNull()) {
        // We were disconnected (kicked, channel deleted, ...).
        setState(State::Idle);
        m_info = TransportInfo{};
        return;
    }
    m_info.sessionId = data.value("session_id").toString();
    maybeReady();
}

void VoiceSession::handleVoiceServerUpdate(const QJsonObject &data)
{
    if (m_state == State::Idle || data.value("guild_id").toString() != m_info.guildId)
        return;
    m_info.endpoint = data.value("endpoint").toString();
    m_info.token = data.value("token").toString();
    maybeReady();
}

void VoiceSession::maybeReady()
{
    if (m_info.sessionId.isEmpty() || m_info.endpoint.isEmpty() || m_info.token.isEmpty())
        return;
    setState(State::ServerAssigned);
    emit transportInfoReady(m_info);
}

} // namespace lightcord
