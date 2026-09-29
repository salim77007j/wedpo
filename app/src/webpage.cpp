#include "webpage.h"
#include "webview.h"
#include "settings.h"
#include "privacy.h"
#include "stores.h"

#include <QWebEngineSettings>
#include <QWebEngineCertificateError>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QMessageBox>
#include <QPushButton>
#include <QCheckBox>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QUrlQuery>
#include <QFile>
#include <QDebug>

static QString jsStringLiteral(const QString &s)
{
    QString out;
    out.reserve(s.size() + 16);
    out.append("\");
    for (const QChar c : s) {
        switch (c.unicode()) {
        case 0x22:  out.append("\\""); break;
        case 0x5C:  out.append("\\\\"); break;
        case 0x0A: out.append("\n"); break;
        case 0x0D: out.append("\r"); break;
        case 0x09: out.append("\t"); break;
        default:
            if (c.unicode() < 0x20)
                out.append(QStringLiteral("\u%1").arg(c.unicode(), 4, 16, QLatin1Char(0)));
            else
                out.append(c);
        }
    }
    out.append("\");
    return out;
}
WebPage::WebPage(QWebEngineProfile *profile, QObject *parent)
    : QWebEnginePage(profile, parent)
{
    connect(this, &QWebEnginePage::permissionRequested,
            this, &WebPage::onPermissionRequested);
    connect(this, &QWebEnginePage::certificateError,
            this, &WebPage::onCertificateError);
    connect(this, &QWebEnginePage::urlChanged, this, [this](const QUrl &url) {
        if (m_pendingPopup && url.isValid() && !url.isEmpty()) {
            const QUrl u = url;
            m_pendingPopup->deleteLater();
            m_pendingPopup = nullptr;
            emit createTabRequested(u, false, false);
        }
    });
    connect(this, &QWebEnginePage::urlChanged, this, &WebPage::injectCosmeticCss);
    connect(this, &QWebEnginePage::loadFinished, this, [this](bool ok) {
        if (ok)
            collectGenericCosmetic();
    });
    connect(this, &QWebEnginePage::zoomFactorChanged, this, &WebPage::zoomChanged);

    QWebEngineSettings *st = settings();
    st->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture,
                     Settings::instance()->autoplayBlocked());
    st->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows,
                     !Settings::instance()->blockPopups());
    st->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, true);
    st->setAttribute(QWebEngineSettings::ScrollAnimatorEnabled, true);
    st->setAttribute(QWebEngineSettings::ErrorPageEnabled, true);
    st->setAttribute(QWebEngineSettings::PdfViewerEnabled, true);

    ensureFingerprintScript();
}

QString WebPage::permissionKey(QWebEnginePermission::PermissionType type)
{
    switch (type) {
    case QWebEnginePermission::PermissionType::Geolocation:               return QStringLiteral("geolocation");
    case QWebEnginePermission::PermissionType::MediaAudioCapture:        return QStringLiteral("microphone");
    case QWebEnginePermission::PermissionType::MediaVideoCapture:        return QStringLiteral("camera");
    case QWebEnginePermission::PermissionType::MediaAudioVideoCapture:   return QStringLiteral("camera");
    case QWebEnginePermission::PermissionType::DesktopVideoCapture:      return QStringLiteral("screen");
    case QWebEnginePermission::PermissionType::DesktopAudioVideoCapture: return QStringLiteral("screen");
    case QWebEnginePermission::PermissionType::Notifications:            return QStringLiteral("notifications");
    case QWebEnginePermission::PermissionType::ClipboardReadWrite:       return QStringLiteral("clipboard");
    case QWebEnginePermission::PermissionType::MouseLock:                return QStringLiteral("mouse");
    case QWebEnginePermission::PermissionType::LocalFontsAccess:         return QStringLiteral("fonts");
    default:                                                             return QStringLiteral("other");
    }
}

