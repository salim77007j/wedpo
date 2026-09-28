#include "webview.h"
#include "webpage.h"
#include "settings.h"

#include <QWheelEvent>
#include <QMenu>
#include <QContextMenuEvent>
#include <QApplication>
#include <QClipboard>
#include <QStyle>
#include <QUrl>
#include <cmath>

WebView::WebView(QWebEngineProfile *profile, QWidget *parent)
    : QWebEngineView(parent)
{
    m_page = new WebPage(profile, this);
    setPage(m_page);
    connect(m_page, &WebPage::zoomChanged, this, &WebView::zoomChanged);
    connect(this, &QWebEngineView::urlChanged, this, [this](const QUrl &url) {
        if (!url.host().isEmpty())
            Settings::instance()->setZoomForHost(url.host(), zoomFactor()); // no-op keeps map fresh
    });
}

WebPage *WebView::webPage() const
{
    return m_page;
}

void WebView::applyZoom(double f)
{
    f = qBound(0.25, f, 5.0);
    setZoomFactor(f);
    const QUrl url = this->url();
    if (!url.host().isEmpty())
        Settings::instance()->setZoomForHost(url.host(), f);
    emit zoomChanged(f);
}

void WebView::zoomIn()
{
    applyZoom(std::round((zoomFactor() + 0.10) * 100.0) / 100.0);
}

void WebView::zoomOut()
{
    applyZoom(std::round((zoomFactor() - 0.10) * 100.0) / 100.0);
}

void WebView::zoomReset()
{
    applyZoom(1.0);
}

void WebView::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        if (event->angleDelta().y() > 0)
            zoomIn();
        else if (event->angleDelta().y() < 0)
            zoomOut();
        event->accept();
        return;
    }
    QWebEngineView::wheelEvent(event);
}

void WebView::contextMenuEvent(QContextMenuEvent *event)
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
