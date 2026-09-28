#include "privacy.h"
#include "settings.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QDateTime>
#include <QSettings>
#include <QCryptographicHash>
#include <QTimer>
#include <algorithm>

extern "C" {
typedef struct wedpo_engine wedpo_engine;
wedpo_engine *wedpo_engine_new(void);
void wedpo_engine_free(wedpo_engine *);
int wedpo_engine_load_list(wedpo_engine *, const char *name, const char *text);
int wedpo_engine_remove_list(wedpo_engine *, const char *name);
int wedpo_engine_check(wedpo_engine *, const char *url, const char *rtype,
                       const char *first_party_host, char *redirect_out, int cap);
int wedpo_engine_cosmetic(wedpo_engine *, const char *url, char *out, int cap);
int wedpo_engine_generic_css(wedpo_engine *, const char *classes, const char *ids,
                             const char *exceptions, char *out, int cap);
int wedpo_engine_strip_tracking(wedpo_engine *, const char *url, char *out, int cap);
int wedpo_engine_risk(wedpo_engine *, const char *url, const char *mime, char *reasons, int cap);
int wedpo_engine_lists_info(wedpo_engine *, char *out, int cap);
const char *wedpo_core_version(void);
}

Privacy *Privacy::instance()
{
    static Privacy s;
    return &s;
}

Privacy::Privacy(QObject *parent)
    : QObject(parent)
    , m_saveTimer(new QTimer(this))
{
    m_engine = wedpo_engine_new();
    m_saveTimer->setInterval(5000);     // periodic persistence on the main thread
    connect(m_saveTimer, &QTimer::timeout, this, &Privacy::saveStats);
    m_saveTimer->start();
    loadStats();
}

Privacy::~Privacy()
{
    saveStats();
    if (m_engine)
        wedpo_engine_free(m_engine);
}

QString Privacy::listPath(const QString &id)
{
    return Settings::instance()->dataDir() + QStringLiteral("/lists/%1.txt").arg(id);
}

const QList<Privacy::ListDef> &Privacy::availableLists()
{
    static const QList<ListDef> lists = {
        {QStringLiteral("easylist"),    QStringLiteral("EasyList"),
         QStringLiteral("Removes ads, banners and popups"),
         QStringLiteral("https://easylist.to/easylist/easylist.txt"), true},
        {QStringLiteral("easyprivacy"), QStringLiteral("EasyPrivacy"),
         QStringLiteral("Blocks tracking scripts and pixels"),
         QStringLiteral("https://easylist.to/easylist/easyprivacy.txt"), true},
        {QStringLiteral("peterlowes"),  QStringLiteral("Peter Lowe's Ad servers"),
         QStringLiteral("Blocklist of ad and tracking servers"),
         QStringLiteral("https://pgl.yoyo.org/adservers/serverlist.php?hostformat=adblockplus&showintro=0&mimetype=plaintext"), true},
        {QStringLiteral("adguardspy"),  QStringLiteral("AdGuard Tracking Protection"),
         QStringLiteral("Extra tracking protection filter"),
         QStringLiteral("https://filters.adtidy.org/extension/chromium/filters/3.txt"), false},
        {QStringLiteral("annoyances"),  QStringLiteral("EasyList Cookie/Annoyances"),
         QStringLiteral("Hides cookie notices and nags"),
         QStringLiteral("https://secure.fanboy.co.nz/fanboy-annoyance.txt"), false},
    };
    return lists;
}

void Privacy::startup()
{
    reloadLists();
}

void Privacy::shutdown()
{
    saveStats();
}

void Privacy::reloadLists()
{
    QMutexLocker lock(&m_engineMutex);
    const QStringList enabled = Settings::instance()->filterLists();
    for (const QString &id : enabled) {
        QFile f(listPath(id));
        if (!f.open(QIODevice::ReadOnly))
            continue;                       // not cached yet; updater will fetch
        const QByteArray text = f.readAll();
        wedpo_engine_load_list(m_engine, id.toUtf8().constData(), text.constData());
    }
    for (const QString &id : listIdsOnDisk())
        if (!enabled.contains(id))
            wedpo_engine_remove_list(m_engine, id.toUtf8().constData());
}

QStringList Privacy::listIdsOnDisk() const
{
    QDir d(Settings::instance()->dataDir() + "/lists");
    QStringList ids;
    for (const QFileInfo &fi : d.entryInfoList({QStringLiteral("*.txt")}, QDir::Files))
        ids << fi.completeBaseName();
    return ids;
}

void Privacy::updateList(const QString &id)
{
    QString url;
    for (const ListDef &def : availableLists())
        if (def.id == id) { url = def.url; break; }
    if (url.isEmpty()) {
        // custom list — url stored in settings
        QSettings st("Wedpo", "Wedpo");
        const QJsonObject o = QJsonDocument::fromJson(st.value("privacy/customLists").toByteArray()).object();
        url = o.value(id).toObject().value("url").toString();
    }
    if (url.isEmpty())
        return;
    QNetworkAccessManager *nam = new QNetworkAccessManager(this);
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Wedpo/1.0 (privacy list updater)"));
    QNetworkReply *reply = nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, id]() {
        nam->deleteLater();
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;
        const QByteArray data = reply->readAll();
        if (data.size() < 64 || !data.contains('\n'))
            return;
        QFile f(listPath(id));
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(data);
            f.close();
        }
        reloadLists();
        emit listsUpdated();
    });
}