// ---- permissions -------------------------------------------------------------
void WebPage::onPermissionRequested(QWebEnginePermission request)
{
    const QUrl origin = request.origin();
    const QString key = permissionKey(request.permissionType());
    int decision = Settings::instance()->permission(origin.toString(), key);
    if (decision == 0) {
        QWidget *dlgParent = qobject_cast<QWidget *>(parent());
    QMessageBox box(dlgParent);
        box.setWindowTitle(tr("Permission request"));
        box.setIcon(QMessageBox::Question);
        box.setText(tr("Allow %1 to use %2?").arg(origin.host()).arg(key));
        QCheckBox *remember = new QCheckBox(tr("Remember this decision for %1").arg(origin.host()), &box);
        box.setCheckBox(remember);
        QPushButton *allow = box.addButton(tr("Allow"), QMessageBox::YesRole);
        QPushButton *block = box.addButton(tr("Block"), QMessageBox::NoRole);
        box.addButton(QMessageBox::Close);
        box.exec();
        decision = (box.clickedButton() == allow) ? 1 : 2;
        if (remember->isChecked())
            Settings::instance()->setPermission(origin.toString(), key, decision);
    }
    if (decision == 1)
        request.grant();
    else
        request.deny();
}

// ---- certificates --------------------------------------------------------------
void WebPage::onCertificateError(const QWebEngineCertificateError &error)
{
    QWebEngineCertificateError ce = error;   // the signal object is a copy we own
    const QUrl url = ce.url();
    const QString host = url.host();
    // remembered override?
    if (Settings::instance()->value(QStringLiteral("certOverride/%1").arg(host), false).toBool()) {
        ce.acceptCertificate();
        return;
    }
    if (!ce.isOverridable()) {
        ce.rejectCertificate();
        return;
    }
    ce.defer();
    QWidget *dlgParent = qobject_cast<QWidget *>(parent());
    QMessageBox box(dlgParent);
    box.setWindowTitle(tr("Security warning"));
    box.setIcon(QMessageBox::Warning);
    box.setText(tr("The certificate for %1 could not be verified.").arg(host));
    box.setInformativeText(ce.errorDescription());
    QPushButton *proceed = box.addButton(tr("Proceed anyway"), QMessageBox::YesRole);
    box.addButton(tr("Back to safety"), QMessageBox::NoRole);
    QCheckBox *remember = new QCheckBox(tr("Always proceed for %1 (not recommended)").arg(host), &box);
    box.setCheckBox(remember);
    box.exec();
    if (box.clickedButton() == proceed) {
        if (remember->isChecked())
            Settings::instance()->setValue(QStringLiteral("certOverride/%1").arg(host), true);
        ce.acceptCertificate();
        return;
    }
    ce.rejectCertificate();
}

// ---- navigation ------------------------------------------------------------------
bool WebPage::acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame)
{
    // Scheme allowlist for top-level navigations
    const QString scheme = url.scheme();
    static const QStringList allowed = {QStringLiteral("http"), QStringLiteral("https"),
                                        QStringLiteral("wedpo"), QStringLiteral("view-source"),
                                        QStringLiteral("data"), QStringLiteral("blob"),
                                        QStringLiteral("file"), QStringLiteral("about")};
    if (isMainFrame && !allowed.contains(scheme)) {
        // External scheme (mailto:, tel:, magnet:, ...) — hand to the OS.
        QDesktopServices::openUrl(url);
        return false;
    }
    return QWebEnginePage::acceptNavigationRequest(url, type, isMainFrame);
}

// ---- popups / window.open ----------------------------------------------------------
QWebEnginePage *WebPage::createWindow(WebWindowType type)
{
    if (m_pendingPopup)
        return m_pendingPopup.data();
    const bool background = (type == WebBrowserBackgroundTab);
    m_pendingPopup = new WebPage(profile(), this);
    connect(m_pendingPopup, &WebPage::createTabRequested, this, &WebPage::createTabRequested);
    Q_UNUSED(background)
    return m_pendingPopup.data();
}

