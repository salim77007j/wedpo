# One-shot Qt 6.8.3 API corrections for Wedpo
import io, os, sys

BASE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'app', 'src')

def patch(path, replacements, must_all=True):
    fp = os.path.join(BASE, path)
    s = io.open(fp, encoding='utf-8').read()
    for old, new in replacements:
        if must_all and old not in s:
            print(f"!! MISSING in {path}: {old[:70]!r}")
            sys.exit(1)
        s = s.replace(old, new, 1)
    io.open(fp, 'w', encoding='utf-8', newline='\n').write(s)
    print(f"patched {path}")

# ---------- webpage.h ----------
patch('webpage.h', [
    ('''    static QString featureKey(QWebEnginePage::Feature f);

signals:
    void createTabRequested(const QUrl &url, bool background, bool fromUser);
    void zoomChanged(double factor);

protected:
    QWebEnginePage *createWindow(WebWindowType type) override;
    bool certificateError(const QWebEngineCertificateError &error) override;
    bool acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame) override;
    void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level, const QString &message,
                                  int lineNumber, const QString &sourceID) override;

private slots:
    void onFeaturePermissionRequested(const QUrl &origin, QWebEnginePage::Feature feature);
    void injectCosmeticCss(const QUrl &url);''',
     '''    static QString permissionKey(QWebEnginePermission::PermissionType type);

signals:
    void createTabRequested(const QUrl &url, bool background, bool fromUser);
    void zoomChanged(double factor);

protected:
    QWebEnginePage *createWindow(WebWindowType type) override;
    bool acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame) override;
    void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level, const QString &message,
                                  int lineNumber, const QString &sourceID) override;

private slots:
    void onPermissionRequested(QWebEnginePermission request);
    void onCertificateError(const QWebEngineCertificateError &error);
    void injectCosmeticCss(const QUrl &url);'''),
])

# ---------- webpage.cpp ----------
patch('webpage.cpp', [
    ('''    connect(this, &QWebEnginePage::featurePermissionRequested,
            this, &WebPage::onFeaturePermissionRequested);''',
     '''    connect(this, &QWebEnginePage::permissionRequested,
            this, &WebPage::onPermissionRequested);
    connect(this, &QWebEnginePage::certificateError,
            this, &WebPage::onCertificateError);'''),
    ('''    st->setAttribute(QWebEngineSettings::PdfViewerEnabled, true);

    ensureFingerprintScript();''',
     '''    st->setAttribute(QWebEngineSettings::PdfViewerEnabled, true);

    ensureFingerprintScript();'''),
    ('''QString WebPage::featureKey(QWebEnginePage::Feature f)
{
    switch (f) {
    case QWebEnginePage::Geolocation:              return QStringLiteral("geolocation");
    case QWebEnginePage::MediaAudioCapture:        return QStringLiteral("microphone");
    case QWebEnginePage::MediaVideoCapture:        return QStringLiteral("camera");
    case QWebEnginePage::MediaAudioVideoCapture:   return QStringLiteral("camera");
    case QWebEnginePage::DesktopVideoCapture:      return QStringLiteral("screen");
    case QWebEnginePage::DesktopAudioVideoCapture: return QStringLiteral("screen");
    case QWebEnginePage::Notifications:            return QStringLiteral("notifications");
    case QWebEnginePage::ClipboardReadWrite:       return QStringLiteral("clipboard");
    case QWebEnginePage::MouseLock:                return QStringLiteral("mouse");
    case QWebEnginePage::LocalFontsAccess:         return QStringLiteral("fonts");
    default:                                       return QStringLiteral("other");
    }
}''',
     '''QString WebPage::permissionKey(QWebEnginePermission::PermissionType type)
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
}'''),
    ('''void WebPage::onFeaturePermissionRequested(const QUrl &origin, QWebEnginePage::Feature feature)
{
    const QString key = featureKey(feature);
    int decision = Settings::instance()->permission(origin.toString(), key);
    if (decision == 0) {
        QMessageBox box(this->view());
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
    setFeaturePermission(origin, feature,
                         decision == 1 ? QWebEnginePage::PermissionGrantedByUser
                                       : QWebEnginePage::PermissionDeniedByUser);
}''',
     '''void WebPage::onPermissionRequested(QWebEnginePermission request)
{
    const QUrl origin = request.origin();
    const QString key = permissionKey(request.permissionType());
    int decision = Settings::instance()->permission(origin.toString(), key);
    if (decision == 0) {
        QMessageBox box(this->view());
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
}'''),
    ('''bool WebPage::certificateError(const QWebEngineCertificateError &error)
{
    QWebEngineCertificateError ce = error;''',
     '''void WebPage::onCertificateError(const QWebEngineCertificateError &error)
{
    QWebEngineCertificateError ce = error;   // the signal object is a copy we own'''),
    ('''    ce.defer();
    QMessageBox box(this->view());''',
     '''    if (!ce.isOverridable()) {
        ce.rejectCertificate();
        return;
    }
    ce.defer();
    QMessageBox box(this->view());'''),
    ('''    if (box.clickedButton() == proceed) {
        if (remember->isChecked())
            Settings::instance()->setValue(QStringLiteral("certOverride/%1").arg(host), true);
        ce.acceptCertificate();
        return true;
    }
    ce.rejectCertificate();
    return true;
}''',
     '''    if (box.clickedButton() == proceed) {
        if (remember->isChecked())
            Settings::instance()->setValue(QStringLiteral("certOverride/%1").arg(host), true);
        ce.acceptCertificate();
        return;
    }
    ce.rejectCertificate();
}'''),
])

