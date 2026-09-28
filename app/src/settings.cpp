#include "settings.h"
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QUrl>

static const char *kOrg = "Wedpo";
static const char *kApp = "Wedpo";

Settings *Settings::instance()
{
    static Settings s;
    return &s;
}

Settings::Settings(QObject *parent)
    : QObject(parent)
{
    QDir base(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    m_dataDir = base.filePath("profile");
    base.mkpath(".");
    QDir(m_dataDir).mkpath(".");
    QDir(m_dataDir).mkpath("lists");
    loadCaches();
}

void Settings::loadCaches()
{
    QSettings st(kOrg, kApp);
    m_zoom.clear();
    const QJsonObject zo = QJsonDocument::fromJson(st.value("pageZooms").toByteArray()).object();
    for (auto it = zo.begin(); it != zo.end(); ++it)
        m_zoom.insert(it.key(), it.value().toDouble(1.0));
    m_perms.clear();
    const QJsonObject po = QJsonDocument::fromJson(st.value("permissions").toByteArray()).object();
    for (auto fit = po.begin(); fit != po.end(); ++fit) {
        QHash<QString, int> m;
        const QJsonObject fo = fit.value().toObject();
        for (auto oit = fo.begin(); oit != fo.end(); ++oit)
            m.insert(oit.key(), oit.value().toInt());
        m_perms.insert(fit.key(), m);
    }
}

QVariant Settings::value(const QString &key, const QVariant &def) const
{
    return QSettings(kOrg, kApp).value(key, def);
}

void Settings::setValue(const QString &key, const QVariant &v)
{
    QSettings(kOrg, kApp).setValue(key, v);
    emit changed(key);
}

// --- general ---------------------------------------------------------------
QString Settings::startupMode() const { return value("general/startup", "newtab").toString(); }
void Settings::setStartupMode(const QString &v) { setValue("general/startup", v); }
QString Settings::homeUrl() const { return value("general/home", "wedpo://newtab").toString(); }
void Settings::setHomeUrl(const QString &v) { setValue("general/home", v); }
QString Settings::searchEngine() const { return value("general/searchEngine", "duckduckgo").toString(); }
void Settings::setSearchEngine(const QString &v) { setValue("general/searchEngine", v); }
QString Settings::customSearchUrl() const { return value("general/customSearch", "").toString(); }
void Settings::setCustomSearchUrl(const QString &v) { setValue("general/customSearch", v); }
bool Settings::searchSuggestions() const { return value("general/suggest", true).toBool(); }
void Settings::setSearchSuggestions(bool v) { setValue("general/suggest", v); }
QString Settings::downloadsDir() const
{
    QString d = value("general/downloadsDir", QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)).toString();
    if (d.isEmpty())
        d = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    return d;
}
void Settings::setDownloadsDir(const QString &v) { setValue("general/downloadsDir", v); }
bool Settings::askWhereToSave() const { return value("general/askSave", false).toBool(); }
void Settings::setAskWhereToSave(bool v) { setValue("general/askSave", v); }

// --- appearance ------------------------------------------------------------
QString Settings::theme() const { return value("appearance/theme", "system").toString(); }
void Settings::setTheme(const QString &v) { setValue("appearance/theme", v); }
int Settings::accentIndex() const { return value("appearance/accent", 0).toInt(); }
void Settings::setAccentIndex(int v) { setValue("appearance/accent", v); }
bool Settings::showBookmarksBar() const { return value("appearance/bookmarksBar", true).toBool(); }
void Settings::setShowBookmarksBar(bool v) { setValue("appearance/bookmarksBar", v); }
int Settings::fontSize() const { return value("appearance/fontSize", 14).toInt(); }
void Settings::setFontSize(int v) { setValue("appearance/fontSize", v); }

