// SPDX-License-Identifier: MIT
#include "Theme.h"

#include <QApplication>
#include <QFile>
#include <QFont>
#include <QPainter>
#include <QPalette>
#include <QSvgRenderer>

namespace lightcord {

namespace {
ThemePalette g_palette = Theme::paletteFor(QStringLiteral("dark"));

QByteArray readResource(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}
} // namespace

ThemePalette Theme::paletteFor(const QString &name)
{
    ThemePalette p;
    if (name == QLatin1String("light")) {
        p.name = QStringLiteral("light");
        p.dark = false;
        p.window = QColor("#FAF8F4");
        p.sidebar = QColor("#F1EEE7");
        p.surface = QColor("#FFFFFF");
        p.surfaceHover = QColor("#E9E5DC");
        p.border = QColor("#DED9CE");
        p.selection = QColor("#EADFC8");
        p.text = QColor("#2B2E35");
        p.textStrong = QColor("#111317");
        p.textMuted = QColor("#6E7280");
        p.accent = QColor("#B9770E");
        p.accentHover = QColor("#A0660A");
        p.accentText = QColor("#FFFFFF");
        p.accentSoft = QColor("#F6E7CB");
        p.danger = QColor("#C8323F");
        p.dangerSoft = QColor("#F7D9DC");
        p.success = QColor("#23864F");
        p.spoiler = QColor("#2B2E35");
        p.codeBackground = QColor("#F0ECE3");
    } else {
        p.name = QStringLiteral("dark");
        p.dark = true;
        p.window = QColor("#111318");
        p.sidebar = QColor("#16191F");
        p.surface = QColor("#1D2129");
        p.surfaceHover = QColor("#262B35");
        p.border = QColor("#2A2F3A");
        p.selection = QColor("#2F3542");
        p.text = QColor("#D7DBE3");
        p.textStrong = QColor("#F4F6FA");
        p.textMuted = QColor("#848B99");
        p.accent = QColor("#FFB547");
        p.accentHover = QColor("#FFC56E");
        p.accentText = QColor("#1A1300");
        p.accentSoft = QColor("#3A2E17");
        p.danger = QColor("#F0616D");
        p.dangerSoft = QColor("#4A2227");
        p.success = QColor("#4CC38A");
        p.spoiler = QColor("#3A3F4B");
        p.codeBackground = QColor("#0C0E12");
    }
    return p;
}

const ThemePalette &Theme::palette()
{
    return g_palette;
}

void Theme::apply(const QString &name, int fontPointSize)
{
    g_palette = paletteFor(name);
    const ThemePalette &p = g_palette;

    QPalette pal;
    pal.setColor(QPalette::Window, p.window);
    pal.setColor(QPalette::WindowText, p.text);
    pal.setColor(QPalette::Base, p.surface);
    pal.setColor(QPalette::AlternateBase, p.sidebar);
    pal.setColor(QPalette::Text, p.text);
    pal.setColor(QPalette::BrightText, p.textStrong);
    pal.setColor(QPalette::Button, p.surface);
    pal.setColor(QPalette::ButtonText, p.text);
    pal.setColor(QPalette::Highlight, p.accent);
    pal.setColor(QPalette::HighlightedText, p.accentText);
    pal.setColor(QPalette::Link, p.accent);
    pal.setColor(QPalette::ToolTipBase, p.surface);
    pal.setColor(QPalette::ToolTipText, p.text);
    pal.setColor(QPalette::PlaceholderText, p.textMuted);
    pal.setColor(QPalette::Disabled, QPalette::Text, p.textMuted);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, p.textMuted);
    pal.setColor(QPalette::Disabled, QPalette::WindowText, p.textMuted);
    QApplication::setPalette(pal);

    QFont font = QApplication::font();
    font.setPointSize(qBound(8, fontPointSize, 18));
    QApplication::setFont(font);

    QString qss = QString::fromUtf8(readResource(QStringLiteral(":/themes/base.qss")));
    const QList<QPair<QString, QColor>> tokens = {
        {"window", p.window},          {"sidebar", p.sidebar},
        {"surfaceHover", p.surfaceHover}, {"surface", p.surface},
        {"border", p.border},          {"selection", p.selection},
        {"textStrong", p.textStrong},  {"textMuted", p.textMuted},
        {"text", p.text},              {"accentHover", p.accentHover},
        {"accentText", p.accentText},  {"accentSoft", p.accentSoft},
        {"accent", p.accent},          {"dangerSoft", p.dangerSoft},
        {"danger", p.danger},          {"success", p.success},
    };
    for (const auto &t : tokens)
        qss.replace(QStringLiteral("{{%1}}").arg(t.first), t.second.name());
    qApp->setStyleSheet(qss);
}

QIcon Theme::icon(const QString &name, const QColor &color)
{
    QByteArray svg = readResource(QStringLiteral(":/icons/%1.svg").arg(name));
    if (svg.isEmpty())
        return {};
    const QColor c = color.isValid() ? color : g_palette.text;
    svg.replace("currentColor", c.name().toLatin1());

    QSvgRenderer renderer(svg);
    QIcon icon;
    for (int size : {16, 20, 24, 32, 48, 64}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter painter(&pm);
        painter.setRenderHint(QPainter::Antialiasing);
        renderer.render(&painter);
        painter.end();
        icon.addPixmap(pm);
    }
    return icon;
}

QPixmap Theme::logo(int size)
{
    QSvgRenderer renderer(QStringLiteral(":/icons/lightcord.svg"));
    const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    QPixmap pm(QSize(size, size) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter, QRectF(0, 0, size, size));
    return pm;
}

} // namespace lightcord