# ---------- webview.cpp: context menu ----------
fp = os.path.join(BASE, 'webview.cpp')
s = io.open(fp, encoding='utf-8').read()
start = s.index('void WebView::contextMenuEvent(QContextMenuEvent *event)')
s = s[:start] + '''void WebView::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    if (pageAction(QWebEnginePage::Back)->isEnabled())
        menu.addAction(pageAction(QWebEnginePage::Back));
    if (pageAction(QWebEnginePage::Forward)->isEnabled())
        menu.addAction(pageAction(QWebEnginePage::Forward));
    menu.addAction(pageAction(QWebEnginePage::Reload));
    menu.addAction(pageAction(QWebEnginePage::Stop));
    menu.addSeparator();
    // link/image actions are auto-enabled by the engine when a target is under the cursor
    menu.addAction(pageAction(QWebEnginePage::OpenLinkInNewTab));
    menu.addAction(pageAction(QWebEnginePage::OpenLinkInNewWindow));
    menu.addAction(pageAction(QWebEnginePage::CopyLinkToClipboard));
    menu.addAction(pageAction(QWebEnginePage::DownloadLinkToDisk));
    menu.addSeparator();
    menu.addAction(pageAction(QWebEnginePage::CopyImageToClipboard));
    menu.addAction(pageAction(QWebEnginePage::CopyImageUrlToClipboard));
    menu.addAction(pageAction(QWebEnginePage::DownloadImageToDisk));
    menu.addSeparator();
    menu.addAction(pageAction(QWebEnginePage::Copy));
    menu.addAction(pageAction(QWebEnginePage::Paste));
    menu.addAction(pageAction(QWebEnginePage::SelectAll));
    menu.addSeparator();
    QAction *saveAct = menu.addAction(tr("Save page as…"), this, &WebView::pageRequestedSave);
    saveAct->setShortcut(QKeySequence::Save);
    QAction *printAct = menu.addAction(tr("Print…"), this, &WebView::pageRequestedPrint);
    printAct->setShortcut(QKeySequence::Print);
    menu.addSeparator();
    QAction *insp = menu.addAction(tr("Inspect element"), this, [this]() {
        triggerPageAction(QWebEnginePage::InspectElement);
    });
    insp->setShortcut(QKeySequence("F12"));
    menu.exec(event->globalPos());
}
'''
io.open(fp, 'w', encoding='utf-8', newline='\n').write(s)
print("patched webview.cpp")

# ---------- interceptor.cpp ----------
patch('interceptor.cpp', [
    ('    case R::ResourceTypeFont:           return QStringLiteral("font");',
     '    case R::ResourceTypeFontResource:   return QStringLiteral("font");'),
    ('    case R::ResourceTypeWebSocket:      return QStringLiteral("websocket");\n', ''),
    ('    case R::ResourceTypeFetch:          return QStringLiteral("fetch");\n', ''),
])

# ---------- mainwindow.cpp ----------
patch('mainwindow.cpp', [
    ('''#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    if (Settings::instance()->cookieMode() == 1) {
        m_profile->setCookieFilter([](const QWebEngineCookieFilterRequest &req) {
            return !req.thirdParty;      // block third-party cookies
        });
    }
#endif''',
     '''    if (Settings::instance()->cookieMode() == 1) {
        m_profile->cookieStore()->setCookieFilter(
            [](const QWebEngineCookieStore::FilterRequest &req) {
                return !req.thirdParty;      // block third-party cookies
            });
    }'''),
    ('#include <QWebEngineUrlRequestJob>',
     '#include <QWebEngineUrlRequestJob>\n#include <QWebEngineCookieStore>'),
    ('connect(page, &QWebEnginePage::recentAudibleChanged, this, [this, view](bool audible) {',
     'connect(page, &QWebEnginePage::recentlyAudibleChanged, this, [this, view](bool audible) {'),
    ('    page->settings()->setAttribute(QWebEngineSettings::FullSupportsViewportMeta, true);\n', ''),
    ('''    t->view->page()->print(&printer, [this](bool ok) {
        if (!ok)
            QMessageBox::warning(this, tr("Print"), tr("Printing failed."));
    });''',
     '    t->view->print(&printer);'),
    ('t->view->page()->save(path, QWebEngineDownloadRequest::MimeHtmlSavePageFormat);',
     't->view->page()->save(path, QWebEngineDownloadRequest::MimeHtmlSaveFormat);'),
])

print("ALL PATCHES APPLIED")
