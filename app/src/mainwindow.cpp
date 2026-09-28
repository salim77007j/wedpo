#include "mainwindow.h"
#include "tabstrip.h"
#include "toolbar.h"
#include "omnibox.h"
#include "webview.h"
#include "webpage.h"
#include "findbar.h"
#include "bridge.h"
#include "stores.h"
#include "settings.h"
#include "privacy.h"
#include "downloads.h"
#include "theme.h"
#include "interceptor.h"
#include "pages.h"

#include <QtWidgets>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineDownloadRequest>
#include <QWebEngineFullScreenRequest>
#include <QWebEngineUrlSchemeHandler>
#include <QWebEngineUrlRequestJob>
#include <QWebChannel>
#include <QWebEngineScript>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QPrinter>
#include <QPrintDialog>
#include <QFileInfo>
#include <QDir>
#include <QDesktopServices>

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#endif

// ===========================================================================
// NTP scheme handler — serves the embedded new tab page
// ===========================================================================
namespace {
class NtpSchemeHandler : public QWebEngineUrlSchemeHandler
{
public:
    explicit NtpSchemeHandler(QObject *parent = nullptr)
        : QWebEngineUrlSchemeHandler(parent) {}
    void requestStarted(QWebEngineUrlRequestJob *job) override
    {
        const QUrl url = job->requestUrl();
        QFile *f = new QFile(QStringLiteral(":/ntp/ntp.html"), job);
        if (!f->open(QIODevice::ReadOnly)) {
            delete f;
            job->fail(QWebEngineUrlRequestJob::UrlNotFound);
            return;
        }
        job->reply(QByteArrayLiteral("text/html; charset=utf-8"), f);
    }
};
} // namespace

// ===========================================================================
// Construction
// ===========================================================================
MainWindow::MainWindow(bool isPrivate, QWidget *parent)
    : QMainWindow(parent)
    , m_isPrivate(isPrivate)
{
    setWindowFlag(Qt::FramelessWindowHint);
    setMinimumSize(940, 560);
    resize(1360, 860);
    setAttribute(Qt::WA_DeleteOnClose);

    setupProfile();
    setupUi();
    setupActions();
    restoreOrCreateTabs();

    m_sessionTimer.setInterval(15000);
    connect(&m_sessionTimer, &QTimer::timeout, this, &MainWindow::saveSessionSoon);
    m_sessionTimer.start();
    connect(Privacy::instance(), &Privacy::statsChanged, this, &MainWindow::updateChrome);
    connect(Stores::instance(), &Stores::bookmarksChanged, this, &MainWindow::updateBookmarksBar);
    connect(Settings::instance(), &Settings::changed, this, [this](const QString &key) {
        if (key == "appearance/bookmarksBar")
            m_bookmarksBar->setVisible(Settings::instance()->showBookmarksBar());
        if (key.startsWith("privacy/allowSites"))
            Privacy::instance()->reloadLists();
    });
    updateBookmarksBar();
}

MainWindow::~MainWindow() = default;

