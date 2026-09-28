// Wedpo browser — find-in-page bar: live match count, prev/next, Esc close.
#pragma once
#include <QWidget>

class QLineEdit;
class QLabel;
class QToolButton;
class WebView;

class FindBar : public QWidget
{
    Q_OBJECT
public:
    explicit FindBar(QWidget *parent = nullptr);

    void openFor(WebView *view);
    void closeBar();

signals:
    void closed();

protected:
    void keyPressEvent(QKeyEvent *e) override;

private:
    void find(bool backward);

    WebView *m_view = nullptr;
    QLineEdit *m_edit = nullptr;
    QLabel *m_count = nullptr;
    QToolButton *m_prev = nullptr;
    QToolButton *m_next = nullptr;
    QToolButton *m_close = nullptr;
};
