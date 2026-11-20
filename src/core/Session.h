// SPDX-License-Identifier: MIT
#pragma once

#include "DiscordRest.h"
#include "Gateway.h"
#include "Models.h"

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVector>

namespace lightcord {

// Owns the REST client and the gateway connection, keeps the in-memory state
// (user, guilds, channels, recent messages) and exposes it to the UI through
// signals. This is the only class the UI talks to for Discord data.
class Session : public QObject {
    Q_OBJECT
public:
    static constexpr int kMessagesPerChannel = 200;

    explicit Session(QObject *parent = nullptr);

    void start(const QString &token, bool isBot);
    void stop();
    bool isRunning() const { return m_running; }
    bool isBot() const { return m_isBot; }

    const User &me() const { return m_me; }
    const QVector<Guild> &guilds() const { return m_guilds; }
    const QVector<Channel> &privateChannels() const { return m_privateChannels; }

    const Guild *guild(const Snowflake &id) const;
    const Channel *channel(const Snowflake &id) const;
    QString channelDisplayName(const Channel &channel) const;
    QString userDisplayName(const Snowflake &userId) const;

    bool canView(const Channel &channel) const;
    bool canSend(const Channel &channel) const;

    // Users currently connected to a voice channel (from the guilds' voice states).
    QStringList voiceMembers(const Snowflake &channelId) const;

    const QVector<Message> &messages(const Snowflake &channelId) const;
    bool hasLoadedMessages(const Snowflake &channelId) const;
    void loadMessages(const Snowflake &channelId);
    void sendMessage(const Snowflake &channelId, const QString &content);

    Gateway *gateway() { return &m_gateway; }
    DiscordRest *rest() { return &m_rest; }

signals:
    void ready();
    void guildsChanged();
    void messagesLoaded(const lightcord::Snowflake &channelId);
    void messageCreated(const lightcord::Message &message);
    void messageUpdated(const lightcord::Message &message);
    void messageDeleted(const lightcord::Snowflake &channelId, const lightcord::Snowflake &messageId);
    void connectionStateChanged(const QString &description);
    void authenticationFailed(const QString &reason);
    void errorOccurred(const QString &message);
    void voiceStateUpdate(const QJsonObject &data);
    void voiceMembersChanged();
    void voiceServerUpdate(const QJsonObject &data);

private:
    void onDispatch(const QString &event, const QJsonObject &data);
    void handleReady(const QJsonObject &data);
    void upsertGuild(const Guild &g);
    void upsertChannel(const Channel &c);
    void removeChannel(const Snowflake &channelId, const Snowflake &guildId);
    Guild *mutableGuild(const Snowflake &id);
    void rememberUser(const User &u);
    void applyVoiceState(const QJsonObject &state, const Snowflake &guildId);

    DiscordRest m_rest;
    Gateway m_gateway;

    bool m_running = false;
    bool m_isBot = false;
    User m_me;
    QVector<Guild> m_guilds;
    QVector<Channel> m_privateChannels;
    QHash<Snowflake, User> m_users;
    QHash<Snowflake, QVector<Message>> m_messages;
    QHash<Snowflake, bool> m_loadedChannels;
    QHash<Snowflake, Snowflake> m_voiceChannelOfUser; // user id -> voice channel id
};

} // namespace lightcord