void MainWindow::setupProfile()
{
    if (m_isPrivate) {
        m_profile = new QWebEngineProfile(this);          // off the record
    } else {
        m_profile = new QWebEngineProfile(QStringLiteral("wedpo"), this);
        m_profile->setPersistentStoragePath(Settings::instance()->dataDir() + QStringLiteral("/browsing"));
        m_profile->setCachePath(Settings::instance()->dataDir() + QStringLiteral("/cache"));
        m_profile->setHttpCacheType(QWebEngineProfile::DiskHttpCacheType);
    }
    m_profile->setHttpAcceptLanguage(QLocale().name());

    auto *interceptor = new RequestInterceptor(m_profile);
    connect(interceptor, &RequestInterceptor::requestBlocked, this, [this](const QString &host, const QString &) {
        if (currentTab() && currentTab()->view
            && QUrl(currentTab()->view->url()).host() == host)
            updateChrome();
    });
    m_profile->setUrlRequestInterceptor(interceptor);
    m_profile->installUrlSchemeHandler(QByteArrayLiteral("wedpo"), new NtpSchemeHandler(m_profile));

    switch (Settings::instance()->cookieMode()) {
    case 0:
        m_profile->setPersistentCookiesPolicy(QWebEngineProfile::AllowPersistentCookies);
        break;
    case 2:
        m_profile->setPersistentCookiesPolicy(QWebEngineProfile::NoPersistentCookies);
        break;
    default:
        m_profile->setPersistentCookiesPolicy(QWebEngineProfile::AllowPersistentCookies);
        break;
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    if (Settings::instance()->cookieMode() == 1) {
        m_profile->setCookieFilter([](const QWebEngineCookieFilterRequest &req) {
            return !req.thirdParty;      // block third-party cookies
        });
    }
#endif

    connect(m_profile, &QWebEngineProfile::downloadRequested, this, [this](QWebEngineDownloadRequest *req) {
        if (Settings::instance()->askWhereToSave()) {
            const QString suggested = Settings::instance()->downloadsDir() + "/"
                                      + req->suggestedFileName();
            const QString path = QFileDialog::getSaveFileName(this, tr("Save file"), suggested);
            if (path.isEmpty()) {
                req->cancel();
                return;
            }
            req->setDownloadDirectory(QFileInfo(path).absolutePath());
            req->setDownloadFileName(QFileInfo(path).fileName());
        }
        Downloads::instance()->attach(req);
    });
}

void MainWindow::setupUi()
{
    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    setCentralWidget(central);

    // ---- titlebar: tabs + window buttons --------------------------------
    m_titlebar = new QWidget(central);
    m_titlebar->setObjectName(QStringLiteral("tabstrip"));
    auto *tbLay = new QHBoxLayout(m_titlebar);
    tbLay->setContentsMargins(0, 0, 0, 0);
    tbLay->setSpacing(0);

    m_strip = new TabStrip(m_titlebar);
    tbLay->addWidget(m_strip, 1);

    auto winButton = [this](const QString &name, const QString &tip) {
        QToolButton *b = new QToolButton(m_titlebar);
        b->setObjectName(name == "close" ? QStringLiteral("winclose") : QStringLiteral("winbtn"));
        b->setIcon(Theme::icon(name, Theme::paletteColor("text")));
        b->setToolTip(tip);
        b->setFixedSize(46, 38);
        b->setAutoRaise(true);
        tbLay->addWidget(b);
        return b;
    };
    m_btnMin = winButton(QStringLiteral("minimize"), tr("Minimize"));
    m_btnMax = winButton(QStringLiteral("maximize"), tr("Maximize"));
    m_btnClose = winButton(QStringLiteral("close"), tr("Close"));
    connect(m_btnMin, &QToolButton::clicked, this, &QWidget::showMinimized);
    connect(m_btnMax, &QToolButton::clicked, this, [this]() {
        isMaximized() ? showNormal() : showMaximized();
    });
    connect(m_btnClose, &QToolButton::clicked, this, &QWidget::close);
    root->addWidget(m_titlebar);

    // ---- toolbar ----------------------------------------------------------
    m_toolbar = new ToolBar(central);
    root->addWidget(m_toolbar);

    // ---- bookmarks bar ------------------------------------------------------
    m_bookmarksBar = new QWidget(central);
    m_bookmarksBar->setObjectName(QStringLiteral("bookmarksbar"));
    m_bookmarksBar->setVisible(Settings::instance()->showBookmarksBar());
    root->addWidget(m_bookmarksBar);

    // ---- tab stack ------------------------------------------------------------
    m_stack = new QStackedWidget(central);
    root->addWidget(m_stack, 1);

    // ---- find bar overlay -------------------------------------------------------
    m_findbar = new FindBar(central);
    m_findbar->hide();
    connect(m_findbar, &FindBar::closed, this, [this]() {
        if (currentTab() && currentTab()->view)
            currentTab()->view->setFocus();
    });

    // ---- web channel for NTP ------------------------------------------------------
    m_bridge = new NtpBridge(this);
    m_channel = new QWebChannel(this);
    m_channel->registerObject(QStringLiteral("wedpo"), m_bridge);
    connect(m_bridge, &NtpBridge::navigateRequested, this, [this](const QUrl &url) {
        Tab *t = currentTab();
        if (t && t->view)
            t->view->load(url);
        else
            newTab(url);
    });
    connect(m_bridge, &NtpBridge::openSettingsRequested, this, [this]() {
        openInternal(QStringLiteral("settings"));
    });

    // ---- tab strip wiring ------------------------------------------------------------
    connect(m_strip, &TabStrip::tabSelected, this, &MainWindow::selectTabById);
    connect(m_strip, &TabStrip::tabCloseRequested, this, &MainWindow::closeTabById);
    connect(m_strip, &TabStrip::newTabRequested, this, [this]() { newTab(); });
    connect(m_strip, &TabStrip::tabContextMenuRequested, this, &MainWindow::tabContextMenu);
    connect(m_strip, &TabStrip::tabReordered, this, [this](qint64 id, int newIndex) {
        for (int i = 0; i < m_tabs.size(); ++i) {
            if (m_tabs[i].id == id) {
                m_tabs.move(i, newIndex);
                updateChrome();
                return;
            }
        }
    });
    connect(m_strip, &TabStrip::audioToggleRequested, this, [this](qint64 id) {
        Tab *t = tabById(id);
        if (t && t->view)
            t->view->page()->setAudioMuted(!t->view->page()->isAudioMuted());
    });
    connect(m_strip, &TabStrip::emptyAreaDoubleClicked, this, [this]() {
        isMaximized() ? showNormal() : showMaximized();
    });

    // ---- toolbar wiring -------------------------------------------------------------
    connect(m_toolbar, &ToolBar::backClicked, this, &MainWindow::goBack);
    connect(m_toolbar, &ToolBar::forwardClicked, this, &MainWindow::goForward);
    connect(m_toolbar, &ToolBar::reloadClicked, this, &MainWindow::reloadPage);
    connect(m_toolbar, &ToolBar::stopClicked, this, &MainWindow::stopPage);
    connect(m_toolbar, &ToolBar::navigateRequested, this, &MainWindow::navigateCurrent);
    connect(m_toolbar, &ToolBar::shieldClicked, this, &MainWindow::onShieldClicked);
    connect(m_toolbar, &ToolBar::bookmarkStarClicked, this, &MainWindow::toggleBookmark);
    connect(m_toolbar, &ToolBar::downloadsClicked, this, [this]() {
        openInternal(QStringLiteral("downloads"));
    });
    connect(m_toolbar, &ToolBar::menuRequested, this, &MainWindow::showAppMenu);

    connect(Downloads::instance(), &Downloads::activeCountChanged, this, [this]() {
        m_toolbar->setDownloadsBadge(Downloads::instance()->activeCount());
    });
    connect(Downloads::instance(), &Downloads::riskWarning,
            this, &MainWindow::onDownloadRisk);
}

void MainWindow::setupActions()
{
    auto add = [this](const QString &text, const QKeySequence &key, auto slot) {
        QAction *a = new QAction(text, this);
        if (!key.isEmpty())
            a->setShortcut(key);
        connect(a, &QAction::triggered, this, slot);
        addAction(a);
        return a;
    };

    add(tr("New tab"), QKeySequence("Ctrl+T"), [this]() { newTab(); });
    add(tr("New window"), QKeySequence("Ctrl+N"), [this]() {
        (new MainWindow(false))->show();
    });
    add(tr("New private window"), QKeySequence("Ctrl+Shift+N"), [this]() {
        (new MainWindow(true))->show();
    });
    add(tr("Close tab"), QKeySequence("Ctrl+W"), [this]() {
        if (currentTab())
            closeTabById(m_activeId);
    });
    add(tr("Reopen closed tab"), QKeySequence("Ctrl+Shift+T"), [this]() { reopenClosedTab(); });
    add(tr("Focus address bar"), QKeySequence("Ctrl+L"), [this]() {
        m_toolbar->omnibox()->selectAllAndFocus();
    });
    add(tr("History"), QKeySequence("Ctrl+H"), [this]() { openInternal(QStringLiteral("history")); });
    add(tr("Bookmarks manager"), QKeySequence("Ctrl+Shift+O"), [this]() {
        openInternal(QStringLiteral("bookmarks"));
    });
    add(tr("Downloads"), QKeySequence("Ctrl+J"), [this]() { openInternal(QStringLiteral("downloads")); });
    add(tr("Bookmark this page"), QKeySequence("Ctrl+D"), [this]() { toggleBookmark(); });
    add(tr("Find in page"), QKeySequence::Find, [this]() { findInPage(); });
    add(tr("Print"), QKeySequence::Print, [this]() { printPage(); });
    add(tr("Save page"), QKeySequence::Save, [this]() { savePage(); });
    add(tr("Zoom in"), QKeySequence("Ctrl++"), [this]() { zoomIn(); });
    add(tr("Zoom in"), QKeySequence("Ctrl+="), [this]() { zoomIn(); });
    add(tr("Zoom out"), QKeySequence("Ctrl+-"), [this]() { zoomOut(); });
    add(tr("Reset zoom"), QKeySequence("Ctrl+0"), [this]() { zoomReset(); });
    add(tr("Full screen"), QKeySequence("F11"), [this]() { toggleWindowFullScreen(); });
    add(tr("Developer tools"), QKeySequence("F12"), [this]() { toggleDevTools(); });
    add(tr("Developer tools"), QKeySequence("Ctrl+Shift+I"), [this]() { toggleDevTools(); });
    add(tr("Home"), QKeySequence("Alt+Home"), [this]() { goHome(); });
    add(tr("Back"), QKeySequence("Alt+Left"), [this]() { goBack(); });
    add(tr("Forward"), QKeySequence("Alt+Right"), [this]() { goForward(); });
    add(tr("Reload"), QKeySequence("Ctrl+R"), [this]() { reloadPage(); });
    add(tr("Reload"), QKeySequence::Refresh, [this]() { reloadPage(); });
    add(tr("Stop"), QKeySequence("Esc"), [this]() { stopPage(); });
    add(tr("Next tab"), QKeySequence("Ctrl+Tab"), [this]() {
        if (m_tabs.size() < 2)
            return;
        for (int i = 0; i < m_tabs.size(); ++i)
            if (m_tabs[i].id == m_activeId)
                selectTabById(m_tabs[(i + 1) % m_tabs.size()].id);
    });
    add(tr("Previous tab"), QKeySequence("Ctrl+Shift+Tab"), [this]() {
        if (m_tabs.size() < 2)
            return;
        for (int i = 0; i < m_tabs.size(); ++i)
            if (m_tabs[i].id == m_activeId)
                selectTabById(m_tabs[(i - 1 + m_tabs.size()) % m_tabs.size()].id);
    });
    add(tr("Settings"), QKeySequence("Ctrl+,"), [this]() { openInternal(QStringLiteral("settings")); });
    add(tr("Privacy dashboard"), QKeySequence("Ctrl+Shift+P"), [this]() {
        openInternal(QStringLiteral("privacy"));
    });
    add(tr("Exit"), QKeySequence("Ctrl+Q"), [this]() { qApp->closeAllWindows(); });
    for (int i = 1; i <= 8; ++i) {
        const QKeySequence key(QStringLiteral("Ctrl+%1").arg(i));
        QAction *a = new QAction(this);
        a->setShortcut(key);
        connect(a, &QAction::triggered, this, [this, i]() {
            if (i - 1 < m_tabs.size())
                selectTabById(m_tabs[i - 1].id);
        });
        addAction(a);
    }
    QAction *last = new QAction(this);
    last->setShortcut(QKeySequence("Ctrl+9"));
    connect(last, &QAction::triggered, this, [this]() {
        if (!m_tabs.isEmpty())
            selectTabById(m_tabs.last().id);
    });
    addAction(last);
}

// ===========================================================================
// Tabs
// ===========================================================================
MainWindow::Tab *MainWindow::tabById(qint64 id)
{
    for (Tab &t : m_tabs)
        if (t.id == id)
            return &t;
    return nullptr;
}

MainWindow::Tab *MainWindow::currentTab()
{
    return tabById(m_activeId);
}

qint64 MainWindow::createTabWidget(WebView *view, QWidget *internalPage, const QString &internalKind)
{
    Tab t;
    t.id = m_nextId++;
    t.container = new QWidget(this);
    auto *lay = new QVBoxLayout(t.container);
    lay->setContentsMargins(0, 0, 0, 0);
    if (view) {
        lay->addWidget(view);
        t.view = view;
        t.title = tr("New tab");
    } else {
        lay->addWidget(internalPage);
        t.internal = internalPage;
        t.internalKind = internalKind;
        static const QHash<QString, QString> names = {
            {QStringLiteral("settings"), QStringLiteral("Settings")},
            {QStringLiteral("privacy"), QStringLiteral("Privacy dashboard")},
            {QStringLiteral("history"), QStringLiteral("History")},
            {QStringLiteral("bookmarks"), QStringLiteral("Bookmarks")},
            {QStringLiteral("downloads"), QStringLiteral("Downloads")}};
        t.title = names.value(internalKind, internalKind);
    }
    m_stack->addWidget(t.container);
    m_tabs.append(t);
    return t.id;
}

void MainWindow::installPageHooks(WebView *view)
{
    WebPage *page = view->webPage();
    view->page()->setWebChannel(m_channel);
    page->settings()->setAttribute(QWebEngineSettings::FullSupportsViewportMeta, true);

    connect(page, &WebPage::createTabRequested, this,
            [this](const QUrl &url, bool background, bool) { newTab(url, background, !background); });

    connect(page, &QWebEnginePage::titleChanged, this, [this, view](const QString &title) {
        for (Tab &t : m_tabs)
            if (t.view == view)
                t.title = title.isEmpty() ? tr("New tab") : title;
        updateChrome();
    });
    connect(page, &QWebEnginePage::iconChanged, this, [this, view](const QIcon &icon) {
        for (Tab &t : m_tabs)
            if (t.view == view) {
                t.icon = icon.pixmap(16, 16);
                if (t.icon.isNull())
                    t.icon = Theme::icon("globe", Theme::paletteColor("muted"), 16);
            }
        updateChrome();
    });
    connect(page, &QWebEnginePage::urlChanged, this, [this, view](const QUrl &url) {
        if (currentTab() && currentTab()->view == view) {
            m_toolbar->omnibox()->setUrl(url);
            Privacy::instance()->setCurrentHost(url.host());
            m_toolbar->setBlockedCount(Privacy::instance()->blockedForHost(url.host()));
        }
        updateChrome();
    });
    connect(page, &QWebEnginePage::loadStarted, this, [this, view]() {
        for (Tab &t : m_tabs)
            if (t.view == view)
                t.loading = true;
        updateChrome();
    });
    connect(page, &QWebEnginePage::loadFinished, this, [this, view](bool ok) {
        for (Tab &t : m_tabs)
            if (t.view == view)
                t.loading = false;
        if (ok) {
            const QUrl url = view->url();
            if ((url.scheme() == "http" || url.scheme() == "https") && !m_isPrivate)
                Stores::instance()->recordVisit(url.toString(), view->page()->title());
        }
        updateChrome();
        if (currentTab() && currentTab()->view == view) {
            m_toolbar->omnibox()->setUrl(view->url());
            m_toolbar->setBookmarked(Stores::instance()->isBookmarked(view->url().toString()));
        }
    });
    connect(page, &QWebEnginePage::recentAudibleChanged, this, [this, view](bool audible) {
        for (Tab &t : m_tabs)
            if (t.view == view)
                t.audible = audible;
        updateChrome();
    });
    connect(page, &WebPage::zoomChanged, this, [this, view](double) {
        if (currentTab() && currentTab()->view == view)
            updateChrome();
    });
    connect(page, &QWebEnginePage::fullScreenRequested, this, [this](QWebEngineFullScreenRequest req) {
        req.accept();
        if (req.toggleOn()) {
            Tab *t = currentTab();
            if (!t || !t->view)
                return;
            m_fsView = t->view;
            t->view->setParent(nullptr);
            t->view->showFullScreen();
        } else if (m_fsView) {
            WebView *v = m_fsView.data();
            for (Tab &t : m_tabs) {
                if (t.view == v) {
                    v->setParent(this);
                    t.container->layout()->addWidget(v);
                    m_stack->setCurrentWidget(t.container);
                    break;
                }
            }
            v->showNormal();
            m_fsView.clear();
        }
    });
    connect(view, &WebView::pageRequestedSave, this, &MainWindow::savePage);
    connect(view, &WebView::pageRequestedPrint, this, &MainWindow::printPage);
    // per-host zoom
    connect(view, &WebView::zoomChanged, this, [this](double f) {
        Tab *t = currentTab();
        if (t && t->view)
            m_toolbar->omnibox()->setToolTip(tr("Zoom: %1%").arg(int(f * 100)));
    });
    view->page()->setZoomFactor(Settings::instance()->zoomForHost(view->url().host()));
}

void MainWindow::newTab(const QUrl &url, bool background, bool activate)
{
    WebView *view = new WebView(m_profile, this);
    const qint64 id = createTabWidget(view, nullptr, QString());
    installPageHooks(view);
    if (url.isValid() && !url.isEmpty())
        view->load(url);
    else
        view->load(QUrl(QStringLiteral("wedpo://newtab")));
    if (activate && !background)
        selectTabById(id);
    updateChrome();
    saveSessionSoon();
}

void MainWindow::closeTabById(qint64 id)
{
    for (int i = 0; i < m_tabs.size(); ++i) {
        Tab &t = m_tabs[i];
        if (t.id != id)
            continue;
        if (t.view) {
            const QUrl url = t.view->url();
            if (!url.isEmpty() && url.scheme() != "wedpo")
                m_closedTabs.prepend({url, t.title});
            if (m_closedTabs.size() > 25)
                m_closedTabs.removeLast();
            if (m_devToolsView && t.view->page()->devToolsPage() == m_devToolsView->page())
                t.view->page()->setDevToolsPage(nullptr);
        }
        m_stack->removeWidget(t.container);
        t.container->deleteLater();
        m_tabs.removeAt(i);
        break;
    }
    if (m_tabs.isEmpty()) {
        close();
        return;
    }
    if (m_activeId == id && !m_tabs.isEmpty())
        selectTabById(m_tabs.first().id);
    updateChrome();
    saveSessionSoon();
}

void MainWindow::selectTabById(qint64 id)
{
    Tab *t = tabById(id);
    if (!t)
        return;
    m_activeId = id;
    m_stack->setCurrentWidget(t->container);
    if (t->view) {
        m_toolbar->omnibox()->setUrl(t->view->url());
        m_toolbar->setBookmarked(Stores::instance()->isBookmarked(t->view->url().toString()));
        Privacy::instance()->setCurrentHost(t->view->url().host());
        m_toolbar->setBlockedCount(Privacy::instance()->blockedForHost(t->view->url().host()));
        rebindDevTools();
        updateChrome();
        if (m_findbar->isVisible())
            m_findbar->openFor(t->view);
    } else {
        updateChrome();
    }
    saveSessionSoon();
}

void MainWindow::reopenClosedTab()
{
    if (m_closedTabs.isEmpty())
        return;
    const auto entry = m_closedTabs.takeFirst();
    newTab(entry.first, false, true);
}

void MainWindow::rebindDevTools()
{
    if (!m_devToolsView)
        return;
    Tab *t = currentTab();
    if (t && t->view)
        t->view->page()->setDevToolsPage(m_devToolsView->page());
}

// ===========================================================================
// Navigation
// ===========================================================================
void MainWindow::navigateCurrent(const QUrl &url)
{
    if (!url.isValid() || url.isEmpty())
        return;
    Tab *t = currentTab();
    if (t && t->view)
        t->view->load(url);
    else
        newTab(url, false, true);
    saveSessionSoon();
}

void MainWindow::goBack()
{
    Tab *t = currentTab();
    if (t && t->view)
        t->view->back();
}

void MainWindow::goForward()
{
    Tab *t = currentTab();
    if (t && t->view)
        t->view->forward();
}

void MainWindow::reloadPage()
{
    Tab *t = currentTab();
    if (t && t->view)
        t->view->reload();
}

void MainWindow::stopPage()
{
    Tab *t = currentTab();
    if (t && t->view)
        t->view->stop();
    if (m_findbar->isVisible())
        m_findbar->closeBar();
}

void MainWindow::goHome()
{
    navigateCurrent(wedpoParseToUrl(Settings::instance()->homeUrl()));
}

void MainWindow::toggleBookmark()
{
    Tab *t = currentTab();
    if (!t || !t->view)
        return;
    const QUrl url = t->view->url();
    if (url.isEmpty())
        return;
    const Bookmark existing = Stores::instance()->bookmarkByUrl(url.toString());
    if (existing.id != 0)
        Stores::instance()->removeBookmark(existing.id);
    else
        Stores::instance()->addBookmark(t->title, url.toString(), QStringLiteral("bar"));
    m_toolbar->setBookmarked(Stores::instance()->isBookmarked(url.toString()));
}

void MainWindow::onShieldClicked()
{
    openInternal(QStringLiteral("privacy"));
}

// ===========================================================================
// Chrome updates
// ===========================================================================
void MainWindow::updateChrome()
{
    QList<TabInfo> infos;
    for (const Tab &t : m_tabs) {
        TabInfo info;
        info.id = t.id;
        info.title = t.title;
        info.icon = t.icon.isNull() && !t.internal ? Theme::icon("globe", Theme::paletteColor("muted"), 16) : t.icon;
        info.pinned = t.pinned;
        info.muted = t.muted;
        info.audible = t.audible;
        info.loading = t.loading;
        info.active = t.id == m_activeId;
        infos.append(info);
    }
    m_strip->setTabs(infos);

    Tab *t = currentTab();
    if (t && t->view) {
        WebPage *page = t->view->webPage();
        m_toolbar->updateNavigationState(
            page->action(QWebEnginePage::Back)->isEnabled(),
            page->action(QWebEnginePage::Forward)->isEnabled(),
            t->loading);
    } else {
        m_toolbar->updateNavigationState(false, false, false);
    }
    setWindowTitle(QStringLiteral("%1 — Wedpo").arg(t ? t->title : tr("Wedpo")));
}

void MainWindow::updateBookmarksBar()
{
    qDeleteAll(m_bookmarksBar->findChildren<QToolButton *>());
    delete m_bookmarksBar->layout();
    auto *lay = new QHBoxLayout(m_bookmarksBar);
    lay->setContentsMargins(10, 3, 10, 3);
    lay->setSpacing(2);

    for (const Bookmark &b : Stores::instance()->bookmarks(QStringLiteral("bar"))) {
        auto *btn = new QToolButton(m_bookmarksBar);
        btn->setText(b.title.isEmpty() ? b.url : b.title);
        btn->setToolTip(b.url);
        btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
        btn->setAutoRaise(true);
        const QUrl url(b.url);
        connect(btn, &QToolButton::clicked, this, [this, url](bool) {
            navigateCurrent(url);
        });
        btn->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(btn, &QToolButton::customContextMenuRequested, this, [this, b](const QPoint &pos) {
            QMenu menu(this);
            menu.addAction(tr("Open in new tab"), this, [this, b]() {
                newTab(QUrl(b.url), false, true);
            });
            menu.addSeparator();
            menu.addAction(tr("Remove"), this, [b]() {
                Stores::instance()->removeBookmark(b.id);
            });
            menu.exec(m_bookmarksBar->mapToGlobal(pos));
        });
        lay->addWidget(btn);
    }
    lay->addStretch();

    // Other bookmarks
    const QList<Bookmark> other = Stores::instance()->bookmarks(QStringLiteral("other"));
    auto *otherBtn = new QToolButton(m_bookmarksBar);
    otherBtn->setText(tr("Other Bookmarks"));
    otherBtn->setAutoRaise(true);
    otherBtn->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(otherBtn);
    for (const Bookmark &b : other)
        menu->addAction(b.title.isEmpty() ? b.url : b.title, this, [this, b]() {
            navigateCurrent(QUrl(b.url));
        });
    if (other.isEmpty())
        menu->addAction(tr("(empty)"))->setEnabled(false);
    otherBtn->setMenu(menu);
    lay->addWidget(otherBtn);
}

// ===========================================================================
// Internal pages
// ===========================================================================
void MainWindow::openInternal(const QString &kind)
{
    for (const Tab &t : m_tabs)
        if (t.internalKind == kind) {
            selectTabById(t.id);
            return;
        }
    QWidget *page = nullptr;
    if (kind == "settings")
        page = new SettingsPage(this);
    else if (kind == "privacy")
        page = new PrivacyDashboard(this);
    else if (kind == "history")
        page = new HistoryPage(this);
    else if (kind == "bookmarks")
        page = new BookmarksPage(this);
    else if (kind == "downloads")
        page = new DownloadsPage(this);
    else
        return;
    if (auto *hp = qobject_cast<HistoryPage *>(page))
        connect(hp, &HistoryPage::openUrlRequested, this, [this](const QUrl &url, bool newTabReq) {
            newTabReq ? newTab(url, false, true) : navigateCurrent(url);
        });
    if (auto *bp = qobject_cast<BookmarksPage *>(page))
        connect(bp, &BookmarksPage::openUrlRequested, this, [this](const QUrl &url, bool newTabReq) {
            newTabReq ? newTab(url, false, true) : navigateCurrent(url);
        });
    const qint64 id = createTabWidget(nullptr, page, kind);
    selectTabById(id);
}

void MainWindow::tabContextMenu(qint64 id, const QPoint &globalPos)
{
    Tab *t = tabById(id);
    if (!t)
        return;
    QMenu menu(this);
    menu.addAction(tr("New tab"), this, [this]() { newTab(); });
    menu.addSeparator();
    menu.addAction(tr("Reload"), this, [this, id]() {
        selectTabById(id);
        reloadPage();
    });
    menu.addAction(tr("Duplicate"), this, [this, t]() {
        if (t->view)
            newTab(t->view->url(), false, true);
    });
    if (t->view) {
        menu.addAction(t->muted ? tr("Unmute tab") : tr("Mute tab"), this, [this, t]() {
            t->muted = !t->muted;
            t->view->page()->setAudioMuted(t->muted);
            updateChrome();
        });
    }
    menu.addAction(t->pinned ? tr("Unpin tab") : tr("Pin tab"), this, [this, id]() {
        if (Tab *tab = tabById(id)) {
            tab->pinned = !tab->pinned;
            updateChrome();
        }
    });
    menu.addSeparator();
    menu.addAction(tr("Close tab"), this, [this, id]() { closeTabById(id); });
    menu.addAction(tr("Close other tabs"), this, [this, id]() {
        for (const Tab &tab : QList<Tab>(m_tabs))
            if (tab.id != id)
                closeTabById(tab.id);
        selectTabById(id);
    });
    menu.addAction(tr("Close tabs to the right"), this, [this, id]() {
        bool seen = false;
        for (const Tab &tab : QList<Tab>(m_tabs)) {
            if (tab.id == id) {
                seen = true;
                continue;
            }
            if (seen)
                closeTabById(tab.id);
        }
    });
    menu.exec(globalPos);
}

// ===========================================================================
// Find / print / save / zoom / fullscreen / devtools
// ===========================================================================
void MainWindow::findInPage()
{
    Tab *t = currentTab();
    if (!t || !t->view)
        return;
    const QRect global = QRect(m_stack->mapToGlobal(QPoint(0, 0)), m_stack->size());
    m_findbar->setParent(centralWidget());
    m_findbar->move(centralWidget()->width() - m_findbar->width() - 24, 96);
    m_findbar->openFor(t->view);
    m_findbar->raise();
}

void MainWindow::printPage()
{
    Tab *t = currentTab();
    if (!t || !t->view)
        return;
    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dlg(&printer, this);
    dlg.setWindowTitle(tr("Print page"));
    if (dlg.exec() != QDialog::Accepted)
        return;
    t->view->page()->print(&printer, [this](bool ok) {
        if (!ok)
            QMessageBox::warning(this, tr("Print"), tr("Printing failed."));
    });
}

void MainWindow::savePage()
{
    Tab *t = currentTab();
    if (!t || !t->view)
        return;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save page"),
        QDir(Settings::instance()->downloadsDir()).filePath(t->title + QStringLiteral(".mhtml")),
        tr("MHTML single file (*.mhtml)"));
    if (path.isEmpty())
        return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    t->view->page()->save(path, QWebEngineDownloadRequest::MimeHtmlSavePageFormat);
#else
    QMessageBox::information(this, tr("Save page"), tr("Save page requires Qt 6.8 or newer."));
#endif
}

