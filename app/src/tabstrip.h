// Wedpo browser — custom tab strip painted to match the product design:
// rounded active tabs on a subtle strip, favicons, loading spinner, audio
// indicator, pinning, drag reorder, overflow shrinking, + button.
#pragma once
#include <QWidget>
#include <QTimer>
#include <QList>

struct TabInfo {
    qint64 id = 0;
    QString title;
    QPixmap icon;
    bool pinned = false;
    bool muted = false;
    bool audible = false;
    bool loading = false;
    bool active = false;
};

class TabStrip : public QWidget
{
    Q_OBJECT
public:
    explicit TabStrip(QWidget *parent = nullptr);

    void setTabs(const QList<TabInfo> &tabs);
    bool pointInInteractiveArea(const QPoint &localPos) const;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void tabSelected(qint64 id);
    void tabCloseRequested(qint64 id);
    void newTabRequested();
    void tabContextMenuRequested(qint64 id, const QPoint &globalPos);
    void tabReordered(qint64 id, int newIndex);
    void audioToggleRequested(qint64 id);
    void emptyAreaDoubleClicked();

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;

private:
    int tabAt(const QPoint &pos, QRect *rect = nullptr) const;
    QRect closeButtonRect(const QRect &tabRect) const;
    QRect audioRect(const QRect &tabRect) const;
    void layoutTabs(QList<QRect> *out) const;

    QList<TabInfo> m_tabs;
    int m_hoverTab = -1;
    int m_hoverClose = -1;
    int m_dragIndex = -1;
    int m_dragX = 0;
    bool m_pressed = false;
    QPoint m_pressPos;
    qreal m_spinnerAngle = 0;
    QTimer m_spinnerTimer;
};
