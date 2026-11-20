// SPDX-License-Identifier: MIT
#include "MessageFormatter.h"

#include "Session.h"
#include "ui/Theme.h"

#include <QLocale>
#include <QRegularExpression>
#include <QStringList>

namespace lightcord {

namespace {

QString humanSize(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

QString formatTimestampTag(qint64 epochSecs, const QString &style)
{
    const QDateTime dt = QDateTime::fromSecsSinceEpoch(epochSecs).toLocalTime();
    const QLocale loc;
    if (style == QLatin1String("t"))
        return loc.toString(dt.time(), QLocale::ShortFormat);
    if (style == QLatin1String("T"))
        return loc.toString(dt.time(), QLocale::LongFormat);
    if (style == QLatin1String("d"))
        return loc.toString(dt.date(), QLocale::ShortFormat);
    if (style == QLatin1String("D"))
        return loc.toString(dt.date(), QLocale::LongFormat);
    if (style == QLatin1String("R")) {
        const qint64 secs = QDateTime::currentDateTime().secsTo(dt);
        const qint64 a = qAbs(secs);
        QString amount;
        if (a < 60)
            amount = QObject::tr("%n second(s)", nullptr, int(a));
        else if (a < 3600)
            amount = QObject::tr("%n minute(s)", nullptr, int(a / 60));
        else if (a < 86400)
            amount = QObject::tr("%n hour(s)", nullptr, int(a / 3600));
        else
            amount = QObject::tr("%n day(s)", nullptr, int(a / 86400));
        return secs >= 0 ? QObject::tr("in %1").arg(amount) : QObject::tr("%1 ago").arg(amount);
    }
    return loc.toString(dt, QLocale::ShortFormat);
}

} // namespace

MessageFormatter::MessageFormatter(const Session *session)
    : m_session(session)
{
}

QString MessageFormatter::styleSheet()
{
    const ThemePalette &p = Theme::palette();
    return QStringLiteral(
               "body { color: %1; }"
               ".author { color: %2; font-weight: 600; }"
               ".bot { color: %3; background-color: %4; font-size: small; }"
               ".time { color: %5; font-size: small; }"
               ".edited { color: %5; font-size: small; }"
               ".reply { color: %5; font-size: small; }"
               ".mention { color: %3; background-color: %4; }"
               ".spoiler { background-color: %6; color: %6; }"
               ".quote { color: %5; }"
               ".system { color: %5; font-style: italic; }"
               "code { font-family: 'Cascadia Mono', Consolas, 'DejaVu Sans Mono', monospace; background-color: %7; }"
               "pre { font-family: 'Cascadia Mono', Consolas, 'DejaVu Sans Mono', monospace; background-color: %7; }"
               "a { color: %3; text-decoration: none; }")
        .arg(p.text.name(), p.textStrong.name(), p.accent.name(), p.accentSoft.name(), p.textMuted.name(),
             p.spoiler.name(), p.codeBackground.name());
}

QString MessageFormatter::replaceMentions(const QString &escaped) const
{
    QString out = escaped;

    // User mentions: <@123> or <@!123>  (already escaped as &lt;@123&gt;)
    static const QRegularExpression userRe(QStringLiteral("&lt;@!?(\\d+)&gt;"));
    QString result;
    qsizetype last = 0;
    auto it = userRe.globalMatch(out);
    while (it.hasNext()) {
        const auto m = it.next();
        result += out.mid(last, m.capturedStart() - last);
        QString name = m_session ? m_session->userDisplayName(m.captured(1)) : QString();
        if (name.isEmpty())
            name = QObject::tr("user");
        result += QStringLiteral("<span class=\"mention\">@%1</span>").arg(name.toHtmlEscaped());
        last = m.capturedEnd();
    }
    result += out.mid(last);
    out = result;

    // Channel mentions: <#123>
    static const QRegularExpression chanRe(QStringLiteral("&lt;#(\\d+)&gt;"));
    result.clear();
    last = 0;
    it = chanRe.globalMatch(out);
    while (it.hasNext()) {
        const auto m = it.next();
        result += out.mid(last, m.capturedStart() - last);
        const Channel *c = m_session ? m_session->channel(m.captured(1)) : nullptr;
        const QString name = c ? c->name : QObject::tr("channel");
        result += QStringLiteral("<span class=\"mention\">#%1</span>").arg(name.toHtmlEscaped());
        last = m.capturedEnd();
    }
    result += out.mid(last);
    out = result;

    // Role mentions: <@&123>
    static const QRegularExpression roleRe(QStringLiteral("&lt;@&amp;(\\d+)&gt;"));
    out.replace(roleRe, QStringLiteral("<span class=\"mention\">@role</span>"));

    // Custom emoji: <:name:id> / <a:name:id>  -> :name:
    static const QRegularExpression emojiRe(QStringLiteral("&lt;a?:(\\w+):\\d+&gt;"));
    out.replace(emojiRe, QStringLiteral(":\\1:"));

    // Timestamps: <t:1700000000> or <t:1700000000:R>
    static const QRegularExpression tsRe(QStringLiteral("&lt;t:(-?\\d+)(?::([tTdDfFR]))?&gt;"));
    result.clear();
    last = 0;
    it = tsRe.globalMatch(out);
    while (it.hasNext()) {
        const auto m = it.next();
        result += out.mid(last, m.capturedStart() - last);
        result += QStringLiteral("<span class=\"mention\">%1</span>")
                      .arg(formatTimestampTag(m.captured(1).toLongLong(), m.captured(2)).toHtmlEscaped());
        last = m.capturedEnd();
    }
    result += out.mid(last);
    out = result;

    // @everyone / @here
    static const QRegularExpression massRe(QStringLiteral("@(everyone|here)\\b"));
    out.replace(massRe, QStringLiteral("<span class=\"mention\">@\\1</span>"));
    return out;
}

QString MessageFormatter::inlineMarkdown(const QString &escaped) const
{
    // Protect inline code spans from further formatting.
    QStringList codeSpans;
    QString text = escaped;
    static const QRegularExpression codeRe(QStringLiteral("`([^`\\n]+)`"));
    {
        QString result;
        qsizetype last = 0;
        auto it = codeRe.globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            result += text.mid(last, m.capturedStart() - last);
            result += QStringLiteral("\x01%1\x01").arg(codeSpans.size());
            codeSpans << m.captured(1);
            last = m.capturedEnd();
        }
        result += text.mid(last);
        text = result;
    }

