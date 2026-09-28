// Wedpo browser — download manager with Rust-core risk gating.
#pragma once
#include <QObject>
#include <QList>
#include <QPointer>

class QWebEngineDownloadRequest;
class Settings;

struct DownloadItem {
    quint32 id = 0;
    QString url;
    QString path;          // target file path
    QString fileName;
    QString mimeType;
    qint64 received = 0;
    qint64 total = 0;      // -1 unknown
    int state = 0;         // 0 requested, 1 running, 2 completed, 3 cancelled, 4 failed, 5 risk-blocked
    int riskScore = 0;
    QStringList riskReasons;
    QPointer<QWebEngineDownloadRequest> handle;
};

class Downloads : public QObject
{
    Q_OBJECT
public:
    static Downloads *instance();

    void attach(QWebEngineDownloadRequest *req);   // from profile downloadRequested
    const QList<DownloadItem> &items() const { return m_items; }
    int activeCount() const;
    void pause(quint32 id);
    void resume(quint32 id);
    void cancel(quint32 id);
    void remove(quint32 id);       // forget entry
    void clearFinished();
    void setTargetPath(quint32 id, const QString &dir, const QString &fileName);
    void acceptDownload(quint32 id);    // confirm after risk dialog
    void rejectDownload(quint32 id);

signals:
    void itemAdded(quint32 id);
    void itemChanged(quint32 id);
    void downloadFinished(quint32 id, bool ok);
    void riskWarning(quint32 id, int score, const QStringList &reasons);
    void activeCountChanged();

private:
    explicit Downloads(QObject *parent = nullptr);
    void updateItem(quint32 id);

    QList<DownloadItem> m_items;
    quint32 m_nextId = 1;
};