void MainWindow::zoomIn()
{
    if (Tab *t = currentTab(); t && t->view)
        t->view->zoomIn();
}

void MainWindow::zoomOut()
{
    if (Tab *t = currentTab(); t && t->view)
        t->view->zoomOut();
}

void MainWindow::zoomReset()
{
    if (Tab *t = currentTab(); t && t->view)
        t->view->zoomReset();
}

void MainWindow::toggleWindowFullScreen()
{
    isFullScreen() ? showNormal() : showFullScreen();
}

void MainWindow::toggleDevTools()
{
    Tab *t = currentTab();
    if (!t || !t->view)
        return;
    if (!m_devToolsWindow) {
        m_devToolsWindow = new QWidget(nullptr);
        m_devToolsWindow->setWindowTitle(tr("Wedpo DevTools"));
        m_devToolsWindow->resize(980, 620);
        auto *lay = new QVBoxLayout(m_devToolsWindow);
        lay->setContentsMargins(0, 0, 0, 0);
        m_devToolsView = new QWebEngineView(m_profile, m_devToolsWindow);
        lay->addWidget(m_devToolsView);
    }
    if (m_devToolsWindow->isVisible()) {
        t->view->page()->setDevToolsPage(nullptr);
        m_devToolsWindow->hide();
    } else {
        t->view->page()->setDevToolsPage(m_devToolsView->page());
        m_devToolsWindow->show();
        m_devToolsWindow->raise();
    }
}