// --- privacy & security ------------------------------------------------------
bool Settings::adblockEnabled() const { return value("privacy/adblock", true).toBool(); }
void Settings::setAdblockEnabled(bool v) { setValue("privacy/adblock", v); }
bool Settings::cosmeticFiltering() const { return value("privacy/cosmetic", true).toBool(); }
void Settings::setCosmeticFiltering(bool v) { setValue("privacy/cosmetic", v); }
QStringList Settings::filterLists() const
{
    QStringList def = {QStringLiteral("easylist"), QStringLiteral("easyprivacy")};
    return value("privacy/lists", def).toStringList();
}
void Settings::setFilterLists(const QStringList &v) { setValue("privacy/lists", v); }
bool Settings::stripTrackingParams() const { return value("privacy/stripTracking", true).toBool(); }
void Settings::setStripTrackingParams(bool v) { setValue("privacy/stripTracking", v); }
bool Settings::httpsOnly() const { return value("privacy/httpsOnly", true).toBool(); }
void Settings::setHttpsOnly(bool v) { setValue("privacy/httpsOnly", v); }
bool Settings::sendGpc() const { return value("privacy/gpc", true).toBool(); }
void Settings::setSendGpc(bool v) { setValue("privacy/gpc", v); }
bool Settings::sendDnt() const { return value("privacy/dnt", false).toBool(); }
void Settings::setSendDnt(bool v) { setValue("privacy/dnt", v); }
int Settings::fingerprintMode() const { return value("privacy/fingerprint", 1).toInt(); }
void Settings::setFingerprintMode(int v) { setValue("privacy/fingerprint", v); }
int Settings::webRtcMode() const { return value("privacy/webrtc", 1).toInt(); }
void Settings::setWebRtcMode(int v) { setValue("privacy/webrtc", v); }
bool Settings::blockPopups() const { return value("privacy/blockPopups", true).toBool(); }
void Settings::setBlockPopups(bool v) { setValue("privacy/blockPopups", v); }
bool Settings::autoplayBlocked() const { return value("privacy/noAutoplay", true).toBool(); }
void Settings::setAutoplayBlocked(bool v) { setValue("privacy/noAutoplay", v); }
int Settings::cookieMode() const { return value("privacy/cookies", 1).toInt(); }
void Settings::setCookieMode(int v) { setValue("privacy/cookies", v); }

bool Settings::doNotTrackPerSite(const QString &host) const
{
    return value(QStringLiteral("privacy/allowSites/%1").arg(host), false).toBool();
}
void Settings::setAdblockSiteException(const QString &host, bool allowed)
{
    setValue(QStringLiteral("privacy/allowSites/%1").arg(host), allowed);
}

// --- session -----------------------------------------------------------------
bool Settings::restoreOnCrash() const { return value("session/restoreOnCrash", true).toBool(); }
void Settings::setRestoreOnCrash(bool v) { setValue("session/restoreOnCrash", v); }

// --- zoom --------------------------------------------------------------------
double Settings::zoomForHost(const QString &host) const
{
    return m_zoom.value(host, 1.0);
}
void Settings::setZoomForHost(const QString &host, double factor)
{
    m_zoom.insert(host, factor);
    QJsonObject o;
    for (auto it = m_zoom.begin(); it != m_zoom.end(); ++it)
        o.insert(it.key(), it.value());
    QSettings(kOrg, kApp).setValue("pageZooms", QJsonDocument(o).toJson(QJsonDocument::Compact));
    emit changed(QStringLiteral("zoom/%1").arg(host));
}

// --- permissions --------------------------------------------------------------
int Settings::permission(const QString &origin, const QString &feature) const
{
    return m_perms.value(feature).value(origin, 0);
}
void Settings::setPermission(const QString &origin, const QString &feature, int decision)
{
    m_perms[feature][origin] = decision;
    QJsonObject po;
    for (auto fit = m_perms.begin(); fit != m_perms.end(); ++fit) {
        QJsonObject fo;
        for (auto oit = fit.value().begin(); oit != fit.value().end(); ++oit)
            fo.insert(oit.key(), oit.value());
        po.insert(fit.key(), fo);
    }
    QSettings(kOrg, kApp).setValue("permissions", QJsonDocument(po).toJson(QJsonDocument::Compact));
    emit changed(QStringLiteral("perm/%1/%2").arg(feature, origin));
}
QHash<QString, int> Settings::allPermissions(const QString &feature) const
{
    return m_perms.value(feature);
}
void Settings::clearPermissions()
{
    m_perms.clear();
    QSettings(kOrg, kApp).remove("permissions");
    emit changed("permissions/cleared");
}

// --- search -------------------------------------------------------------------
struct SearchEngineDef { const char *id; const char *name; const char *url; };
static const SearchEngineDef kEngines[] = {
    {"duckduckgo", "DuckDuckGo", "https://duckduckgo.com/?q=%s"},
    {"google", "Google", "https://www.google.com/search?q=%s"},
    {"bing", "Bing", "https://www.bing.com/search?q=%s"},
    {"brave", "Brave Search", "https://search.brave.com/search?q=%s"},
    {"startpage", "Startpage", "https://www.startpage.com/sp/search?query=%s"},
};

QUrl Settings::searchUrlFor(const QString &query) const
{
    QString tpl;
    if (searchEngine() == "custom") {
        tpl = customSearchUrl();
        if (!tpl.contains("%s"))
            tpl = QString();
    }
    if (tpl.isEmpty()) {
        for (const auto &e : kEngines)
            if (searchEngine() == e.id) { tpl = QString::fromLatin1(e.url); break; }
    }
    if (tpl.isEmpty())
        tpl = QString::fromLatin1(kEngines[0].url);
    return QUrl(tpl.arg(QString::fromUtf8(QUrl::toPercentEncoding(query))));
}

QString Settings::searchName(const QString &id)
{
    for (const auto &e : kEngines)
        if (id == e.id)
            return QString::fromLatin1(e.name);
    return id;
}
