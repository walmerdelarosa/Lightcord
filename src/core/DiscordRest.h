// SPDX-License-Identifier: MIT
#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>

#include <functional>

namespace lightcord {

// Minimal asynchronous client for the Discord HTTP API (v10).
// Handles authentication headers and HTTP 429 rate-limit retries.
class DiscordRest : public QObject {
    Q_OBJECT
public:
    struct Reply {
        bool ok = false;
        int status = 0;
        QJsonDocument json;
        QString error;
    };
    using Callback = std::function<void(const Reply &)>;

    static constexpr const char *kApiBase = "https://discord.com/api/v10";

    explicit DiscordRest(QObject *parent = nullptr);

    void setToken(const QString &token, bool isBot);
    void clearToken();

    // `context` guards the callback: if it is destroyed first, the callback is skipped.
    void get(const QString &path, QObject *context, Callback cb);
    void post(const QString &path, const QJsonObject &body, QObject *context, Callback cb);

private:
    void send(const QByteArray &verb, const QString &path, const QByteArray &body,
              QPointer<QObject> context, Callback cb, int attempt);

    QNetworkAccessManager m_nam;
    QByteArray m_authHeader;
};

} // namespace lightcord