void Privacy::updateAllLists()
{
    for (const QString &id : Settings::instance()->filterLists())
        updateList(id);
}

void Privacy::addCustomList(const QString &name, const QString &url)
{
    if (name.isEmpty() || url.isEmpty())
        return;
    const QString id = QStringLiteral("custom_")
        + QString::fromLatin1(QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Sha1).toHex().left(8));
    QStringList lists = Settings::instance()->filterLists();
    if (!lists.contains(id))
        lists << id;
    Settings::instance()->setFilterLists(lists);
    QSettings st("Wedpo", "Wedpo");
    QJsonObject o = QJsonDocument::fromJson(st.value("privacy/customLists").toByteArray()).object();
    o.insert(id, QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("url"), url}});
    st.setValue("privacy/customLists", QJsonDocument(o).toJson(QJsonDocument::Compact));
    updateList(id);
}

void Privacy::removeCustomList(const QString &id)
{
    if (!id.startsWith("custom_"))
        return;
    QStringList lists = Settings::instance()->filterLists();
    lists.removeAll(id);
    Settings::instance()->setFilterLists(lists);
    QFile::remove(listPath(id));
    QSettings st("Wedpo", "Wedpo");
    QJsonObject o = QJsonDocument::fromJson(st.value("privacy/customLists").toByteArray()).object();
    o.remove(id);
    st.setValue("privacy/customLists", QJsonDocument(o).toJson(QJsonDocument::Compact));
    reloadLists();
    emit listsUpdated();
}

void Privacy::importCustomText(const QString &name, const QString &text)
{
    const QString id = QStringLiteral("custom_")
        + QString::fromLatin1(QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Sha1).toHex().left(8));
    QFile f(listPath(id));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(text.toUtf8());
        f.close();
    }
    QStringList lists = Settings::instance()->filterLists();
    if (!lists.contains(id))
        lists << id;
    Settings::instance()->setFilterLists(lists);
    QSettings st("Wedpo", "Wedpo");
    QJsonObject o = QJsonDocument::fromJson(st.value("privacy/customLists").toByteArray()).object();
    o.insert(id, QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("url"), QString()}});
    st.setValue("privacy/customLists", QJsonDocument(o).toJson(QJsonDocument::Compact));
    reloadLists();
    emit listsUpdated();
}

// --- engine calls ------------------------------------------------------------
int Privacy::check(const QString &url, const QString &resourceType,
                   const QString &firstPartyHost, QString *redirectOut) const
{
    if (!Settings::instance()->adblockEnabled())
        return 0;
    if (Settings::instance()->doNotTrackPerSite(QUrl(url).host()))
        return 0;                                    // user allowed this site
    QMutexLocker lock(&m_engineMutex);
    char redirect[2048] = {0};
    const int r = wedpo_engine_check(m_engine, url.toUtf8().constData(),
                                     resourceType.toUtf8().constData(),
                                     firstPartyHost.toUtf8().constData(),
                                     redirect, int(sizeof(redirect)));
    if (r == 2 && redirectOut)
        *redirectOut = QString::fromUtf8(redirect);
    return r;
}

QString Privacy::cosmeticCss(const QString &url) const
{
    if (!Settings::instance()->adblockEnabled() || !Settings::instance()->cosmeticFiltering())
        return QString();
    if (Settings::instance()->doNotTrackPerSite(QUrl(url).host()))
        return QString();
    QMutexLocker lock(&m_engineMutex);
    static thread_local char buf[262144];
    const int n = wedpo_engine_cosmetic(m_engine, url.toUtf8().constData(), buf, int(sizeof(buf)));
    return n > 0 ? QString::fromUtf8(buf) : QString();
}

QString Privacy::genericCss(const QStringList &classes, const QStringList &ids, const QStringList &exceptions) const
{
    if (!Settings::instance()->adblockEnabled() || !Settings::instance()->cosmeticFiltering())
        return QString();
    QMutexLocker lock(&m_engineMutex);
    static thread_local char buf[262144];
    const QString c = classes.join(QLatin1Char('\n')), i = ids.join(QLatin1Char('\n')), e = exceptions.join(QLatin1Char('\n'));
    const int n = wedpo_engine_generic_css(m_engine, c.toUtf8().constData(), i.toUtf8().constData(),
                                           e.toUtf8().constData(), buf, int(sizeof(buf)));
    return n > 0 ? QString::fromUtf8(buf) : QString();
}

QString Privacy::stripTracking(const QString &url) const
{
    if (!Settings::instance()->stripTrackingParams())
        return url;
    QMutexLocker lock(&m_engineMutex);
    static thread_local char buf[2048];
    const int n = wedpo_engine_strip_tracking(m_engine, url.toUtf8().constData(), buf, int(sizeof(buf)));
    return n >= 0 ? QString::fromUtf8(buf) : url;
}

