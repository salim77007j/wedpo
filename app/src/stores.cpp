#include "stores.h"
#include "settings.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QUrl>
#include <QDebug>
#include <QDateTime>

Stores *Stores::instance()
{
    static Stores s;
    return &s;
}

Stores::Stores(QObject *parent)
    : QObject(parent)
{
    const QString path = Settings::instance()->dataDir() + QStringLiteral("/store.db");
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"));
    m_db.setDatabaseName(path);
    if (!m_db.open()) {
        qWarning() << "store.db open failed:" << m_db.lastError().text();
        return;
    }
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    q.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    q.exec("CREATE TABLE IF NOT EXISTS bookmarks ("
           "id INTEGER PRIMARY KEY, title TEXT NOT NULL, url TEXT UNIQUE NOT NULL, "
           "folder TEXT NOT NULL DEFAULT 'bar', added INTEGER NOT NULL)");
    q.exec("CREATE TABLE IF NOT EXISTS history ("
           "url TEXT PRIMARY KEY, title TEXT NOT NULL DEFAULT '', host TEXT NOT NULL DEFAULT '', "
           "last_visit INTEGER NOT NULL, visits INTEGER NOT NULL DEFAULT 1)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_history_last ON history(last_visit)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_history_host ON history(host)");
}

// ---- bookmarks -------------------------------------------------------------
qint64 Stores::addBookmark(const QString &title, const QString &url, const QString &folder)
{
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO bookmarks(title,url,folder,added) VALUES(?,?,?,?) "
              "ON CONFLICT(url) DO UPDATE SET title=excluded.title, folder=excluded.folder");
    q.addBindValue(title.isEmpty() ? url : title);
    q.addBindValue(url);
    q.addBindValue(folder);
    q.addBindValue(QDateTime::currentSecsSinceEpoch());
    if (!q.exec()) {
        qWarning() << "bookmark add failed:" << q.lastError().text();
        return 0;
    }
    emit bookmarksChanged();
    return q.lastInsertId().toLongLong();
}

void Stores::removeBookmark(qint64 id)
{
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM bookmarks WHERE id=?");
    q.addBindValue(id);
    q.exec();
    emit bookmarksChanged();
}

void Stores::updateBookmark(qint64 id, const QString &title, const QString &url, const QString &folder)
{
    QSqlQuery q(m_db);
    q.prepare("UPDATE bookmarks SET title=?, url=?, folder=? WHERE id=?");
    q.addBindValue(title);
    q.addBindValue(url);
    q.addBindValue(folder);
    q.addBindValue(id);
    q.exec();
    emit bookmarksChanged();
}

bool Stores::isBookmarked(const QString &url) const
{
    QSqlQuery q(m_db);
    q.prepare("SELECT 1 FROM bookmarks WHERE url=?");
    q.addBindValue(url);
    return q.exec() && q.next();
}

Bookmark Stores::bookmarkByUrl(const QString &url) const
{
    QSqlQuery q(m_db);
    q.prepare("SELECT id,title,url,folder,added FROM bookmarks WHERE url=?");
    q.addBindValue(url);
    if (q.exec() && q.next())
        return Bookmark{q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toString(),
                        q.value(3).toString(), q.value(4).toLongLong()};
    return {};
}

static Bookmark readBookmarkRow(const QSqlQuery &q)
{
    return Bookmark{q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toString(),
                    q.value(3).toString(), q.value(4).toLongLong()};
}

QList<Bookmark> Stores::bookmarks(const QString &folder) const
{
    QList<Bookmark> out;
    QSqlQuery q(m_db);
    if (folder.isEmpty()) {
        q.exec("SELECT id,title,url,folder,added FROM bookmarks ORDER BY added DESC");
    } else {
        q.prepare("SELECT id,title,url,folder,added FROM bookmarks WHERE folder=? ORDER BY added DESC");
        q.addBindValue(folder);
        q.exec();
    }
    while (q.next())
        out << readBookmarkRow(q);
    return out;
}

QList<Bookmark> Stores::searchBookmarks(const QString &query, int limit) const
{
    QList<Bookmark> out;
    QSqlQuery q(m_db);
    q.prepare("SELECT id,title,url,folder,added FROM bookmarks "
              "WHERE title LIKE ? OR url LIKE ? ORDER BY added DESC LIMIT ?");
    const QString like = "%" + query + "%";
    q.addBindValue(like);
    q.addBindValue(like);
    q.addBindValue(limit);
    q.exec();
    while (q.next())
        out << readBookmarkRow(q);
    return out;
}

