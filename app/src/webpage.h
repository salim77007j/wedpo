// Wedpo browser — QWebEnginePage subclass: permissions, certificates,
// cosmetic filter injection, per-site zoom, popup handling.
#pragma once
#include <QWebEnginePage>
#include <QPointer>

class WebPage : public QWebEnginePage
{
    Q_OBJECT
public:
    explicit WebPage(QWebEngineProfile *profile, QObject *parent = nullptr);

    static QString featureKey(QWebEnginePage::Feature f);

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
    void injectCosmeticCss(const QUrl &url);
    void collectGenericCosmetic();
    void applyZoomForHost(const QUrl &url);

private:
    void ensureFingerprintScript();
    QPointer<WebPage> m_pendingPopup;
};
