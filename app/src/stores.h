// Wedpo browser — persistent stores: bookmarks, history (SQLite), session JSON.
#pragma once
#include <QObject>
#include <QSqlDatabase>
#include <QList>
#include <QJsonArray>

struct Bookmark {
    qint64 id = 0;
    QString title;
    QString url;
    QString folder;      // "bar" | "other" | custom
    qint64 added = 0;
};

struct HistoryEntry {
    QString url;
    QString title;
    QString host;
    qint64 lastVisit = 0;
    int visits = 0;
};

class Stores : public QObject
{
    Q_OBJECT
public:
    static Stores *instance();

    // ---- bookmarks
    qint64 addBookmark(const QString &title, const QString &url, const QString &folder = "bar");
    void removeBookmark(qint64 id);
    void updateBookmark(qint64 id, const QString &title, const QString &url, const QString &folder);
    bool isBookmarked(const QString &url) const;
    Bookmark bookmarkByUrl(const QString &url) const;
    QList<Bookmark> bookmarks(const QString &folder = QString()) const;
    QList<Bookmark> searchBookmarks(const QString &query, int limit = 50) const;

    // ---- history
    void recordVisit(const QString &url, const QString &title);
    QList<HistoryEntry> recentHistory(int limit = 500) const;
    QList<HistoryEntry> searchHistory(const QString &query, int limit = 200) const;
    void deleteHistoryEntry(const QString &url);
    void clearHistory(qint64 sinceEpoch = 0);      // 0 = everything
    QList<HistoryEntry> topSites(int limit = 9) const;

    // ---- session
    bool loadSession(QJsonArray *windows, bool *clean) const;
    void saveSession(const QJsonArray &windows, bool clean);

signals:
    void bookmarksChanged();
    void historyChanged();

private:
    explicit Stores(QObject *parent = nullptr);
    QSqlDatabase m_db;
};