// ---- history ----------------------------------------------------------------
void Stores::recordVisit(const QString &url, const QString &title)
{
    const QUrl u(url);
    if (u.scheme() != "http" && u.scheme() != "https")
        return;
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO history(url,title,host,last_visit,visits) VALUES(?,?,?,?,1) "
              "ON CONFLICT(url) DO UPDATE SET title=excluded.title, last_visit=excluded.last_visit, "
              "visits=visits+1");
    q.addBindValue(url);
    q.addBindValue(title);
    q.addBindValue(u.host());
    q.addBindValue(QDateTime::currentSecsSinceEpoch());
    if (!q.exec())
        qWarning() << "history insert failed:" << q.lastError().text();
    emit historyChanged();
}

static HistoryEntry readHistoryRow(const QSqlQuery &q)
{
    return HistoryEntry{q.value(0).toString(), q.value(1).toString(),
                        q.value(2).toString(), q.value(3).toLongLong(), q.value(4).toInt()};
}

QList<HistoryEntry> Stores::recentHistory(int limit) const
{
    QList<HistoryEntry> out;
    QSqlQuery q(m_db);
    q.prepare("SELECT url,title,host,last_visit,visits FROM history ORDER BY last_visit DESC LIMIT ?");
    q.addBindValue(limit);
    q.exec();
    while (q.next())
        out << readHistoryRow(q);
    return out;
}

QList<HistoryEntry> Stores::searchHistory(const QString &query, int limit) const
{
    QList<HistoryEntry> out;
    QSqlQuery q(m_db);
    q.prepare("SELECT url,title,host,last_visit,visits FROM history "
              "WHERE title LIKE ? OR url LIKE ? OR host LIKE ? ORDER BY last_visit DESC LIMIT ?");
    const QString like = "%" + query + "%";
    q.addBindValue(like);
    q.addBindValue(like);
    q.addBindValue(like);
    q.addBindValue(limit);
    q.exec();
    while (q.next())
        out << readHistoryRow(q);
    return out;
}

void Stores::deleteHistoryEntry(const QString &url)
{
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM history WHERE url=?");
    q.addBindValue(url);
    q.exec();
    emit historyChanged();
}

void Stores::clearHistory(qint64 sinceEpoch)
{
    QSqlQuery q(m_db);
    if (sinceEpoch <= 0) {
        q.exec("DELETE FROM history");
    } else {
        q.prepare("DELETE FROM history WHERE last_visit >= ?");
        q.addBindValue(sinceEpoch);
        q.exec();
    }
    emit historyChanged();
}

QList<HistoryEntry> Stores::topSites(int limit) const
{
    QList<HistoryEntry> out;
    QSqlQuery q(m_db);
    q.prepare("SELECT url,title,host,last_visit,visits FROM history "
              "WHERE host != '' ORDER BY visits DESC, last_visit DESC LIMIT ?");
    q.addBindValue(limit);
    q.exec();
    while (q.next())
        out << readHistoryRow(q);
    return out;
}

// ---- session ------------------------------------------------------------------
bool Stores::loadSession(QJsonArray *windows, bool *clean) const
{
    if (clean)
        *clean = false;
    if (windows)
        windows->clear();
    QFile f(Settings::instance()->dataDir() + QStringLiteral("/session.json"));
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    const QJsonObject o = doc.object();
    if (clean)
        *clean = o.value(QStringLiteral("clean")).toBool(false);
    if (windows)
        *windows = o.value(QStringLiteral("windows")).toArray();
    return true;
}

void Stores::saveSession(const QJsonArray &windows, bool clean)
{
    QJsonObject o;
    o.insert(QStringLiteral("clean"), clean);
    o.insert(QStringLiteral("windows"), windows);
    o.insert(QStringLiteral("saved"), QDateTime::currentSecsSinceEpoch());
    const QString path = Settings::instance()->dataDir() + QStringLiteral("/session.json");
    const QString tmp = path + QStringLiteral(".tmp");
    QFile f(tmp);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
        f.close();
        QFile::remove(path);
        QFile::rename(tmp, path);
    }
}
