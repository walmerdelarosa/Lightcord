// SPDX-License-Identifier: MIT
#include "Session.h"

#include <QJsonArray>
#include <QStringList>
#include <QUrl>

#include <algorithm>

namespace lightcord {

namespace {
const QVector<Message> kNoMessages;
}

Session::Session(QObject *parent)
    : QObject(parent)
{
    connect(&m_gateway, &Gateway::dispatch, this, &Session::onDispatch);
    connect(&m_gateway, &Gateway::authenticationFailed, this, [this](const QString &reason) {
        m_running = false;
        emit authenticationFailed(reason);
    });
    connect(&m_gateway, &Gateway::fatalError, this, &Session::errorOccurred);
    connect(&m_gateway, &Gateway::stateChanged, this, [this](Gateway::State s) {
        QString text;
        switch (s) {
        case Gateway::State::Disconnected: text = tr("Disconnected"); break;
        case Gateway::State::Connecting: text = tr("Connecting…"); break;
        case Gateway::State::Identifying: text = tr("Logging in…"); break;
        case Gateway::State::Resuming: text = tr("Resuming session…"); break;
        case Gateway::State::Connected: text = tr("Connected"); break;
        }
        emit connectionStateChanged(text);
    });
}

void Session::start(const QString &token, bool isBot)
{
    stop();
    m_running = true;
    m_isBot = isBot;
    m_rest.setToken(token, isBot);
    m_gateway.setToken(token, isBot);

    // Ask the API for the gateway URL; fall back to the well-known one.
    m_rest.get(isBot ? QStringLiteral("/gateway/bot") : QStringLiteral("/gateway"), this,
               [this](const DiscordRest::Reply &r) {
                   if (!m_running)
                       return;
                   if (r.status == 401) {
                       m_running = false;
                       emit authenticationFailed(tr("The token was rejected by Discord (401 Unauthorized)."));
                       return;
                   }
                   QUrl url(r.json.object().value("url").toString());
                   if (!url.isValid() || url.scheme() != QLatin1String("wss"))
                       url = QUrl(QString::fromLatin1(Gateway::kDefaultUrl));
                   m_gateway.open(url);
               });
}

void Session::stop()
{
    m_running = false;
    m_gateway.close();
    m_rest.clearToken();
    m_me = User{};
    m_guilds.clear();
    m_privateChannels.clear();
    m_users.clear();
    m_messages.clear();
    m_loadedChannels.clear();
    m_voiceChannelOfUser.clear();
}

const Guild *Session::guild(const Snowflake &id) const
{
    for (const Guild &g : m_guilds) {
        if (g.id == id)
            return &g;
    }
    return nullptr;
}

Guild *Session::mutableGuild(const Snowflake &id)
{
    for (Guild &g : m_guilds) {
        if (g.id == id)
            return &g;
    }
    return nullptr;
}

const Channel *Session::channel(const Snowflake &id) const
{
    for (const Channel &c : m_privateChannels) {
        if (c.id == id)
            return &c;
    }
    for (const Guild &g : m_guilds) {
        for (const Channel &c : g.channels) {
            if (c.id == id)
                return &c;
        }
    }
    return nullptr;
}

QString Session::userDisplayName(const Snowflake &userId) const
{
    if (userId == m_me.id)
        return m_me.displayName();
    const auto it = m_users.constFind(userId);
    return it != m_users.constEnd() ? it->displayName() : QString();
}

QString Session::channelDisplayName(const Channel &c) const
{
    if (!c.isPrivate())
        return c.name;
    if (!c.name.isEmpty())
        return c.name;
    QStringList names;
    for (const User &u : c.recipients)
        names << u.displayName();
    if (names.isEmpty()) {
        for (const Snowflake &id : c.recipientIds) {
            const QString n = userDisplayName(id);
            names << (n.isEmpty() ? tr("Unknown user") : n);
        }
    }
    return names.isEmpty() ? tr("Direct message") : names.join(QStringLiteral(", "));
}

bool Session::canView(const Channel &c) const
{
    if (c.isPrivate())
        return true;
    const Guild *g = guild(c.guildId);
    if (!g || !g->myRolesKnown)
        return true; // unknown: do not hide anything
    return computeChannelPermissions(*g, c, m_me.id) & Permissions::ViewChannel;
}

bool Session::canSend(const Channel &c) const
{
    if (c.isPrivate())
        return true;
    const Guild *g = guild(c.guildId);
    if (!g || !g->myRolesKnown)
        return true;
    return computeChannelPermissions(*g, c, m_me.id) & Permissions::SendMessages;
}

QStringList Session::voiceMembers(const Snowflake &channelId) const
{
    QStringList ids;
    for (auto it = m_voiceChannelOfUser.cbegin(); it != m_voiceChannelOfUser.cend(); ++it) {
        if (it.value() == channelId)
            ids << it.key();
    }
    std::sort(ids.begin(), ids.end(), [this](const QString &a, const QString &b) {
        return userDisplayName(a).compare(userDisplayName(b), Qt::CaseInsensitive) < 0;
    });
    return ids;
}

void Session::applyVoiceState(const QJsonObject &state, const Snowflake &guildId)
{
    Q_UNUSED(guildId);
    const QJsonObject member = state.value("member").toObject();
    if (!member.isEmpty())
        rememberUser(User::fromJson(member.value("user").toObject()));
    const Snowflake userId = state.value("user_id").toString();
    if (userId.isEmpty())
        return;
    const QJsonValue channel = state.value("channel_id");
    if (channel.isNull() || channel.toString().isEmpty())
        m_voiceChannelOfUser.remove(userId);
    else
        m_voiceChannelOfUser.insert(userId, channel.toString());
}

const QVector<Message> &Session::messages(const Snowflake &channelId) const
{
    const auto it = m_messages.constFind(channelId);
    return it != m_messages.constEnd() ? *it : kNoMessages;
}

bool Session::hasLoadedMessages(const Snowflake &channelId) const
{
    return m_loadedChannels.value(channelId, false);
}

void Session::loadMessages(const Snowflake &channelId)
{
    m_rest.get(QStringLiteral("/channels/%1/messages?limit=50").arg(channelId), this,
               [this, channelId](const DiscordRest::Reply &r) {
                   if (!r.ok) {
                       emit errorOccurred(tr("Could not load messages: %1").arg(r.error));
                       return;
                   }
                   QVector<Message> list;
                   const QJsonArray arr = r.json.array();
                   list.reserve(arr.size());
                   for (const QJsonValue &v : arr) {
                       const Message m = Message::fromJson(v.toObject());
                       rememberUser(m.author);
                       list.push_back(m);
                   }
                   std::reverse(list.begin(), list.end()); // API returns newest first

                   // Keep messages that arrived through the gateway while loading.
                   QVector<Message> &store = m_messages[channelId];
                   for (const Message &live : store) {
                       const bool known = std::any_of(list.cbegin(), list.cend(),
                                                      [&](const Message &m) { return m.id == live.id; });
                       if (!known)
                           list.push_back(live);
                   }
                   store = list;
                   m_loadedChannels[channelId] = true;
                   emit messagesLoaded(channelId);
               });
}

void Session::sendMessage(const Snowflake &channelId, const QString &content)
{
    const QString text = content.trimmed();
    if (text.isEmpty())
        return;
    QJsonObject body{{"content", text.left(2000)}};
    m_rest.post(QStringLiteral("/channels/%1/messages").arg(channelId), body, this,
                [this](const DiscordRest::Reply &r) {
                    if (!r.ok)
                        emit errorOccurred(tr("Message not sent: %1").arg(r.error));
                    // On success the message comes back through MESSAGE_CREATE.
                });
}

void Session::rememberUser(const User &u)
{
    if (!u.id.isEmpty())
        m_users.insert(u.id, u);
}

void Session::upsertGuild(const Guild &incoming)
{
    Guild g = incoming;
    if (Guild *existing = mutableGuild(g.id)) {
        if (g.channels.isEmpty())
            g.channels = existing->channels;
        if (g.roles.isEmpty())
            g.roles = existing->roles;
        if (g.name.isEmpty())
            g.name = existing->name;
        if (!g.myRolesKnown && existing->myRolesKnown) {
            g.myRoles = existing->myRoles;
            g.myRolesKnown = true;
        }
        *existing = g;
    } else {
        m_guilds.push_back(g);
    }
}

void Session::upsertChannel(const Channel &c)
{
    if (c.isPrivate()) {
        for (Channel &existing : m_privateChannels) {
            if (existing.id == c.id) {
                existing = c;
                return;
            }
        }
        m_privateChannels.push_front(c);
        return;
    }
    if (Guild *g = mutableGuild(c.guildId)) {
        for (Channel &existing : g->channels) {
            if (existing.id == c.id) {
                existing = c;
                return;
            }
        }
        g->channels.push_back(c);
    }
}

void Session::removeChannel(const Snowflake &channelId, const Snowflake &guildId)
{
    auto pred = [&](const Channel &c) { return c.id == channelId; };
    if (Guild *g = mutableGuild(guildId))
        g->channels.erase(std::remove_if(g->channels.begin(), g->channels.end(), pred), g->channels.end());
    m_privateChannels.erase(std::remove_if(m_privateChannels.begin(), m_privateChannels.end(), pred),
                            m_privateChannels.end());
}

void Session::handleReady(const QJsonObject &data)
{
    m_me = User::fromJson(data.value("user").toObject());
    rememberUser(m_me);

    for (const QJsonValue &v : data.value("users").toArray())
        rememberUser(User::fromJson(v.toObject()));

    const QJsonArray guilds = data.value("guilds").toArray();
    // User accounts receive their own member object per guild in "merged_members",
    // in the same order as "guilds".
    const QJsonArray merged = data.value("merged_members").toArray();

    m_guilds.clear();
    for (int i = 0; i < guilds.size(); ++i) {
        const QJsonObject go = guilds.at(i).toObject();
        Guild g = Guild::fromJson(go);

        QJsonArray members = go.value("members").toArray();
        if (i < merged.size())
            members = merged.at(i).toArray();
        for (const QJsonValue &mv : members) {
            const QJsonObject mo = mv.toObject();
            const QString uid = mo.contains("user_id") ? mo.value("user_id").toString()
                                                       : mo.value("user").toObject().value("id").toString();
            if (uid == m_me.id) {
                for (const QJsonValue &r : mo.value("roles").toArray())
                    g.myRoles.push_back(r.toString());
                g.myRolesKnown = true;
            }
        }
        for (const QJsonValue &vs : go.value("voice_states").toArray())
            applyVoiceState(vs.toObject(), g.id);
        m_guilds.push_back(g);
    }

    m_privateChannels.clear();
    for (const QJsonValue &v : data.value("private_channels").toArray())
        m_privateChannels.push_back(Channel::fromJson(v.toObject()));
    std::sort(m_privateChannels.begin(), m_privateChannels.end(), [](const Channel &a, const Channel &b) {
        // Most recent activity first (snowflakes grow over time).
        if (a.lastMessageId.size() != b.lastMessageId.size())
            return a.lastMessageId.size() > b.lastMessageId.size();
        return a.lastMessageId > b.lastMessageId;
    });

    emit ready();
    emit guildsChanged();
}

void Session::onDispatch(const QString &event, const QJsonObject &data)
{
    if (event == QLatin1String("READY")) {
        handleReady(data);
    } else if (event == QLatin1String("GUILD_CREATE") || event == QLatin1String("GUILD_UPDATE")) {
        Guild g = Guild::fromJson(data);
        for (const QJsonValue &mv : data.value("members").toArray()) {
            const QJsonObject mo = mv.toObject();
            if (mo.value("user").toObject().value("id").toString() == m_me.id) {
                for (const QJsonValue &r : mo.value("roles").toArray())
                    g.myRoles.push_back(r.toString());
                g.myRolesKnown = true;
            }
        }
        for (const QJsonValue &vs : data.value("voice_states").toArray())
            applyVoiceState(vs.toObject(), g.id);
        upsertGuild(g);
        emit guildsChanged();
    } else if (event == QLatin1String("GUILD_DELETE")) {
        const Snowflake id = data.value("id").toString();
        m_guilds.erase(std::remove_if(m_guilds.begin(), m_guilds.end(), [&](const Guild &g) { return g.id == id; }),
                       m_guilds.end());
        emit guildsChanged();
    } else if (event == QLatin1String("CHANNEL_CREATE") || event == QLatin1String("CHANNEL_UPDATE")) {
        upsertChannel(Channel::fromJson(data));
        emit guildsChanged();
    } else if (event == QLatin1String("CHANNEL_DELETE")) {
        removeChannel(data.value("id").toString(), data.value("guild_id").toString());
        emit guildsChanged();
    } else if (event == QLatin1String("MESSAGE_CREATE")) {
        const Message m = Message::fromJson(data);
        rememberUser(m.author);
        QVector<Message> &list = m_messages[m.channelId];
        list.push_back(m);
        if (list.size() > kMessagesPerChannel)
            list.erase(list.begin(), list.begin() + (list.size() - kMessagesPerChannel));
        for (Channel &c : m_privateChannels) {
            if (c.id == m.channelId)
                c.lastMessageId = m.id;
        }
        emit messageCreated(m);
    } else if (event == QLatin1String("MESSAGE_UPDATE")) {
        const Snowflake channelId = data.value("channel_id").toString();
        const Snowflake id = data.value("id").toString();
        QVector<Message> &list = m_messages[channelId];
        for (Message &m : list) {
            if (m.id == id) {
                // Partial updates (e.g. embeds only) may lack content.
                if (data.contains("content"))
                    m.content = data.value("content").toString();
                if (data.contains("edited_timestamp"))
                    m.editedTimestamp = QDateTime::fromString(data.value("edited_timestamp").toString(),
                                                              Qt::ISODateWithMs);
                if (data.contains("embeds"))
                    m.embedCount = data.value("embeds").toArray().size();
                emit messageUpdated(m);
                break;
            }
        }
    } else if (event == QLatin1String("MESSAGE_DELETE")) {
        const Snowflake channelId = data.value("channel_id").toString();
        const Snowflake id = data.value("id").toString();
        QVector<Message> &list = m_messages[channelId];
        list.erase(std::remove_if(list.begin(), list.end(), [&](const Message &m) { return m.id == id; }),
                   list.end());
        emit messageDeleted(channelId, id);
    } else if (event == QLatin1String("VOICE_STATE_UPDATE")) {
        applyVoiceState(data, data.value("guild_id").toString());
        emit voiceStateUpdate(data);
        emit voiceMembersChanged();
    } else if (event == QLatin1String("VOICE_SERVER_UPDATE")) {
        emit voiceServerUpdate(data);
    }
}

} // namespace lightcord
