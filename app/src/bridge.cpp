#include "bridge.h"
#include "settings.h"
#include "stores.h"
#include "privacy.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QDateTime>

NtpBridge::NtpBridge(QObject *parent)
    : QObject(parent)
{
    connect(Privacy::instance(), &Privacy::statsChanged, this, [this]() {
        // NTP polls stats() on demand; nothing to push.
    });
}

QString NtpBridge::greeting() const
{
    const QTime now = QTime::currentTime();
    if (now.hour() < 5)
        return tr("Good night!");
    if (now.hour() < 12)
        return tr("Good morning!");
    if (now.hour() < 18)
        return tr("Good afternoon!");
    return tr("Good evening!");
}

QString NtpBridge::tagline() const
{
    return tr("Simple. Fast. Yours.");
}

QJsonArray NtpBridge::shortcuts() const
{
    const QJsonDocument doc = QJsonDocument::fromJson(
        Settings::instance()->value("ntp/shortcuts", QByteArray()).toByteArray());
    if (doc.isArray())
        return doc.array();
    // defaults shown in the design, editable by the user
    QJsonArray def;
    const QVector<QPair<QString, QString>> defaults = {
        {QStringLiteral("Wikipedia"), QStringLiteral("https://www.wikipedia.org")},
        {QStringLiteral("GitHub"), QStringLiteral("https://github.com")},
        {QStringLiteral("YouTube"), QStringLiteral("https://www.youtube.com")},
        {QStringLiteral("News"), QStringLiteral("https://news.ycombinator.com")},
        {QStringLiteral("Mail"), QStringLiteral("https://mail.proton.me")},
    };
    for (const auto &d : defaults) {
        QJsonObject o;
        o.insert(QStringLiteral("title"), d.first);
        o.insert(QStringLiteral("url"), d.second);
        def.append(o);
    }
    return def;
}

QJsonArray NtpBridge::topSites(int limit) const
{
    QJsonArray out;
    for (const HistoryEntry &h : Stores::instance()->topSites(limit)) {
        QJsonObject o;
        o.insert(QStringLiteral("title"), h.title.isEmpty() ? h.host : h.title);
        o.insert(QStringLiteral("url"), h.url);
        out.append(o);
    }
    return out;
}

void NtpBridge::addShortcut(const QString &title, const QString &url)
{
    if (title.isEmpty() || url.isEmpty())
        return;
    QJsonArray arr = shortcuts();
    QJsonObject o;
    o.insert(QStringLiteral("title"), title);
    o.insert(QStringLiteral("url"), url);
    arr.append(o);
    Settings::instance()->setValue("ntp/shortcuts",
                                   QJsonDocument(arr).toJson(QJsonDocument::Compact));
    emit shortcutsChanged();
}

void NtpBridge::removeShortcut(int index)
{
    QJsonArray arr = shortcuts();
    if (index < 0 || index >= arr.size())
        return;
    arr.removeAt(index);
    Settings::instance()->setValue("ntp/shortcuts",
                                   QJsonDocument(arr).toJson(QJsonDocument::Compact));
    emit shortcutsChanged();
}

void NtpBridge::search(const QString &query)
{
    if (query.trimmed().isEmpty())
        return;
    emit navigateRequested(Settings::instance()->searchUrlFor(query.trimmed()));
}

void NtpBridge::openUrl(const QString &url)
{
    const QUrl u(url);
    if (u.isValid())
        emit navigateRequested(u);
}

QJsonObject NtpBridge::weather() const
{
    return m_weather;
}

void NtpBridge::requestWeather()
{
    const QString city = Settings::instance()->value("ntp/weatherCity", QString()).toString();
    if (city.isEmpty()) {
        m_weather = QJsonObject{{QStringLiteral("ok"), false},
                                {QStringLiteral("reason"), QStringLiteral("nolocation")}};
        emit weatherChanged();
        return;
    }
    fetchWeather();
}

void NtpBridge::fetchWeather()
{
    const QString city = Settings::instance()->value("ntp/weatherCity", QString()).toString();
    if (city.isEmpty())
        return;
    auto *nam = new QNetworkAccessManager(this);
    // geocode city -> lat/lon (open-meteo geocoding, no account, no tracking)
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("name"), city);
    q.addQueryItem(QStringLiteral("count"), QStringLiteral("1"));
    QUrl url(QStringLiteral("https://geocoding-api.open-meteo.com/v1/search"));
    url.setQuery(q);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Wedpo/1.0 (weather)"));
    QNetworkReply *reply = nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam]() {
        reply->deleteLater();
        nam->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;
        const QJsonObject results = QJsonDocument::fromJson(reply->readAll()).object();
        const QJsonArray arr = results.value(QStringLiteral("results")).toArray();
        if (arr.isEmpty()) {
            m_weather = QJsonObject{{QStringLiteral("ok"), false},
                                    {QStringLiteral("reason"), QStringLiteral("notfound")}};
            emit weatherChanged();
            return;
        }
        const QJsonObject place = arr.first().toObject();
        const double lat = place.value(QStringLiteral("latitude")).toDouble();
        const double lon = place.value(QStringLiteral("longitude")).toDouble();
        auto *nam2 = new QNetworkAccessManager(this);
        QUrlQuery q2;
        q2.addQueryItem(QStringLiteral("latitude"), QString::number(lat));
        q2.addQueryItem(QStringLiteral("longitude"), QString::number(lon));
        q2.addQueryItem(QStringLiteral("current"), QStringLiteral("temperature_2m,weather_code"));
        q2.addQueryItem(QStringLiteral("daily"), QStringLiteral("temperature_2m_max,temperature_2m_min,weather_code"));
        q2.addQueryItem(QStringLiteral("timezone"), QStringLiteral("auto"));
        QUrl u2(QStringLiteral("https://api.open-meteo.com/v1/forecast"));
        u2.setQuery(q2);
        QNetworkRequest req2(u2);
        req2.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Wedpo/1.0 (weather)"));
        QNetworkReply *reply2 = nam2->get(req2);
        connect(reply2, &QNetworkReply::finished, this, [this, reply2, nam2, place]() {
            reply2->deleteLater();
            nam2->deleteLater();
            if (reply2->error() != QNetworkReply::NoError)
                return;
            const QJsonObject data = QJsonDocument::fromJson(reply2->readAll()).object();
            QJsonObject out;
            out.insert(QStringLiteral("ok"), true);
            out.insert(QStringLiteral("city"), place.value(QStringLiteral("name")).toString());
            out.insert(QStringLiteral("temp"), data.value(QStringLiteral("current"))
                                                  .toObject().value(QStringLiteral("temperature_2m")));
            out.insert(QStringLiteral("code"), data.value(QStringLiteral("current"))
                                                  .toObject().value(QStringLiteral("weather_code")));
            m_weather = out;
            emit weatherChanged();
        });
    });
}

QJsonObject NtpBridge::stats() const
{
    QJsonObject o;
    o.insert(QStringLiteral("today"), double(Privacy::instance()->blockedToday()));
    o.insert(QStringLiteral("total"), double(Privacy::instance()->blockedTotal()));
    return o;
}

void NtpBridge::openSettings()
{
    emit openSettingsRequested();
}
