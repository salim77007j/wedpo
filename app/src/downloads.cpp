#include "downloads.h"
#include "settings.h"
#include "privacy.h"

#include <QWebEngineDownloadRequest>
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QDebug>

static const int kRiskWarnThreshold = 60;

Downloads *Downloads::instance()
{
    static Downloads s;
    return &s;
}

Downloads::Downloads(QObject *parent)
    : QObject(parent)
{
}

int Downloads::activeCount() const
{
    int n = 0;
    for (const DownloadItem &it : m_items)
        if (it.state == 1)
            ++n;
    return n;
}

void Downloads::attach(QWebEngineDownloadRequest *req)
{
    if (!req)
        return;
    DownloadItem it;
    it.id = m_nextId++;
    it.url = req->url().toString();
    it.fileName = req->suggestedFileName();
    it.mimeType = req->mimeType();
    it.total = req->totalBytes();
    it.handle = req;
    it.state = 0;   // awaiting confirmation
    it.path = req->downloadDirectory() + "/" + req->downloadFileName();

    // Risk scoring before anything is written
    const QString proposed = Settings::instance()->downloadsDir() + "/" + it.fileName;
    it.riskScore = Privacy::instance()->downloadRisk(it.url, it.mimeType, &it.riskReasons);

    const int id = it.id;
    m_items.append(it);
    emit itemAdded(id);
    emit activeCountChanged();

    if (it.riskScore >= kRiskWarnThreshold) {
        it.state = 5;   // risk-blocked awaiting user decision
        for (int i = 0; i < m_items.size(); ++i)
            if (m_items[i].id == quint32(id))
                m_items[i].state = 5;
        emit itemChanged(id);
        emit riskWarning(id, it.riskScore, it.riskReasons);
        return;
    }
    acceptDownload(id);
}

void Downloads::setTargetPath(quint32 id, const QString &dir, const QString &fileName)
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].id != id)
            continue;
        if (!fileName.isEmpty())
            m_items[i].fileName = fileName;
        m_items[i].path = dir + "/" + m_items[i].fileName;
        if (m_items[i].handle) {
            m_items[i].handle->setDownloadDirectory(dir);
            if (!fileName.isEmpty())
                m_items[i].handle->setDownloadFileName(fileName);
        }
        emit itemChanged(id);
        return;
    }
}

void Downloads::acceptDownload(quint32 id)
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].id != id || m_items[i].handle.isNull())
            continue;
        QWebEngineDownloadRequest *req = m_items[i].handle.data();
        if (m_items[i].path.isEmpty()) {
            const QString dir = Settings::instance()->askWhereToSave()
                                    ? QString()   // caller should have set path
                                    : Settings::instance()->downloadsDir();
            if (!dir.isEmpty()) {
                req->setDownloadDirectory(dir);
                m_items[i].path = dir + "/" + m_items[i].fileName;
            }
        } else {
            req->setDownloadDirectory(QFileInfo(m_items[i].path).absolutePath());
            req->setDownloadFileName(QFileInfo(m_items[i].path).fileName());
        }
        connect(req, &QWebEngineDownloadRequest::receivedBytesChanged, this, [this, id]() { updateItem(id); });
        connect(req, &QWebEngineDownloadRequest::totalBytesChanged, this, [this, id]() { updateItem(id); });
        connect(req, &QWebEngineDownloadRequest::stateChanged, this, [this, id, req](QWebEngineDownloadRequest::DownloadState st) {
            for (int j = 0; j < m_items.size(); ++j) {
                if (m_items[j].id != id)
                    continue;
                switch (st) {
                case QWebEngineDownloadRequest::DownloadRequested: m_items[j].state = 0; break;
                case QWebEngineDownloadRequest::DownloadInProgress: m_items[j].state = 1; break;
                case QWebEngineDownloadRequest::DownloadCompleted: m_items[j].state = 2; break;
                case QWebEngineDownloadRequest::DownloadCancelled: m_items[j].state = 3; break;
                case QWebEngineDownloadRequest::DownloadInterrupted: m_items[j].state = 4; break;
                }
                m_items[j].received = req->receivedBytes();
                m_items[j].total = req->totalBytes();
            }
            emit itemChanged(id);
            if (st == QWebEngineDownloadRequest::DownloadCompleted) {
                emit downloadFinished(id, true);
                emit activeCountChanged();
            } else if (st == QWebEngineDownloadRequest::DownloadInterrupted
                       || st == QWebEngineDownloadRequest::DownloadCancelled) {
                emit downloadFinished(id, false);
                emit activeCountChanged();
            }
        });
        req->accept();
        m_items[i].state = 1;
        emit itemChanged(id);
        emit activeCountChanged();
        return;
    }
}

void Downloads::rejectDownload(quint32 id)
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].id != id)
            continue;
        if (m_items[i].handle && m_items[i].handle->state() == QWebEngineDownloadRequest::DownloadRequested)
            m_items[i].handle->cancel();
        m_items[i].state = 5;
        emit itemChanged(id);
        return;
    }
}

void Downloads::updateItem(quint32 id)
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].id != id || m_items[i].handle.isNull())
            continue;
        m_items[i].received = m_items[i].handle->receivedBytes();
        m_items[i].total = m_items[i].handle->totalBytes();
        emit itemChanged(id);
        return;
    }
}

void Downloads::pause(quint32 id)
{
    for (DownloadItem &it : m_items)
        if (it.id == id && it.handle && it.state == 1)
            it.handle->pause();
}

void Downloads::resume(quint32 id)
{
    for (DownloadItem &it : m_items)
        if (it.id == id && it.handle && it.state == 1)
            it.handle->resume();
}

void Downloads::cancel(quint32 id)
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].id == id && m_items[i].handle) {
            if (m_items[i].state == 0 || m_items[i].state == 5)
                m_items[i].handle->cancel();
            else
                m_items[i].handle->cancel();
            return;
        }
    }
}

void Downloads::remove(quint32 id)
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].id == id) {
            if (m_items[i].handle && m_items[i].state == 1)
                m_items[i].handle->cancel();
            m_items.removeAt(i);
            emit activeCountChanged();
            return;
        }
    }
}

void Downloads::clearFinished()
{
    for (int i = m_items.size() - 1; i >= 0; --i)
        if (m_items[i].state == 2 || m_items[i].state == 3 || m_items[i].state == 4 || m_items[i].state == 5)
            m_items.removeAt(i);
    emit activeCountChanged();
}
