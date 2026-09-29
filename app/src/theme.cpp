#include "theme.h"
#include "settings.h"

#include <QApplication>
#include <QStyleHints>
#include <QFile>
#include <QSvgRenderer>
#include <QPainter>
#include <QHash>
#include <QPalette>

namespace
{

Theme::Mode s_mode = Theme::Mode::Light;
QString s_accent = "#2563eb";

struct Palette {
    const char *bg, *surface, *text, *muted, *border, *hover, *tabstrip;
    const char *danger, *ok, *shadow;
};

const Palette kLight = {"#f5f6f8", "#ffffff", "#1f2430", "#6b7280", "#e5e7eb",
                        "#ececf1", "#e8eaed", "#dc2626", "#16a34a", "rgba(0,0,0,0.10)"};
const Palette kDark = {"#1a1d23", "#24272e", "#e5e7eb", "#9ca3af", "#33363d",
                       "#2a2e35", "#15171b", "#ef4444", "#22c55e", "rgba(0,0,0,0.45)"};

const Palette &palette()
{
    return Theme::isDark() ? kDark : kLight;
}

QString qss()
{
    const Palette &p = palette();
    const QString accent = s_accent;
    return QStringLiteral(R"(
* { outline: none; }
QWidget { background: %1; color: %2; font-size: 13px; }
QMainWindow { background: %1; }
QToolTip { background: %3; color: %2; border: 1px solid %4; padding: 4px 8px; border-radius: 4px; }

QWidget#tabstrip { background: %5; }
QWidget#tabstrip QToolButton { background: transparent; border: none; border-radius: 6px; padding: 4px; }
QWidget#tabstrip QToolButton:hover { background: %6; }

QWidget#toolbar { background: %1; border-bottom: 1px solid %4; }
QToolButton { background: transparent; border: none; border-radius: 8px; padding: 6px; }
QToolButton:hover { background: %6; }
QToolButton:pressed { background: %4; }
QToolButton:checked { background: %6; }

QLineEdit#omnibox {
    background: %3; border: 1px solid %4; border-radius: 17px; padding: 4px 10px 4px 6px;
    min-height: 24px; max-height: 28px; selection-background-color: %7; selection-color: #ffffff;
}
QLineEdit#omnibox:focus { border: 2px solid %7; }

QPushButton {
    background: %3; border: 1px solid %4; border-radius: 8px; padding: 6px 14px;
}
QPushButton:hover { background: %6; }
QPushButton#primary { background: %7; color: #ffffff; border: none; }
QPushButton#primary:hover { opacity: 0.9; }
QPushButton:disabled { color: %8; }

QMenu { background: %3; border: 1px solid %4; border-radius: 10px; padding: 6px; }
QMenu::item { padding: 7px 24px 7px 12px; border-radius: 6px; }
QMenu::item:selected { background: %6; }
QMenu::separator { height: 1px; background: %4; margin: 5px 8px; }
QMenu::icon { padding-left: 8px; }

QScrollBar:vertical { background: transparent; width: 12px; margin: 2px; }
QScrollBar::handle:vertical { background: %8; border-radius: 5px; min-height: 32px; }
QScrollBar::handle:vertical:hover { background: %9; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar:horizontal { background: transparent; height: 12px; margin: 2px; }
QScrollBar::handle:horizontal { background: %8; border-radius: 5px; min-width: 32px; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }

QComboBox { background: %3; border: 1px solid %4; border-radius: 8px; padding: 5px 10px; }
QComboBox:hover { background: %6; }
QComboBox QAbstractItemView { background: %3; border: 1px solid %4; selection-background-color: %6; }

QCheckBox { spacing: 8px; }
QCheckBox::indicator { width: 18px; height: 18px; border: 1px solid %4; border-radius: 5px; background: %3; }
QCheckBox::indicator:checked { background: %7; border-color: %7; image: url(:/icons/check.svg); }
QRadioButton::indicator { width: 18px; height: 18px; border: 1px solid %4; border-radius: 9px; background: %3; }
QRadioButton::indicator:checked { border: 5px solid %7; background: #ffffff; }

QListView, QTreeView, QTableView, QListWidget, QTreeWidget, QTableWidget {
    background: %3; border: 1px solid %4; border-radius: 10px; padding: 4px;
    alternate-background-color: %1;
}
QListView::item, QTreeView::item, QTableView::item { padding: 6px; border-radius: 6px; }
QListView::item:selected, QTreeView::item:selected, QTableView::item:selected { background: %6; color: %2; }
QHeaderView::section { background: %1; border: none; padding: 8px; font-weight: 600; }

QProgressBar { background: %4; border: none; border-radius: 4px; height: 8px; text-align: center; color: transparent; }
QProgressBar::chunk { background: %7; border-radius: 4px; }

QGroupBox { border: 1px solid %4; border-radius: 12px; margin-top: 16px; padding: 12px; font-weight: 600; }
QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; color: %8; }

QTabWidget::pane { border: 1px solid %4; border-radius: 0 0 10px 10px; background: %3; }
QTabBar::tab { background: transparent; padding: 8px 18px; color: %8; }
QTabBar::tab:selected { color: %2; border-bottom: 2px solid %7; }

QLabel#welcome { font-size: 26px; font-weight: 700; }
QLabel#muted { color: %8; }
QLabel#h1 { font-size: 22px; font-weight: 700; }
QLabel#h2 { font-size: 16px; font-weight: 600; }
QLabel#stat { font-size: 34px; font-weight: 800; color: %7; }
QLabel#statmuted { font-size: 34px; font-weight: 800; color: %8; }

QFrame#card { background: %3; border: 1px solid %4; border-radius: 14px; }
QFrame#hr { background: %4; max-height: 1px; border: none; }
)")
        .arg(p.bg, p.text, p.surface, p.border, p.tabstrip, p.hover, accent, p.muted,
             Theme::isDark() ? "#4b5563" : "#c3c7cf");
}

QHash<QString, QPixmap> s_cache;

} // namespace

