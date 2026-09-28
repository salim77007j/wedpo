// Wedpo browser — theme: palette, stylesheet, original SVG icon factory.
#pragma once
#include <QColor>
#include <QIcon>
#include <QPixmap>

namespace Theme
{

enum class Mode { Light, Dark };

// Applies the stylesheet for the configured theme; call once at startup and
// again whenever the theme or accent changes.
void apply();

Mode mode();
bool isDark();

// Semantic palette lookups: "bg", "surface", "text", "muted", "border",
// "hover", "tabstrip", "accent", "accentText", "danger", "ok".
QColor paletteColor(const QString &name);

// Recolored SVG icon factory. Icons in :/icons/<name>.svg must use the
// placeholder fill "#CUR"; it is replaced with `color` before rendering.
QPixmap icon(const QString &name, const QColor &color, int size = 18);
QIcon icon(const QString &name, int size = 18);

} // namespace Theme