// ===========================================================================
// Downloads risk dialog
// ===========================================================================
void MainWindow::onDownloadRisk(quint32 id, int score, const QStringList &reasons)
{
    QString body = tr("Wedpo flagged this download as potentially unsafe (risk score %1/100):").arg(score);
    body += QStringLiteral("\n\n• %1").arg(reasons.join(QStringLiteral("\n• ")));
    body += QStringLiteral("\n\n%1").arg(tr("Download it anyway?"));
    QMessageBox box(this);
    box.setWindowTitle(tr("Suspicious download"));
    box.setIcon(QMessageBox::Warning);
    box.setText(body);
    QPushButton *keep = box.addButton(tr("Keep"), QMessageBox::YesRole);
    box.addButton(tr("Discard"), QMessageBox::NoRole);
    box.exec();
    if (box.clickedButton() == keep)
        Downloads::instance()->acceptDownload(id);
    else
        Downloads::instance()->rejectDownload(id);
}

// ===========================================================================
// App menu
// ===========================================================================
void MainWindow::showAppMenu(const QPoint &globalPos)
{
    QMenu menu(this);
    menu.addAction(tr("New tab"), this, [this]() { newTab(); }, QKeySequence("Ctrl+T"));
    menu.addAction(tr("New window"), this, [this]() { (new MainWindow(false))->show(); },
                   QKeySequence("Ctrl+N"));
    menu.addAction(tr("New private window"), this, [this]() { (new MainWindow(true))->show(); },
                   QKeySequence("Ctrl+Shift+N"));
    menu.addSeparator();
    QMenu *history = menu.addMenu(Theme::icon("clock"), tr("History"));
    for (const HistoryEntry &h : Stores::instance()->recentHistory(8))
        history->addAction(h.title.isEmpty() ? h.url : h.title, this, [this, h]() {
            navigateCurrent(QUrl(h.url));
        });
    history->addSeparator();
    history->addAction(tr("Show full history"), this, [this]() {
        openInternal(QStringLiteral("history"));
    }, QKeySequence("Ctrl+H"));
    QMenu *bookmarks = menu.addMenu(Theme::icon("star-filled"), tr("Bookmarks"));
    bookmarks->addAction(tr("Bookmark this page"), this, [this]() { toggleBookmark(); },
                         QKeySequence("Ctrl+D"));
    bookmarks->addAction(tr("Bookmarks manager"), this, [this]() {
        openInternal(QStringLiteral("bookmarks"));
    }, QKeySequence("Ctrl+Shift+O"));
    menu.addAction(Theme::icon("download"), tr("Downloads"), this, [this]() {
        openInternal(QStringLiteral("downloads"));
    }, QKeySequence("Ctrl+J"));
    menu.addSeparator();
    menu.addAction(Theme::icon("shield"), tr("Privacy dashboard"), this, [this]() {
        openInternal(QStringLiteral("privacy"));
    }, QKeySequence("Ctrl+Shift+P"));
    menu.addAction(Theme::icon("settings"), tr("Settings"), this, [this]() {
        openInternal(QStringLiteral("settings"));
    }, QKeySequence("Ctrl+,"));
    menu.addSeparator();
    menu.addAction(Theme::icon("zoom-in"), tr("Zoom in"), this, [this]() { zoomIn(); },
                   QKeySequence("Ctrl++"));
    menu.addAction(Theme::icon("zoom-out"), tr("Zoom out"), this, [this]() { zoomOut(); },
                   QKeySequence("Ctrl+-"));
    menu.addAction(tr("Reset zoom"), this, [this]() { zoomReset(); }, QKeySequence("Ctrl+0"));
    menu.addAction(tr("Full screen"), this, [this]() { toggleWindowFullScreen(); },
                   QKeySequence("F11"));
    menu.addSeparator();
    menu.addAction(Theme::icon("print"), tr("Print…"), this, [this]() { printPage(); },
                   QKeySequence("Ctrl+P"));
    menu.addAction(Theme::icon("save"), tr("Save page as…"), this, [this]() { savePage(); },
                   QKeySequence("Ctrl+S"));
    menu.addAction(Theme::icon("devtools"), tr("Developer tools"), this, [this]() { toggleDevTools(); },
                   QKeySequence("F12"));
    menu.addSeparator();
    menu.addAction(Theme::icon("info"), tr("About Wedpo"), this, [this]() {
        QMessageBox about(this);
        about.setWindowTitle(tr("About Wedpo"));
        about.setTextFormat(Qt::RichText);
        about.setText(QStringLiteral(
            "<div style='text-align:center'>"
            "<h2>Wedpo %1</h2>"
            "<p>A cleaner, brighter web — Simple. Fast. Yours.</p>"
            "<p>Rust privacy core (adblock, tracker stripping)<br>"
            "Chromium rendering via QtWebEngine</p>"
            "</div>").arg(QCoreApplication::applicationVersion()));
        about.exec();
    });
    menu.addSeparator();
    menu.addAction(Theme::icon("close"), tr("Exit"), this, [this]() { qApp->closeAllWindows(); },
                   QKeySequence("Ctrl+Q"));
    menu.exec(globalPos);
}

