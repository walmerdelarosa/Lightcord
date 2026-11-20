// SPDX-License-Identifier: MIT
#pragma once

#include "Models.h"

#include <QString>

namespace lightcord {

class Session;

// Converts Discord messages to the small HTML subset understood by
// QTextBrowser. Supports the common Markdown flavour used by Discord
// (bold, italics, underline, strikethrough, spoilers, inline code, code
// blocks, block quotes), mentions, custom emoji, timestamps and links.
class MessageFormatter {
public:
    explicit MessageFormatter(const Session *session);

    QString contentToHtml(const QString &content) const;
    QString messageToHtml(const Message &message, bool grouped) const;

    // Default CSS for the message view, using the active theme colours.
    static QString styleSheet();

private:
    QString inlineMarkdown(const QString &escaped) const;
    QString replaceMentions(const QString &escaped) const;

    const Session *m_session;
};

} // namespace lightcord
