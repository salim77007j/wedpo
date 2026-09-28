#include "findbar.h"
#include "webview.h"
#include "theme.h"

#include <QLineEdit>
#include <QLabel>
#include <QToolButton>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QWebEngineFindTextResult>

FindBar::FindBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("findbar"));
    setFixedHeight(40);

    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(10, 4, 10, 4);
    lay->setSpacing(6);

    m_edit = new QLineEdit(this);
    m_edit->setPlaceholderText(tr("Find in page"));
    m_edit->setFixedWidth(220);
    lay->addWidget(m_edit);

    m_count = new QLabel(this);
    m_count->setObjectName(QStringLiteral("muted"));
    lay->addWidget(m_count);

    m_prev = new QToolButton(this);
    m_prev->setIcon(Theme::icon("chevron-up"));
    m_prev->setToolTip(tr("Previous match (Shift+Enter)"));
    m_prev->setAutoRaise(true);
    lay->addWidget(m_prev);

    m_next = new QToolButton(this);
    m_next->setIcon(Theme::icon("chevron-down"));
    m_next->setToolTip(tr("Next match (Enter)"));
    m_next->setAutoRaise(true);
    lay->addWidget(m_next);

    m_close = new QToolButton(this);
    m_close->setIcon(Theme::icon("close"));
    m_close->setToolTip(tr("Close (Esc)"));
    m_close->setAutoRaise(true);
    lay->addWidget(m_close);
    lay->addStretch();

    connect(m_edit, &QLineEdit::textChanged, this, [this]() { find(false); });
    connect(m_prev, &QToolButton::clicked, this, [this]() { find(true); });
    connect(m_next, &QToolButton::clicked, this, [this]() { find(false); });
    connect(m_close, &QToolButton::clicked, this, &FindBar::closeBar);
}

void FindBar::openFor(WebView *view)
{
    m_view = view;
    show();
    m_edit->setFocus();
    m_edit->selectAll();
    if (!m_edit->text().isEmpty())
        find(false);
}

void FindBar::closeBar()
{
    if (m_view)
        m_view->page()->findText(QString());   // clear highlights
    m_view = nullptr;
    m_count->clear();
    hide();
    emit closed();
}

void FindBar::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape) {
        closeBar();
        return;
    }
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        find(e->modifiers() & Qt::ShiftModifier);
        return;
    }
    QWidget::keyPressEvent(e);
}

void FindBar::find(bool backward)
{
    if (!m_view)
        return;
    const QString needle = m_edit->text();
    if (needle.isEmpty()) {
        m_view->page()->findText(QString());
        m_count->clear();
        return;
    }
    QWebEnginePage::FindFlags flags;
    if (backward)
        flags |= QWebEnginePage::FindBackward;
    m_view->page()->findText(needle, flags, [this](const QWebEngineFindTextResult &r) {
        m_count->setText(r.numberOfMatches() > 0
                             ? tr("%1/%2").arg(r.activeMatch()).arg(r.numberOfMatches())
                             : tr("No matches"));
    });
}
