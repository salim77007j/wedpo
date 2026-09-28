// Wedpo browser — network request interceptor: adblock, tracker stripping,
// HTTPS upgrades, privacy headers (GPC/DNT), Referer trimming.
#pragma once
#include <QWebEngineUrlRequestInterceptor>

class RequestInterceptor : public QWebEngineUrlRequestInterceptor
{
    Q_OBJECT
public:
    explicit RequestInterceptor(QObject *parent = nullptr);
    void interceptRequest(QWebEngineUrlRequestInfo &info) override;

signals:
    void requestBlocked(const QString &host, const QString &type) const;
};

bool isLocalOrPrivateHost(const QString &host);
