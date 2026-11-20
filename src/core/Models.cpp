// SPDX-License-Identifier: MIT
#include "Models.h"

namespace lightcord {

namespace {
quint64 parsePermissionString(const QJsonValue &v)
{
    // Permissions are serialized as strings because they exceed 53 bits.
    if (v.isString())
        return v.toString().toULongLong();
    if (v.isDouble())
        return static_cast<quint64>(v.toDouble());
    return 0;
}

QDateTime parseTimestamp(const QJsonValue &v)
{
    if (!v.isString())
        return {};
    return QDateTime::fromString(v.toString(), Qt::ISODateWithMs);
}
} // namespace

User User::fromJson(const QJsonObject &o)
{
    User u;
    u.id = o.value("id").toString();
    u.username = o.value("username").toString();
    u.globalName = o.value("global_name").toString();
    u.bot = o.value("bot").toBool();
    return u;
}

bool Channel::isText() const
{
    switch (type) {
    case ChannelType::GuildText:
    case ChannelType::DM:
    case ChannelType::GroupDM:
    case ChannelType::GuildAnnouncement:
    case ChannelType::AnnouncementThread:
    case ChannelType::PublicThread:
    case ChannelType::PrivateThread:
        return true;
    default:
        return false;
    }
}

bool Channel::isVoice() const
{
    return type == ChannelType::GuildVoice || type == ChannelType::GuildStageVoice;
}

Channel Channel::fromJson(const QJsonObject &o, const Snowflake &guildId)
{
    Channel c;
    c.id = o.value("id").toString();
    c.guildId = o.contains("guild_id") ? o.value("guild_id").toString() : guildId;
    c.parentId = o.value("parent_id").toString();
    c.type = static_cast<ChannelType>(o.value("type").toInt(-1));
    c.name = o.value("name").toString();
    c.topic = o.value("topic").toString();
    c.position = o.value("position").toInt();
    c.lastMessageId = o.value("last_message_id").toString();

    const QJsonArray ows = o.value("permission_overwrites").toArray();
    for (const QJsonValue &v : ows) {
        const QJsonObject ow = v.toObject();
        PermissionOverwrite p;
        p.id = ow.value("id").toString();
        const QJsonValue t = ow.value("type");
        p.type = t.isString() ? (t.toString() == "member" ? 1 : 0) : t.toInt();
        p.allow = parsePermissionString(ow.value("allow"));
        p.deny = parsePermissionString(ow.value("deny"));
        c.overwrites.push_back(p);
    }

    for (const QJsonValue &v : o.value("recipients").toArray()) {
        const User u = User::fromJson(v.toObject());
        c.recipients.push_back(u);
        c.recipientIds.push_back(u.id);
    }
    for (const QJsonValue &v : o.value("recipient_ids").toArray())
        c.recipientIds.push_back(v.toString());

    return c;
}

Role Role::fromJson(const QJsonObject &o)
{
    Role r;
    r.id = o.value("id").toString();
    r.name = o.value("name").toString();
    r.permissions = parsePermissionString(o.value("permissions"));
    r.position = o.value("position").toInt();
    r.color = static_cast<quint32>(o.value("color").toInt());
    return r;
}

const Role *Guild::role(const Snowflake &id) const
{
    for (const Role &r : roles) {
        if (r.id == id)
            return &r;
    }
    return nullptr;
}

Guild Guild::fromJson(const QJsonObject &o)
{
    Guild g;
    g.id = o.value("id").toString();
    g.unavailable = o.value("unavailable").toBool();

    // Some gateway payloads nest the guild metadata under "properties".
    const QJsonObject props = o.value("properties").toObject();
    g.name = o.contains("name") ? o.value("name").toString() : props.value("name").toString();
    g.ownerId = o.contains("owner_id") ? o.value("owner_id").toString() : props.value("owner_id").toString();

    for (const QJsonValue &v : o.value("roles").toArray())
        g.roles.push_back(Role::fromJson(v.toObject()));
    for (const QJsonValue &v : o.value("channels").toArray())
        g.channels.push_back(Channel::fromJson(v.toObject(), g.id));
    return g;
}

Message Message::fromJson(const QJsonObject &o)
{
    Message m;
    m.id = o.value("id").toString();
    m.channelId = o.value("channel_id").toString();
    m.author = User::fromJson(o.value("author").toObject());
    m.content = o.value("content").toString();
    m.timestamp = parseTimestamp(o.value("timestamp"));
    m.editedTimestamp = parseTimestamp(o.value("edited_timestamp"));
    m.type = o.value("type").toInt();
    m.embedCount = o.value("embeds").toArray().size();

    for (const QJsonValue &v : o.value("attachments").toArray()) {
        const QJsonObject a = v.toObject();
        m.attachments.push_back({a.value("filename").toString(), a.value("url").toString(),
                                 static_cast<qint64>(a.value("size").toDouble())});
    }
    for (const QJsonValue &v : o.value("mentions").toArray())
        m.mentions.push_back(User::fromJson(v.toObject()));

    const QJsonObject ref = o.value("referenced_message").toObject();
    if (!ref.isEmpty()) {
        m.replyAuthor = User::fromJson(ref.value("author").toObject()).displayName();
        m.replyContent = ref.value("content").toString();
    }
    return m;
}

quint64 computeChannelPermissions(const Guild &guild, const Channel &channel, const Snowflake &userId)
{
    if (!guild.ownerId.isEmpty() && guild.ownerId == userId)
        return Permissions::All;

    // Base permissions: @everyone (role id == guild id) plus every member role.
    quint64 perms = 0;
    if (const Role *everyone = guild.role(guild.id))
        perms = everyone->permissions;
    for (const Snowflake &rid : guild.myRoles) {
        if (const Role *r = guild.role(rid))
            perms |= r->permissions;
    }
    if (perms & Permissions::Administrator)
        return Permissions::All;

    // 1. @everyone overwrite
    for (const PermissionOverwrite &ow : channel.overwrites) {
        if (ow.id == guild.id) {
            perms &= ~ow.deny;
            perms |= ow.allow;
            break;
        }
    }
    // 2. Role overwrites (accumulated)
    quint64 allow = 0, deny = 0;
    for (const PermissionOverwrite &ow : channel.overwrites) {
        if (ow.type == 0 && guild.myRoles.contains(ow.id)) {
            allow |= ow.allow;
            deny |= ow.deny;
        }
    }
    perms &= ~deny;
    perms |= allow;
    // 3. Member overwrite
    for (const PermissionOverwrite &ow : channel.overwrites) {
        if (ow.type == 1 && ow.id == userId) {
            perms &= ~ow.deny;
            perms |= ow.allow;
            break;
        }
    }
    return perms;
}

} // namespace lightcord
