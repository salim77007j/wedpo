#include "tabstrip.h"
#include "theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QStyleOption>
#include <QTimer>
#include <QtMath>
#include <algorithm>
#include <cmath>

static constexpr int kStripHeight = 38;
static constexpr int kPinnedWidth = 44;
static constexpr int kMinTabWidth = 100;
static constexpr int kMaxTabWidth = 232;
static constexpr int kNewTabWidth = 34;
static constexpr int kTabGap = 2;
static constexpr int kStripPad = 8;
static constexpr int kRadius = 10;

TabStrip::TabStrip(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("tabstrip"));
    setFixedHeight(kStripHeight);
    setMouseTracking(true);
    connect(&m_spinnerTimer, &QTimer::timeout, this, [this]() {
        m_spinnerAngle = std::fmod(m_spinnerAngle + 30.0, 360.0);
        if (std::any_of(m_tabs.cbegin(), m_tabs.cend(), [](const TabInfo &t) { return t.loading; }))
            update();
        else
            m_spinnerTimer.stop();
    });
    m_spinnerTimer.setInterval(80);
}

bool TabStrip::pointInInteractiveArea(const QPoint &localPos) const
{
    // tabs and the + button consume mouse events; empty strip is caption area
    if (tabAt(localPos) >= 0)
        return true;
    QList<QRect> rects;
    layoutTabs(&rects);
    const int lastRight = rects.isEmpty() ? kStripPad : rects.last().right();
    const QRect plusRect(lastRight + 8, (height() - 26) / 2, 26, 26);
    return plusRect.contains(localPos);
}

QSize TabStrip::sizeHint() const { return {640, kStripHeight}; }
QSize TabStrip::minimumSizeHint() const { return {280, kStripHeight}; }

void TabStrip::setTabs(const QList<TabInfo> &tabs)
{
    m_tabs = tabs;
    const bool loading = std::any_of(m_tabs.cbegin(), m_tabs.cend(),
                                     [](const TabInfo &t) { return t.loading; });
    if (loading && !m_spinnerTimer.isActive())
        m_spinnerTimer.start();
    update();
}

void TabStrip::layoutTabs(QList<QRect> *out) const
{
    out->clear();
    const int n = m_tabs.size();
    if (n == 0)
        return;
    int pinnedCount = 0;
    for (const TabInfo &t : m_tabs)
        if (t.pinned)
            ++pinnedCount;
    const int avail = width() - kStripPad - kNewTabWidth - kTabGap;
    const int normalCount = n - pinnedCount;
    int w = kMaxTabWidth;
    if (normalCount > 0) {
        w = (avail - pinnedCount * (kPinnedWidth + kTabGap) - normalCount * kTabGap) / normalCount;
        w = qBound(kMinTabWidth, w, kMaxTabWidth);
    }
    int x = kStripPad;
    for (const TabInfo &t : m_tabs) {
        const int tw = t.pinned ? kPinnedWidth : w;
        out->append(QRect(x, 2, tw, kStripHeight - 6));
        x += tw + kTabGap;
    }
}

int TabStrip::tabAt(const QPoint &pos, QRect *rect) const
{
    QList<QRect> rects;
    layoutTabs(&rects);
    for (int i = 0; i < rects.size(); ++i) {
        if (rects[i].adjusted(-kTabGap / 2, 0, kTabGap / 2, 0).contains(pos)) {
            if (rect)
                *rect = rects[i];
            return i;
        }
    }
    return -1;
}

QRect TabStrip::closeButtonRect(const QRect &tabRect) const
{
    return QRect(tabRect.right() - 24, tabRect.top() + (tabRect.height() - 18) / 2, 18, 18);
}

QRect TabStrip::audioRect(const QRect &tabRect) const
{
    return QRect(tabRect.right() - 26, tabRect.top() + (tabRect.height() - 16) / 2, 16, 16);
}