// ---- cosmetic filters -----------------------------------------------------------------
void WebPage::injectCosmeticCss(const QUrl &url)
{
    if (url.scheme() != "http" && url.scheme() != "https")
        return;
    if (m_pendingPopup && sender() == m_pendingPopup)
        return;
    const QString css = Privacy::instance()->cosmeticCss(url.toString());
    if (css.isEmpty())
        return;
    const QString script = QStringLiteral(
        "(function(){var s=document.createElement('style');s.id='wedpo-cosmetic';"
        "s.textContent=%1;(document.head||document.documentElement).appendChild(s);})();")
        .arg(jsStringLiteral(css));
    runJavaScript(script, QWebEngineScript::ApplicationWorld);
}

void WebPage::collectGenericCosmetic()
{
    const QUrl url = this->url();
    if (url.scheme() != "http" && url.scheme() != "https")
        return;
    const QString js = QStringLiteral(
        "(function(){"
        "var cls=[],ids=[];"
        "try{var els=document.querySelectorAll('[class],[id]');"
        "for(var i=0;i<els.length&&i<4000;i++){var e=els[i];"
        "if(e.className&&typeof e.className==='string')cls.push.apply(cls,e.className.split(/\\s+/));"
        "if(e.id)ids.push(e.id);}}catch(x){}"
        "var uniq=function(a){var o={},r=[];for(var i=0;i<a.length;i++){if(a[i]&&!o[a[i]]){o[a[i]]=1;r.push(a[i]);}}return r;};"
        "console.log('__wedpo_c:'+JSON.stringify({c:uniq(cls).slice(0,1500),i:uniq(ids).slice(0,1500)}));"
        "})();");
    runJavaScript(js, QWebEngineScript::ApplicationWorld);
}

void WebPage::javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level, const QString &message,
                                       int lineNumber, const QString &sourceID)
{
    Q_UNUSED(level); Q_UNUSED(lineNumber); Q_UNUSED(sourceID)
    if (!message.startsWith("__wedpo_c:"))
        return;
    const QJsonObject o = QJsonDocument::fromJson(message.mid(10).toUtf8()).object();
    QStringList classes, ids;
    for (const QJsonValue &v : o.value("c").toArray())
        classes << v.toString();
    for (const QJsonValue &v : o.value("i").toArray())
        ids << v.toString();
    if (classes.isEmpty() && ids.isEmpty())
        return;
    // exceptions ( #@# rules matched earlier for this page are handled by the engine)
    const QString css = Privacy::instance()->genericCss(classes, ids, {});
    if (css.isEmpty())
        return;
    const QString script = QStringLiteral(
        "(function(){var s=document.createElement('style');s.id='wedpo-cosmetic-generic';"
        "s.textContent=%1;(document.head||document.documentElement).appendChild(s);})();")
        .arg(jsStringLiteral(css));
    runJavaScript(script, QWebEngineScript::ApplicationWorld);
}

// ---- fingerprint protection -------------------------------------------------------
void WebPage::ensureFingerprintScript()
{
    const int mode = Settings::instance()->fingerprintMode();
    scripts().clear();   // per-page collection: drop previous dynamic scripts
    if (mode <= 0)
        return;
    QFile f(QStringLiteral(":/inject/fp.js"));
    if (!f.open(QIODevice::ReadOnly))
        return;
    QString src = QString::fromUtf8(f.readAll());
    src.replace(QStringLiteral("__WEDPO_MODE__"), QString::number(mode));
    QWebEngineScript s;
    s.setName(QStringLiteral("wedpo-fingerprint"));
    s.setSourceCode(src);
    s.setInjectionPoint(QWebEngineScript::DocumentCreation);
    s.setWorldId(QWebEngineScript::MainWorld);
    s.setRunsOnSubFrames(true);
    scripts().insert(s);
}
