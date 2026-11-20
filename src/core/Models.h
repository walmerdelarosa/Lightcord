// SPDX-License-Identifier: MIT
#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace lightcord {

// Discord IDs are 64-bit "snowflakes"; they are kept as strings because
// that is how the API transmits them and it avoids precision issues.
using Snowflake = QString;

namespace Permissions {
constexpr quint64 Stream = 1ull << 9;
constexpr quint64 Administrator = 1ull << 3;
constexpr quint64 ViewChannel = 1ull << 10;
constexpr quint64 SendMessages = 1ull << 11;
constexpr quint64 ReadMessageHistory = 1ull << 16;
constexpr quint64 Connect = 1ull << 20;
constexpr quint64 Speak = 1ull << 21;
constexpr quint64 All = ~0ull;
} // namespace Permissions

struct User {
    Snowflake id;
    QString username;
    QString globalName;
    bool bot = false;

    QString displayName() const { return globalName.isEmpty() ? username : globalName; }
    static User fromJson(const QJsonObject &o);
};

enum class ChannelType {
    Unknown = -1,
    GuildText = 0,
    DM = 1,
    GuildVoice = 2,
    GroupDM = 3,
    GuildCategory = 4,
    GuildAnnouncement = 5,
    AnnouncementThread = 10,
    PublicThread = 11,
    PrivateThread = 12,
    GuildStageVoice = 13,
    GuildDirectory = 14,
    GuildForum = 15,
    GuildMedia = 16,
};

struct PermissionOverwrite {
    Snowflake id;
    int type = 0; // 0 = role, 1 = member
    quint64 allow = 0;
    quint64 deny = 0;
};

struct Channel {
    Snowflake id;
    Snowflake guildId;
    Snowflake parentId;
    ChannelType type = ChannelType::Unknown;
    QString name;
    QString topic;
    int position = 0;
    QVector<PermissionOverwrite> overwrites;
    QVector<Snowflake> recipientIds;
    QVector<User> recipients;
    Snowflake lastMessageId;

    bool isText() const;
    bool isVoice() const;
    bool isCategory() const { return type == ChannelType::GuildCategory; }
    bool isPrivate() const { return type == ChannelType::DM || type == ChannelType::GroupDM; }

    static Channel fromJson(const QJsonObject &o, const Snowflake &guildId = {});
};

struct Role {
    Snowflake id;
    QString name;
    quint64 permissions = 0;
    int position = 0;
    quint32 color = 0;
    static Role fromJson(const QJsonObject &o);
};

struct Guild {
    Snowflake id;
    QString name;
    Snowflake ownerId;
    QVector<Role> roles;
    QVector<Channel> channels;
    bool unavailable = false;

    // Roles of the logged-in user in this guild. Empty + rolesKnown=false means
    // "unknown", in which case channels are not filtered by permissions.
    QVector<Snowflake> myRoles;
    bool myRolesKnown = false;

    const Role *role(const Snowflake &id) const;
    static Guild fromJson(const QJsonObject &o);
};

struct Attachment {
    QString filename;
    QString url;
    qint64 size = 0;
};

struct Message {
    Snowflake id;
    Snowflake channelId;
    User author;
    QString content;
    QDateTime timestamp;
    QDateTime editedTimestamp;
    QVector<Attachment> attachments;
    QVector<User> mentions;
    int embedCount = 0;
    int type = 0;
    QString replyAuthor;
    QString replyContent;

    static Message fromJson(const QJsonObject &o);
};

// Computes the effective permissions of `userId` in `channel` following the
// algorithm documented by Discord (base role permissions, then @everyone,
// role and member overwrites).
quint64 computeChannelPermissions(const Guild &guild, const Channel &channel, const Snowflake &userId);

} // namespace lightcord