void TabStrip::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QColor strip = Theme::paletteColor("tabstrip");
    const QColor surface = Theme::paletteColor("surface");
    const QColor text = Theme::paletteColor("text");
    const QColor muted = Theme::paletteColor("muted");
    const QColor danger = Theme::paletteColor("danger");

    p.fillRect(rect(), strip);

    QList<QRect> rects;
    layoutTabs(&rects);

    for (int i = 0; i < m_tabs.size(); ++i) {
        const TabInfo &t = m_tabs[i];
        const QRect r = rects[i];
        const bool active = t.active;
        const bool hovered = (i == m_hoverTab && !active);

        // tab body
        QPainterPath path;
        path.addRoundedRect(r, kRadius, kRadius);
        p.fillPath(path, active ? surface : (hovered ? Theme::paletteColor("hover") : QColor(Qt::transparent)));

        // favicon / spinner
        const int iconX = r.left() + 10;
        const int iconY = r.top() + (r.height() - 16) / 2;
        if (t.loading) {
            QPen pen(Theme::paletteColor("accent"), 2);
            pen.setCapStyle(Qt::RoundCap);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawArc(QRect(iconX, iconY, 16, 16), int(m_spinnerAngle * 16), 100 * 16);
        } else if (!t.icon.isNull()) {
            p.drawPixmap(iconX, iconY, 16, 16, t.icon);
        } else {
            p.drawPixmap(iconX, iconY, 16, 16, Theme::icon("globe", muted, 16));
        }

        // title (hidden on pinned)
        if (!t.pinned) {
            int textX = iconX + 16 + 8;
            int textW = r.width() - (textX - r.left()) - 30;
            if (t.audible || t.muted)
                textW -= 20;
            const QFontMetrics fm(font());
            QString title = fm.elidedText(t.title.isEmpty() ? tr("New tab") : t.title, Qt::ElideRight, textW);
            p.setPen(active ? text : muted);
            p.drawText(QRect(textX, r.top(), textW, r.height()), Qt::AlignVCenter | Qt::AlignLeft, title);
        }

        // audio indicator
        if (t.audible || t.muted) {
            QRect ar = audioRect(r);
            if (t.pinned) {
                // pinned tabs show audio at bottom-right corner
                ar.moveRight(r.right() - 4);
                ar.moveBottom(r.bottom() - 3);
            }
            p.drawPixmap(ar, Theme::icon(t.muted ? "mute" : "sound",
                                          t.muted ? danger : muted, 14));
        }

        // close button
        if (!t.pinned && (active || i == m_hoverTab)) {
            const QRect cr = closeButtonRect(r);
            if (i == m_hoverClose) {
                p.setBrush(Theme::paletteColor("border"));
                p.setPen(Qt::NoPen);
                p.drawEllipse(cr);
            }
            p.drawPixmap(cr.adjusted(4, 4, -4, -4),
                         Theme::icon("close", active ? text : muted, 10));
        }

        // separator between inactive tabs
        if (!active && i + 1 < m_tabs.size() && !m_tabs[i + 1].active && i != m_hoverTab) {
            p.setPen(QColor(Theme::isDark() ? QColor(255, 255, 255, 28) : QColor(0, 0, 0, 26)));
            p.drawLine(r.right() + 1, r.top() + 10, r.right() + 1, r.bottom() - 10);
        }
    }

    // + button
    const int lastRight = m_tabs.isEmpty() ? kStripPad : rects.last().right();
    const QRect plusRect(lastRight + 8, (height() - 26) / 2, 26, 26);
    if (plusRect.contains(mapFromGlobal(QCursor::pos()))) {
        p.setBrush(Theme::paletteColor("hover"));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(plusRect, 7, 7);
    }
    p.drawPixmap(plusRect.adjusted(6, 6, -6, -6), Theme::icon("plus", muted, 14));
}

void TabStrip::mousePressEvent(QMouseEvent *e)
{
    QRect r;
    const int idx = tabAt(e->pos(), &r);
    if (e->button() == Qt::LeftButton) {
        if (idx >= 0) {
            m_pressed = true;
            m_pressPos = e->pos();
            m_dragIndex = idx;
            m_dragX = e->pos().x();
            if (!closeButtonRect(r).contains(e->pos()))
                emit tabSelected(m_tabs[idx].id);
        } else {
            // + button hit?
            QList<QRect> rects;
            layoutTabs(&rects);
            const int lastRight = rects.isEmpty() ? kStripPad : rects.last().right();
            const QRect plusRect(lastRight + 8, (height() - 26) / 2, 26, 26);
            if (plusRect.contains(e->pos()))
                emit newTabRequested();
        }
    } else if (e->button() == Qt::MiddleButton && idx >= 0) {
        emit tabCloseRequested(m_tabs[idx].id);
    }
    QWidget::mousePressEvent(e);
}

void TabStrip::mouseMoveEvent(QMouseEvent *e)
{
    const int idx = tabAt(e->pos());
    if (m_hoverClose != -1 || m_hoverTab != idx) {
        m_hoverTab = idx;
        QRect r;
        tabAt(e->pos(), &r);
        m_hoverClose = (r.isValid() && closeButtonRect(r).contains(e->pos())) ? idx : -1;
        update();
    }
    if (m_pressed && m_dragIndex >= 0 && (e->pos() - m_pressPos).manhattanLength() > 8) {
        // live reorder
        QList<QRect> rects;
        layoutTabs(&rects);
        int target = m_dragIndex;
        for (int i = 0; i < rects.size(); ++i) {
            if (e->pos().x() < rects[i].center().x()) {
                target = i;
                break;
            }
            target = i;
        }
        if (target != m_dragIndex && m_tabs[m_dragIndex].pinned == m_tabs[target].pinned) {
            const qint64 id = m_tabs[m_dragIndex].id;
            m_tabs.move(m_dragIndex, target);
            m_dragIndex = target;
            update();
            emit tabReordered(id, target);
        }
    }
    QWidget::mouseMoveEvent(e);
}

void TabStrip::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        QRect r;
        const int idx = tabAt(e->pos(), &r);
        if (idx >= 0 && closeButtonRect(r).contains(e->pos()))
            emit tabCloseRequested(m_tabs[idx].id);
        m_pressed = false;
        m_dragIndex = -1;
    }
    QWidget::mouseReleaseEvent(e);
}

void TabStrip::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (tabAt(e->pos()) < 0)
        emit emptyAreaDoubleClicked();
    QWidget::mouseDoubleClickEvent(e);
}

void TabStrip::leaveEvent(QEvent *e)
{
    m_hoverTab = -1;
    m_hoverClose = -1;
    update();
    QWidget::leaveEvent(e);
}

void TabStrip::contextMenuEvent(QContextMenuEvent *e)
{
    const int idx = tabAt(e->pos());
    if (idx >= 0)
        emit tabContextMenuRequested(m_tabs[idx].id, e->globalPos());
    QWidget::contextMenuEvent(e);
}
