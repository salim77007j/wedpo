// Wedpo browser — toolbar row under the tab strip: back, forward,
// reload/stop, expanding omnibox, downloads badge, main menu.
// Matches the design: one clean row, no clutter.
#pragma once
#include <QWidget>

class QToolButton;
class Omnibox;

class ToolBar : public QWidget
{
    Q_OBJECT
public:
    explicit ToolBar(QWidget *parent = nullptr);

    Omnibox *omnibox() const { return m_omnibox; }
    void updateNavigationState(bool canBack, bool canForward, bool loading);
    void setDownloadsBadge(int active);
    void setSecure(bool secure, bool certError);
    void setBookmarked(bool bookmarked);
    void setBlockedCount(qint64 n);

signals:
    void backClicked();
    void forwardClicked();
    void reloadClicked();
    void stopClicked();
    void homeClicked();
    void navigateRequested(const QUrl &url);
    void shieldClicked();
    void bookmarkStarClicked();
    void downloadsClicked();
    void menuRequested(const QPoint &globalPos);

private:
    QToolButton *makeButton(const QString &iconName, const QString &tooltip);

    QToolButton *m_back = nullptr;
    QToolButton *m_forward = nullptr;
    QToolButton *m_reloadStop = nullptr;
    QToolButton *m_downloads = nullptr;
    QToolButton *m_menu = nullptr;
    Omnibox *m_omnibox = nullptr;
    bool m_loading = false;
};
