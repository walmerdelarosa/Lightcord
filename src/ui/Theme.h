// SPDX-License-Identifier: MIT
#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>

namespace lightcord {

struct ThemePalette {
    QString name;
    bool dark = true;
    QColor window;
    QColor sidebar;
    QColor surface;
    QColor surfaceHover;
    QColor border;
    QColor selection;
    QColor text;
    QColor textStrong;
    QColor textMuted;
    QColor accent;
    QColor accentHover;
    QColor accentText;
    QColor accentSoft;
    QColor danger;
    QColor dangerSoft;
    QColor success;
    QColor spoiler;
    QColor codeBackground;
};

// Lightcord visual identity: two palettes ("Night" and "Day") built around a
// warm amber accent, a token-based stylesheet and recolourable SVG icons.
class Theme {
public:
    static void apply(const QString &name, int fontPointSize);
    static const ThemePalette &palette();
    static ThemePalette paletteFor(const QString &name);

    // Renders resources/icons/<name>.svg tinted with `color` (text colour by default).
    static QIcon icon(const QString &name, const QColor &color = QColor());
    static QPixmap logo(int size);
};

} // namespace lightcord
