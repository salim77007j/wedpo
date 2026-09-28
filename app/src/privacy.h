// Wedpo browser — privacy core wrapper: Rust engine binding, filter list
// registry, updater, and live block statistics.
#pragma once
#include <QObject>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QMutex>

struct wedpo_engine;

class Privacy : public QObject
{
    Q_OBJECT
public:
    static Privacy *instance();

    struct ListDef {
        QString id;
        QString name;
        QString description;
        QString url;
        bool enabled = true;
    };

    void startup();   // load lists from disk / fetch missing in background
    void shutdown();

    // engine queries (thread-safe)
    int check(const QString &url, const QString &resourceType,
              const QString &firstPartyHost, QString *redirectOut = nullptr) const;
    QString cosmeticCss(const QString &url) const;
    QString genericCss(const QStringList &classes, const QStringList &ids, const QStringList &exceptions) const;
    QString stripTracking(const QString &url) const;
    int downloadRisk(const QString &url, const QString &mime, QStringList *reasons) const;
    QString listsInfo() const;

    // list management
    static const QList<ListDef> &availableLists();
    void reloadLists();          // rebuild engine from enabled lists (from disk cache)
    void updateList(const QString &id);      // fetch latest from network
    void updateAllLists();
    void addCustomList(const QString &name, const QString &url);
    void removeCustomList(const QString &id);
    void importCustomText(const QString &name, const QString &text);
    QStringList listIdsOnDisk() const;

    // statistics
    void countBlocked(const QString &host, const QString &type);
    qint64 blockedTotal() const;
    qint64 blockedToday() const;
    QHash<QString, qint64> blockedByHost(int top = 12) const;
    QHash<QString, qint64> blockedByType() const;
    void resetStats();
    QString currentHost() const;
    void setCurrentHost(const QString &host);
    qint64 blockedForHost(const QString &host) const;

signals:
    void listsUpdated();
    void statsChanged();

private:
    explicit Privacy(QObject *parent = nullptr);
    ~Privacy() override;
    static void loadListsIntoEngine();       // called on worker thread context
    static QString listPath(const QString &id);
    void saveStats();
    void loadStats();

    wedpo_engine *m_engine = nullptr;
    mutable QMutex m_engineMutex;   // engine calls must be serialized
    QString m_currentHost;
    mutable QMutex m_statsMutex;
    QHash<QString, qint64> m_hostCounts;
    QHash<QString, qint64> m_typeCounts;
    qint64 m_total = 0;
    qint64 m_today = 0;
    QString m_statsDay;
};