void Theme::apply()
{
    const QString mode = Settings::instance()->theme();
    if (mode == "dark")
        s_mode = Mode::Dark;
    else if (mode == "light")
        s_mode = Mode::Light;
    else
        s_mode = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark ? Mode::Dark : Mode::Light;

    static const QStringList accents = {"#2563eb", "#0d9488", "#7c3aed", "#db2777",
                                        "#ea580c", "#16a34a", "#475569", "#dc2626"};
    const int idx = Settings::instance()->accentIndex();
    s_accent = accents.value(qBound(0, idx, accents.size() - 1), accents.first());

    qApp->setStyleSheet(qss());
    s_cache.clear();
}

Theme::Mode Theme::mode() { return s_mode; }
bool Theme::isDark() { return s_mode == Mode::Dark; }

QColor Theme::paletteColor(const QString &name)
{
    const Palette &p = palette();
    if (name == "bg") return QColor(p.bg);
    if (name == "surface") return QColor(p.surface);
    if (name == "text") return QColor(p.text);
    if (name == "muted") return QColor(p.muted);
    if (name == "border") return QColor(p.border);
    if (name == "hover") return QColor(p.hover);
    if (name == "tabstrip") return QColor(p.tabstrip);
    if (name == "danger") return QColor(p.danger);
    if (name == "ok") return QColor(p.ok);
    if (name == "accent") return QColor(s_accent);
    if (name == "accentText") return QColor("#ffffff");
    return QColor(p.text);
}

QPixmap Theme::icon(const QString &name, const QColor &color, int size)
{
    const QString key = QStringLiteral("%1|%2|%3").arg(name, color.name()).arg(size);
    auto it = s_cache.constFind(key);
    if (it != s_cache.constEnd())
        return it.value();

    QFile f(QStringLiteral(":/icons/%1.svg").arg(name));
    if (!f.open(QIODevice::ReadOnly))
        return QPixmap();
    QString svg = QString::fromUtf8(f.readAll());
    f.close();
    svg.replace(QStringLiteral("#CUR"), color.name());

    QSvgRenderer renderer(svg.toUtf8());
    const qreal dpr = QGuiApplication::primaryScreen()
                          ? QGuiApplication::primaryScreen()->devicePixelRatio() : 1.0;
    QPixmap pm(int(size * dpr), int(size * dpr));
    pm.fill(Qt::transparent);
    QPainter painter(&pm);
    renderer.render(&painter, QRectF(0, 0, pm.width(), pm.height()));
    painter.end();
    pm.setDevicePixelRatio(dpr);
    s_cache.insert(key, pm);
    return pm;
}

QIcon Theme::icon(const QString &name, int size)
{
    return QIcon(icon(name, paletteColor("text"), size));
}
