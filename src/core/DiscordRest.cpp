// SPDX-License-Identifier: MIT
#include "DiscordRest.h"

#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace lightcord {

namespace {
constexpr int kMaxAttempts = 3;

QByteArray userAgent()
{
    return QByteArray("DiscordBot (https://github.com/walmerdelarosa/Lightcord, ") + LIGHTCORD_VERSION + ")";
}
} // namespace

DiscordRest::DiscordRest(QObject *parent)
    : QObject(parent)
{
}

void DiscordRest::setToken(const QString &token, bool isBot)
{
    m_authHeader = (isBot ? QStringLiteral("Bot ") + token : token).toUtf8();
}

void DiscordRest::clearToken()
{
    m_authHeader.clear();
}

void DiscordRest::get(const QString &path, QObject *context, Callback cb)
{
    send("GET", path, {}, context, std::move(cb), 1);
}

void DiscordRest::post(const QString &path, const QJsonObject &body, QObject *context, Callback cb)
{
    send("POST", path, QJsonDocument(body).toJson(QJsonDocument::Compact), context, std::move(cb), 1);
}

void DiscordRest::send(const QByteArray &verb, const QString &path, const QByteArray &body,
                       QPointer<QObject> context, Callback cb, int attempt)
{
    QNetworkRequest req(QUrl(QString::fromLatin1(kApiBase) + path));
    req.setRawHeader("Authorization", m_authHeader);
    req.setRawHeader("User-Agent", userAgent());
    if (!body.isEmpty())
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply *reply = m_nam.sendCustomRequest(req, verb, body);
    const bool hadContext = !context.isNull();

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, verb, path, body, context, cb, attempt, hadContext]() {
                reply->deleteLater();
                if (hadContext && context.isNull())
                    return;

                const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const QByteArray data = reply->readAll();

                QJsonParseError perr{};
                const QJsonDocument doc = QJsonDocument::fromJson(data, &perr);

                if (status == 429 && attempt < kMaxAttempts) {
                    const double retryAfter = doc.object().value("retry_after").toDouble(1.0);
                    const int delayMs = qBound(250, static_cast<int>(retryAfter * 1000.0) + 50, 30000);
                    QTimer::singleShot(delayMs, this, [=]() { send(verb, path, body, context, cb, attempt + 1); });
                    return;
                }

                Reply r;
                r.status = status;
                r.json = doc;
                r.ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
                if (!r.ok) {
                    const QString apiMessage = doc.object().value("message").toString();
                    r.error = !apiMessage.isEmpty() ? apiMessage : reply->errorString();
                }
                if (cb)
                    cb(r);
            });
}

} // namespace lightcord
