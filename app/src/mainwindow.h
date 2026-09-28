// Wedpo browser — main window: frameless shell with tabs in the titlebar
// (Windows hit-testing for snap/resize), toolbar, bookmarks bar, tab stack,
// find bar, full action set, session save/restore with crash recovery.
#pragma once
#include <QMainWindow>
#include <QList>
#include <QTimer>
#include <QPixmap>
#include <QPointer>
#include <QJsonArray>

class TabStrip;
class ToolBar;
class Omnibox;
class FindBar;
class QStackedWidget;
class QToolButton;
class QWebEngineProfile;
class QWebEngineView;
class QWebEngineFullScreenRequest;
class QWebChannel;
class WebView;
class NtpBridge;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(bool isPrivate = false, QWidget *parent = nullptr);
    ~MainWindow() override;

    bool isPrivateWindow() const { return m_isPrivate; }

    // session helpers used by main()
    static QJsonArray serializeAllWindows(bool *clean);
    static void saveSessionNow(bool clean);
    static bool hasRestorableSession(bool *clean);
    static bool restoreFromSession(const QJsonArray &windows);

protected:
    void closeEvent(QCloseEvent *e) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

private slots:
    void newTab(const QUrl &url = QUrl(), bool background = false, bool activate = true);
    void closeTabById(qint64 id);
    void selectTabById(qint64 id);
    void reopenClosedTab();
    void navigateCurrent(const QUrl &url);
    void goBack();
    void goForward();
    void reloadPage();
    void stopPage();
    void goHome();
    void toggleBookmark();
    void showAppMenu(const QPoint &globalPos);
    void tabContextMenu(qint64 id, const QPoint &globalPos);
    void findInPage();
    void printPage();
    void savePage();
    void zoomIn();
    void zoomOut();
    void zoomReset();
    void toggleWindowFullScreen();
    void toggleDevTools();
    void openInternal(const QString &kind);
    void onShieldClicked();
    void onDownloadRisk(quint32 id, int score, const QStringList &reasons);
    void updateChrome();
    void updateBookmarksBar();
    void saveSessionSoon();

private:
    struct Tab {
        qint64 id = 0;
        QWidget *container = nullptr;
        WebView *view = nullptr;          // null for internal pages
        QWidget *internal = nullptr;      // settings/privacy/history/... page
        QString internalKind;
        QString title;
        QPixmap icon;
        bool pinned = false;
        bool muted = false;
        bool audible = false;
        bool loading = false;
    };

    Tab *tabById(qint64 id);
    Tab *currentTab();
    qint64 createTabWidget(WebView *view, QWidget *internalPage, const QString &internalKind);
    void installPageHooks(WebView *view);
    void setupProfile();
    void setupUi();
    void setupActions();
    void restoreOrCreateTabs();
    void rebindDevTools();

    QWebEngineProfile *m_profile = nullptr;
    bool m_isPrivate = false;

    QList<Tab> m_tabs;
    qint64 m_activeId = 0;
    qint64 m_nextId = 1;
    QList<QPair<QUrl, QString>> m_closedTabs;

    QWidget *m_titlebar = nullptr;
    TabStrip *m_strip = nullptr;
    QToolButton *m_btnMin = nullptr;
    QToolButton *m_btnMax = nullptr;
    QToolButton *m_btnClose = nullptr;
    QWidget *m_bookmarksBar = nullptr;
    ToolBar *m_toolbar = nullptr;
    QStackedWidget *m_stack = nullptr;
    FindBar *m_findbar = nullptr;
    QWebChannel *m_channel = nullptr;
    NtpBridge *m_bridge = nullptr;
    QWebEngineView *m_devToolsView = nullptr;
    QWidget *m_devToolsWindow = nullptr;
    QPointer<WebView> m_fsView;
    QTimer m_sessionTimer;
};