// ===========================================================================
// Session
// ===========================================================================
QJsonArray MainWindow::serializeAllWindows(bool *clean)
{
    if (clean)
        *clean = true;
    QJsonArray windows;
    for (QWidget *w : QApplication::topLevelWidgets()) {
        auto *mw = qobject_cast<MainWindow *>(w);
        if (!mw || mw->isPrivateWindow())
            continue;
        QJsonArray tabs;
        int active = 0;
        for (int i = 0; i < mw->m_tabs.size(); ++i) {
            const Tab &t = mw->m_tabs[i];
            QJsonObject o;
            if (t.view) {
                const QUrl url = t.view->url();
                if (url.isEmpty())
                    continue;
                o.insert(QStringLiteral("url"), url.toString());
            } else {
                o.insert(QStringLiteral("url"), QStringLiteral("wedpo://internal/%1").arg(t.internalKind));
            }
            o.insert(QStringLiteral("title"), t.title);
            o.insert(QStringLiteral("pinned"), t.pinned);
            o.insert(QStringLiteral("muted"), t.muted);
            if (t.id == mw->m_activeId)
                active = i;
            tabs.append(o);
        }
        if (!tabs.isEmpty()) {
            QJsonObject win;
            win.insert(QStringLiteral("tabs"), tabs);
            win.insert(QStringLiteral("active"), active);
            windows.append(win);
        }
    }
    return windows;
}