    // Links (before emphasis so that underscores in URLs are untouched).
    QStringList links;
    // "<https://...>" (link without embed) arrives escaped; unwrap it first.
    static const QRegularExpression wrappedUrlRe(QStringLiteral("&lt;(https?://(?:[^\\s<&]|&amp;)+)&gt;"));
    text.replace(wrappedUrlRe, QStringLiteral("\\1"));
    static const QRegularExpression urlRe(
        QStringLiteral("(https?://(?:[^\\s<&]|&amp;)*(?:[^\\s<&.,:;\"')\\]!?]|&amp;))"));
    {
        QString result;
        qsizetype last = 0;
        auto it = urlRe.globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            result += text.mid(last, m.capturedStart() - last);
            result += QStringLiteral("\x02%1\x02").arg(links.size());
            links << m.captured(1);
            last = m.capturedEnd();
        }
        result += text.mid(last);
        text = result;
    }

    text = replaceMentions(text);

    static const QRegularExpression boldRe(QStringLiteral("\\*\\*(.+?)\\*\\*"));
    static const QRegularExpression underlineRe(QStringLiteral("__(.+?)__"));
    static const QRegularExpression italicStarRe(QStringLiteral("\\*(?!\\s)(.+?)\\*"));
    static const QRegularExpression italicUnderRe(QStringLiteral("\\b_(?!\\s)(.+?)_\\b"));
    static const QRegularExpression strikeRe(QStringLiteral("~~(.+?)~~"));
    static const QRegularExpression spoilerRe(QStringLiteral("\\|\\|(.+?)\\|\\|"));

    text.replace(boldRe, QStringLiteral("<b>\\1</b>"));
    text.replace(underlineRe, QStringLiteral("<u>\\1</u>"));
    text.replace(italicStarRe, QStringLiteral("<i>\\1</i>"));
    text.replace(italicUnderRe, QStringLiteral("<i>\\1</i>"));
    text.replace(strikeRe, QStringLiteral("<s>\\1</s>"));
    text.replace(spoilerRe, QStringLiteral("<span class=\"spoiler\">\\1</span>"));

