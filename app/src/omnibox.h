// Wedpo browser — omnibox: URL/search entry, security indicator, privacy
// shield chip with live blocked count, bookmark star, completer over
// history/bookmarks plus DuckDuckGo network suggestions.
#pragma once
#include <QLineEdit>
#include <QPointer>

class QCompleter;
class QStandardItemModel;
class QNetworkAccessManager;
class QNetworkReply;

class Omnibox : public QLineEdit
{
    Q_OBJECT
public:
    explicit Omnibox(QWidget *parent = nullptr);

    void setUrl(const QUrl &url, bool showFull = false);
    QUrl url() const { return m_currentUrl; }
    void setSecure(bool secure, bool certError = false);
    void setBookmarked(bool bookmarked);
    void setBlockedCount(qint64 n);
    QAction *starAction() const { return m_starAction; }
    QAction *shieldAction() const { return m_shieldAction; }
    void selectAllAndFocus();

signals:
    void navigateRequested(const QUrl &url);
    void shieldClicked();

protected:
    void focusInEvent(QFocusEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private slots:
    void onReturnPressed();
    void refreshSuggestions();
    void onSuggestionsFinished();
    void onCompleterActivated(const QModelIndex &index);

private:
    void rebuildCompleter();
    void pushSuggestion(const QString &text, const QString &subtext, const QUrl &url, const QString &icon);

    QAction *m_lockAction = nullptr;
    QAction *m_shieldAction = nullptr;
    QAction *m_starAction = nullptr;
    QCompleter *m_completer = nullptr;
    QStandardItemModel *m_model = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
    QPointer<QNetworkReply> m_pendingReply;
    QUrl m_currentUrl;
    bool m_completing = false;
};

// Shared URL parser: omnibox text -> navigable URL (search fallback included).
QUrl wedpoParseToUrl(const QString &raw);