void MainWindow::saveSessionNow(bool clean)
{
    bool dummy = true;
    Stores::instance()->saveSession(serializeAllWindows(&dummy), clean);
}

bool MainWindow::hasRestorableSession(bool *clean)
{
    QJsonArray windows;
    const bool has = Stores::instance()->loadSession(&windows, clean);
    return has && !windows.isEmpty();
}

bool MainWindow::restoreFromSession(const QJsonArray &windows)
{
    bool created = false;
    for (const QJsonValue &wv : windows) {
        const QJsonObject win = wv.toObject();
        const QJsonArray tabs = win.value(QStringLiteral("tabs")).toArray();
        if (tabs.isEmpty())
            continue;
        auto *mw = new MainWindow(false);
        int active = win.value(QStringLiteral("active")).toInt();
        active = qBound(0, active, tabs.size() - 1);
        qint64 activeId = 0;
        for (int i = 0; i < tabs.size(); ++i) {
            const QJsonObject t = tabs[i].toObject();
            const QString u = t.value(QStringLiteral("url")).toString();
            QUrl url;
            if (u.startsWith("wedpo://internal/"))
                mw->openInternal(u.section('/', -1));
            else
                url = QUrl(u);
            if (url.isValid() && !url.isEmpty())
                mw->newTab(url, true, false);
            if (mw->m_tabs.isEmpty())
                mw->newTab(QUrl(QStringLiteral("wedpo://newtab")), true, false);
            // pin/mute state
            Tab &tabRef = mw->m_tabs.last();
            tabRef.pinned = t.value(QStringLiteral("pinned")).toBool(false);
            tabRef.muted = t.value(QStringLiteral("muted")).toBool(false);
            if (i == active)
                activeId = tabRef.id;
        }
        if (activeId != 0)
            mw->selectTabById(activeId);
        mw->updateChrome();
        mw->show();
        created = true;
    }
    return created;
}