    for (int i = 0; i < links.size(); ++i) {
        const QString url = links.at(i);
        text.replace(QStringLiteral("\x02%1\x02").arg(i), QStringLiteral("<a href=\"%1\">%1</a>").arg(url));
    }
    for (int i = 0; i < codeSpans.size(); ++i)
        text.replace(QStringLiteral("\x01%1\x01").arg(i), QStringLiteral("<code>%1</code>").arg(codeSpans.at(i)));
    return text;
}

QString MessageFormatter::contentToHtml(const QString &content) const
{
    QString html;
    // Split out fenced code blocks first: ```lang\n...```
    static const QRegularExpression fenceRe(QStringLiteral("```(?:[\\w+-]*\\n)?([\\s\\S]*?)```"));
    qsizetype last = 0;
    auto it = fenceRe.globalMatch(content);

    auto renderText = [this](const QString &raw) {
        QStringList lines = raw.split(QLatin1Char('\n'));
        QStringList out;
        for (const QString &line : lines) {
            if (line.startsWith(QLatin1String("> "))) {
                out << QStringLiteral("<span class=\"quote\">▎ %1</span>")
                           .arg(inlineMarkdown(line.mid(2).toHtmlEscaped()));
            } else {
                out << inlineMarkdown(line.toHtmlEscaped());
            }
        }
        return out.join(QStringLiteral("<br>"));
    };

    while (it.hasNext()) {
        const auto m = it.next();
        html += renderText(content.mid(last, m.capturedStart() - last));
        QString code = m.captured(1);
        if (code.endsWith(QLatin1Char('\n')))
            code.chop(1);
        html += QStringLiteral("<pre>%1</pre>").arg(code.toHtmlEscaped());
        last = m.capturedEnd();
    }
    html += renderText(content.mid(last));
    return html;
}

QString MessageFormatter::messageToHtml(const Message &m, bool grouped) const
{
    const QLocale loc;
    const QDateTime local = m.timestamp.toLocalTime();
    const QString time = local.date() == QDate::currentDate() ? loc.toString(local.time(), QLocale::ShortFormat)
                                                              : loc.toString(local, QLocale::ShortFormat);

    QString body;
    if (m.type == 7) {
        body = QStringLiteral("<span class=\"system\">%1</span>")
                   .arg(QObject::tr("%1 joined the server.").arg(m.author.displayName().toHtmlEscaped()));
    } else {
        body = contentToHtml(m.content);
    }
    if (m.editedTimestamp.isValid())
        body += QStringLiteral(" <span class=\"edited\">%1</span>").arg(QObject::tr("(edited)"));
    for (const Attachment &a : m.attachments) {
        body += QStringLiteral("<br>📎 <a href=\"%1\">%2</a> <span class=\"time\">%3</span>")
                    .arg(a.url.toHtmlEscaped(), a.filename.toHtmlEscaped(), humanSize(a.size));
    }
    if (m.embedCount > 0 && m.content.isEmpty() && m.attachments.isEmpty())
        body += QStringLiteral("<span class=\"system\">%1</span>").arg(QObject::tr("[embed]"));

    QString html;
    if (!m.replyAuthor.isEmpty()) {
        QString preview = m.replyContent.left(80);
        if (m.replyContent.size() > 80)
            preview += QStringLiteral("…");
        html += QStringLiteral("<div class=\"reply\">↱ <b>%1</b> %2</div>")
                    .arg(m.replyAuthor.toHtmlEscaped(), preview.toHtmlEscaped());
        grouped = false;
    }

    if (grouped) {
        html += QStringLiteral("<div style=\"margin-left: 0px;\">%1</div>").arg(body);
    } else {
        const QString botTag = m.author.bot ? QStringLiteral(" <span class=\"bot\">&nbsp;BOT&nbsp;</span>") : QString();
        html += QStringLiteral("<div style=\"margin-top: 8px;\"><span class=\"author\">%1</span>%2 "
                               "<span class=\"time\">%3</span><br>%4</div>")
                    .arg(m.author.displayName().toHtmlEscaped(), botTag, time, body);
    }
    return html;
}

} // namespace lightcord
