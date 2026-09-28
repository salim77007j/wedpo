// Wedpo browser — application entry point: scheme registration, WebEngine
// flags (WebRTC policy), theme, single-flow startup with session restore
// and crash recovery.
#include "mainwindow.h"
#include "settings.h"
#include "privacy.h"
#include "stores.h"
#include "theme.h"

#include <QApplication>
#include <QWebEngineUrlScheme>
#include <QMessageBox>
#include <QPushButton>
#include <QJsonArray>

static void registerWedpoScheme()
{
    QWebEngineUrlScheme scheme(QByteArrayLiteral("wedpo"));
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme
                    | QWebEngineUrlScheme::LocalScheme
                    | QWebEngineUrlScheme::LocalAccessAllowed
                    | QWebEngineUrlScheme::ContentSecurityPolicyIgnored
                    | QWebEngineUrlScheme::FetchApiAllowed);
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Host);
    QWebEngineUrlScheme::registerScheme(scheme);
}

static void applyWebEngineFlags()
{
    QString flags = qEnvironmentVariable("QTWEBENGINE_CHROMIUM_FLAGS");
    switch (Settings::instance()->webRtcMode()) {
    case 1:
        flags += QStringLiteral(" --force-webrtc-ip-handling-policy=disable_non_proxied_udp");
        break;
    case 2:
        flags += QStringLiteral(" --disable-webrtc");
        break;
    default:
        break;
    }
    flags += QStringLiteral(" --disable-features=Translate");
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", flags.toUtf8());
}

int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QCoreApplication::setOrganizationName(QStringLiteral("Wedpo"));
    QCoreApplication::setApplicationName(QStringLiteral("Wedpo"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));

    // Scheme registration must precede QApplication (Qt requirement).
    registerWedpoScheme();

    QApplication app(argc, argv);
    applyWebEngineFlags();

    Theme::apply();
    Privacy::instance()->startup();
    Stores::instance();   // open the store now

    // ---- session restore / crash recovery --------------------------------
    bool restored = false;
    bool clean = true;
    if (MainWindow::hasRestorableSession(&clean)) {
        if (!clean && Settings::instance()->restoreOnCrash()) {
            QMessageBox box;
            box.setWindowTitle(QObject::tr("Wedpo didn't shut down correctly"));
            box.setIcon(QMessageBox::Question);
            box.setText(QObject::tr("Wedpo was not closed properly last time.
"

                                    "Restore your previous session?"));
            QPushButton *restoreBtn = box.addButton(QObject::tr("Restore session"),
                                                    QMessageBox::YesRole);
            box.addButton(QObject::tr("Start fresh"), QMessageBox::NoRole);
            box.exec();
            if (box.clickedButton() == restoreBtn) {
                QJsonArray windows;
                Stores::instance()->loadSession(&windows, &clean);
                restored = MainWindow::restoreFromSession(windows);
            }
        } else if (clean && Settings::instance()->startupMode() == "continue") {
            QJsonArray windows;
            Stores::instance()->loadSession(&windows, &clean);
            restored = MainWindow::restoreFromSession(windows);
        }
    }
    if (!restored) {
        auto *win = new MainWindow(false);
        win->show();
    }

    // clean shutdown marker
    QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
        MainWindow::saveSessionNow(true);
        Privacy::instance()->shutdown();
    });

    return app.exec();
}
