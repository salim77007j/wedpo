// Wedpo browser — typed settings store over QSettings.
#pragma once
#include <QObject>
#include <QVariant>
#include <QHash>
#include <QStringList>

class Settings : public QObject
{
    Q_OBJECT
public:
    static Settings *instance();

    // ---- general
    QString startupMode() const;                 // "newtab" | "continue" | "url"
    void setStartupMode(const QString &v);
    QString homeUrl() const;                     // "wedpo://newtab" or custom
    void setHomeUrl(const QString &v);
    QString searchEngine() const;                // "duckduckgo" | "google" | "bing" | "brave" | "custom"
    void setSearchEngine(const QString &v);
    QString customSearchUrl() const;             // %s placeholder
    void setCustomSearchUrl(const QString &v);
    bool searchSuggestions() const;
    void setSearchSuggestions(bool v);
    QString downloadsDir() const;
    void setDownloadsDir(const QString &v);
    bool askWhereToSave() const;
    void setAskWhereToSave(bool v);

    // ---- appearance
    QString theme() const;                       // "system" | "light" | "dark"
    void setTheme(const QString &v);
    int accentIndex() const;
    void setAccentIndex(int v);
    bool showBookmarksBar() const;
    void setShowBookmarksBar(bool v);
    int fontSize() const;                        // omnibox/NTP base, not page zoom
    void setFontSize(int v);

    // ---- privacy & security
    bool adblockEnabled() const;
    void setAdblockEnabled(bool v);
    bool cosmeticFiltering() const;
    void setCosmeticFiltering(bool v);
    QStringList filterLists() const;             // enabled list ids
    void setFilterLists(const QStringList &v);
    bool stripTrackingParams() const;
    void setStripTrackingParams(bool v);
    bool httpsOnly() const;
    void setHttpsOnly(bool v);
    bool sendGpc() const;
    void setSendGpc(bool v);
    bool sendDnt() const;
    void setSendDnt(bool v);
    int fingerprintMode() const;                 // 0 off, 1 standard, 2 strict
    void setFingerprintMode(int v);
    int webRtcMode() const;                      // 0 default, 1 no public ip, 2 disable
    void setWebRtcMode(int v);
    bool blockPopups() const;
    void setBlockPopups(bool v);
    bool autoplayBlocked() const;
    void setAutoplayBlocked(bool v);
    int cookieMode() const;                      // 0 allow all, 1 block 3rd-party, 2 block all
    void setCookieMode(int v);
    bool doNotTrackPerSite(const QString &host) const;   // site exceptions for adblock (allowlist)
    void setAdblockSiteException(const QString &host, bool allowed);

    // ---- session & windows
    bool restoreOnCrash() const;
    void setRestoreOnCrash(bool v);

    // generic access for pages
    QVariant value(const QString &key, const QVariant &def = QVariant()) const;
    void setValue(const QString &key, const QVariant &v);

    // per-host page zoom
    double zoomForHost(const QString &host) const;
    void setZoomForHost(const QString &host, double factor);

    // permission decisions per origin+feature: 0 ask, 1 allow, 2 block
    int permission(const QString &origin, const QString &feature) const;
    void setPermission(const QString &origin, const QString &feature, int decision);
    QHash<QString, int> allPermissions(const QString &feature) const;
    void clearPermissions();

    QString dataDir() const { return m_dataDir; }

    // search engine url from id
    QUrl searchUrlFor(const QString &query) const;
    static QString searchName(const QString &id);

signals:
    void changed(const QString &key);

private:
    explicit Settings(QObject *parent = nullptr);
    QString m_dataDir;
    QHash<QString, double> m_zoom;               // cached zoom map
    QHash<QString, QHash<QString, int>> m_perms; // feature -> origin -> decision
    void loadCaches();
};