void MainWindow::saveSessionSoon()
{
    if (m_isPrivate)
        return;
    saveSessionNow(false);
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    QMainWindow::closeEvent(e);
    saveSessionSoon();
}

// ===========================================================================
// Windows frameless hit-testing (resize edges, caption drag, snap)
// ===========================================================================
bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
#ifdef Q_OS_WIN
    if (eventType == "windows_generic_MSG") {
        MSG *msg = static_cast<MSG *>(message);
        if (msg->message == WM_NCHITTEST) {
            const int x = GET_X_LPARAM(msg->lParam);
            const int y = GET_Y_LPARAM(msg->lParam);
            POINT pt{x, y};
            ScreenToClient(msg->hwnd, &pt);
            const int w = width(), h = height();
            const bool max = isMaximized();
            const int m = 8;
            if (!max) {
                const bool left = pt.x < m;
                const bool right = pt.x > w - m;
                const bool top = pt.y < m;
                const bool bottom = pt.y > h - m;
                if (top && left)     { *result = HTTOPLEFT; return true; }
                if (top && right)    { *result = HTTOPRIGHT; return true; }
                if (bottom && left)  { *result = HTBOTTOMLEFT; return true; }
                if (bottom && right) { *result = HTBOTTOMRIGHT; return true; }
                if (left)            { *result = HTLEFT; return true; }
                if (right)           { *result = HTRIGHT; return true; }
                if (top)             { *result = HTTOP; return true; }
                if (bottom)          { *result = HTBOTTOM; return true; }
            }
            // caption over empty strip area
            if (pt.y >= 0 && pt.y < m_strip->height()
                && !m_strip->pointInInteractiveArea(QPoint(pt.x, pt.y))
                && !m_btnMin->geometry().contains(QPoint(pt.x, pt.y))
                && !m_btnMax->geometry().contains(QPoint(pt.x, pt.y))
                && !m_btnClose->geometry().contains(QPoint(pt.x, pt.y))) {
                *result = HTCAPTION;
                return true;
            }
            *result = HTCLIENT;
            return true;
        }
    }
#else
    Q_UNUSED(eventType)
    Q_UNUSED(message)
    Q_UNUSED(result)
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}
