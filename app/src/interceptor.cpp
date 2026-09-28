#include "interceptor.h"
#include "privacy.h"
#include "settings.h"

#include <QUrl>
#include <QUrlQuery>
#include <QHostAddress>
#include <QRegularExpression>

RequestInterceptor::RequestInterceptor(QObject *parent)
    : QWebEngineUrlRequestInterceptor(parent)
{
}

static QString resourceTypeName(QWebEngineUrlRequestInfo::ResourceType t)
{
    using R = QWebEngineUrlRequestInfo;
    switch (t) {
    case R::ResourceTypeMainFrame:      return QStringLiteral("document");
    case R::ResourceTypeSubFrame:       return QStringLiteral("sub_frame");
    case R::ResourceTypeStyleSheet:     return QStringLiteral("stylesheet");
    case R::ResourceTypeScript:         return QStringLiteral("script");
    case R::ResourceTypeImage:          return QStringLiteral("image");
    case R::ResourceTypeXhr:            return QStringLiteral("xmlhttprequest");
    case R::ResourceTypePing:           return QStringLiteral("ping");
    case R::ResourceTypeMedia:          return QStringLiteral("media");
    case R::ResourceTypeFontResource:   return QStringLiteral("font");
    default:                            return QStringLiteral("other");
    }
}

bool isLocalOrPrivateHost(const QString &host)
{
    if (host.isEmpty())
        return true;
    if (host == "localhost" || host.endsWith(".local") || host.endsWith(".lan"))
        return true;
    QHostAddress addr;
    if (addr.setAddress(host))
        return true;   // raw IP — never rewrite
    if (host.endsWith(".onion"))
        return true;
    const QRegularExpression rePrivate(
        "^(localhost|127\\.|10\\.|192\\.168\\.|172\\.(1[6-9]|2[0-9]|3[01])\\.|169\\.254\\.|\\[::1\\])");
    return rePrivate.match(host).hasMatch();
}

void RequestInterceptor::interceptRequest(QWebEngineUrlRequestInfo &info)
{
    const QUrl url = info.requestUrl();
    const QString scheme = url.scheme();
    if (scheme == "wedpo" || scheme == "qrc" || scheme == "devtools" || scheme == "chrome-devtools")
        return;

    const QUrl firstParty = info.firstPartyUrl();
    const QUrl initiator = info.initiator();
    const QString fpHost = firstParty.host();
    const QString urlHost = url.host();
    const bool thirdParty = !initiator.isEmpty() && initiator.host() != urlHost;

    // --- privacy headers ------------------------------------------------------
    if (Settings::instance()->sendGpc())
        info.setHttpHeader(QByteArrayLiteral("Sec-GPC"), QByteArrayLiteral("1"));
    if (Settings::instance()->sendDnt())
        info.setHttpHeader(QByteArrayLiteral("DNT"), QByteArrayLiteral("1"));

    // --- cross-site referer trimming (origin only, mirroring strict policies)
    if (thirdParty && info.requestMethod() == "GET"
        && (scheme == "http" || scheme == "https")) {
        info.setHttpHeader(QByteArrayLiteral("Referer"), firstParty.toString(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment).toUtf8());
    }

    // --- HTTPS-only upgrades for top-level navigations
    if (scheme == "http" && info.resourceType() == QWebEngineUrlRequestInfo::ResourceTypeMainFrame
        && Settings::instance()->httpsOnly() && !isLocalOrPrivateHost(urlHost)) {
        QUrl upgraded = url;
        upgraded.setScheme("https");
        info.redirect(upgraded);
        return;
    }

    // --- ad / tracker blocking
    const QString rtype = resourceTypeName(info.resourceType());
    QString redirect;
    const int verdict = Privacy::instance()->check(url.toString(), rtype, fpHost, &redirect);
    if (verdict == 1) {
        info.block(true);
        Privacy::instance()->countBlocked(urlHost, rtype);
        emit requestBlocked(urlHost, rtype);
        return;
    }
    if (verdict == 2 && !redirect.isEmpty()) {
        info.redirect(QUrl(redirect));
        Privacy::instance()->countBlocked(urlHost, rtype);
        emit requestBlocked(urlHost, rtype);
        return;
    }

    // --- tracking parameter stripping on top-level navigations
    if (info.resourceType() == QWebEngineUrlRequestInfo::ResourceTypeMainFrame
        && (scheme == "http" || scheme == "https")) {
        const QString stripped = Privacy::instance()->stripTracking(url.toString());
        if (stripped != url.toString()) {
            info.redirect(QUrl(stripped));
            return;
        }
    }
}