int Privacy::downloadRisk(const QString &url, const QString &mime, QStringList *reasons) const
{
    QMutexLocker lock(&m_engineMutex);
    static thread_local char buf[4096];
    const int score = wedpo_engine_risk(m_engine, url.toUtf8().constData(),
                                        mime.toUtf8().constData(), buf, int(sizeof(buf)));
    if (reasons) {
        reasons->clear();
        const QJsonDocument doc = QJsonDocument::fromJson(QString::fromUtf8(buf).toUtf8());
        for (const QJsonValue &v : doc.array())
            reasons->append(v.toString());
    }
    return score;
}

QString Privacy::listsInfo() const
{
    QMutexLocker lock(&m_engineMutex);
    static thread_local char buf[65536];
    const int n = wedpo_engine_lists_info(m_engine, buf, int(sizeof(buf)));
    return n > 0 ? QString::fromUtf8(buf) : QStringLiteral("{\"lists\":[],\"count\":0}");
}

// --- stats --------------------------------------------------------------------
void Privacy::countBlocked(const QString &host, const QString &type)
{
    {
        QMutexLocker lock(&m_statsMutex);
        m_hostCounts[host]++;
        m_typeCounts[type]++;
        m_total++;
        m_today++;
    }
    emit statsChanged();          // emitted unlocked: slots may re-enter stats getters
}

void Privacy::resetStats()
{
    {
        QMutexLocker lock(&m_statsMutex);
        m_hostCounts.clear();
        m_typeCounts.clear();
        m_total = 0;
        m_today = 0;
    }
    saveStats();
    emit statsChanged();
}

void Privacy::saveStats()
{
    QMutexLocker lock(&m_statsMutex);
    QSettings st("Wedpo", "Wedpo");
    const QString today = QDate::currentDate().toString(Qt::ISODate);
    if (m_statsDay != today) {
        m_statsDay = today;
        m_today = 1;
    }
    st.setValue("stats/day", m_statsDay);
    st.setValue("stats/today", m_today);
    st.setValue("stats/total", m_total);
    QJsonObject hosts;
    for (auto it = m_hostCounts.begin(); it != m_hostCounts.end(); ++it)
        hosts.insert(it.key(), double(it.value()));
    st.setValue("stats/hosts", QJsonDocument(hosts).toJson(QJsonDocument::Compact));
    QJsonObject types;
    for (auto it = m_typeCounts.begin(); it != m_typeCounts.end(); ++it)
        types.insert(it.key(), double(it.value()));
    st.setValue("stats/types", QJsonDocument(types).toJson(QJsonDocument::Compact));
}

void Privacy::loadStats()
{
    QMutexLocker lock(&m_statsMutex);
    QSettings st("Wedpo", "Wedpo");
    m_statsDay = st.value("stats/day").toString();
    m_today = st.value("stats/today").toLongLong();
    m_total = st.value("stats/total").toLongLong();
    const QString today = QDate::currentDate().toString(Qt::ISODate);
    if (m_statsDay != today)
        m_today = 0;
    const QJsonObject hosts = QJsonDocument::fromJson(st.value("stats/hosts").toByteArray()).object();
    for (auto it = hosts.begin(); it != hosts.end(); ++it)
        m_hostCounts.insert(it.key(), qint64(it.value().toDouble()));
    const QJsonObject types = QJsonDocument::fromJson(st.value("stats/types").toByteArray()).object();
    for (auto it = types.begin(); it != types.end(); ++it)
        m_typeCounts.insert(it.key(), qint64(it.value().toDouble()));
}

QString Privacy::currentHost() const { return m_currentHost; }
void Privacy::setCurrentHost(const QString &host) { m_currentHost = host; }
qint64 Privacy::blockedTotal() const { QMutexLocker lock(&m_statsMutex); return m_total; }
qint64 Privacy::blockedToday() const { QMutexLocker lock(&m_statsMutex); return m_today; }
qint64 Privacy::blockedForHost(const QString &host) const { QMutexLocker lock(&m_statsMutex); return m_hostCounts.value(host); }
QHash<QString, qint64> Privacy::blockedByHost(int top) const
{
    QMutexLocker lock(&m_statsMutex);
    if (top <= 0 || m_hostCounts.size() <= top)
        return m_hostCounts;
    QList<qint64> vals;
    for (auto it = m_hostCounts.begin(); it != m_hostCounts.end(); ++it)
        vals << it.value();
    std::sort(vals.begin(), vals.end(), std::greater<qint64>());
    const qint64 cutoff = vals.at(top - 1);
    QHash<QString, qint64> out;
    for (auto it = m_hostCounts.begin(); it != m_hostCounts.end(); ++it)
        if (it.value() >= cutoff)
            out.insert(it.key(), it.value());
    return out;
}
QHash<QString, qint64> Privacy::blockedByType() const { QMutexLocker lock(&m_statsMutex); return m_typeCounts; }
