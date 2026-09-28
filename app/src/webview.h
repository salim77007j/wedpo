// Wedpo browser — QWebEngineView subclass: zoom handling, context menu,
// per-host zoom application, render-process crash recovery UI.
#pragma once
#include <QWebEngineView>

class WebPage;

class WebView : public QWebEngineView
{
    Q_OBJECT
public:
    explicit WebView(QWebEngineProfile *profile, QWidget *parent = nullptr);
    WebPage *webPage() const;

    void zoomIn();
    void zoomOut();
    void zoomReset();
    double zoomFactor() const { return QWebEngineView::zoomFactor(); }

signals:
    void zoomChanged(double factor);
    void pageRequestedSave();
    void pageRequestedPrint();

protected:
    void wheelEvent(QWheelEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void applyZoom(double f);
    WebPage *m_page = nullptr;
};
