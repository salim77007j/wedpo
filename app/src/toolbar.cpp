#include "toolbar.h"
#include "omnibox.h"
#include "theme.h"

#include <QToolButton>
#include <QHBoxLayout>

QToolButton *ToolBar::makeButton(const QString &iconName, const QString &tooltip)
{
    QToolButton *b = new QToolButton(this);
    b->setIcon(Theme::icon(iconName));
    b->setToolTip(tooltip);
    b->setAutoRaise(true);
    b->setFixedSize(34, 34);
    b->setIconSize(QSize(18, 18));
    return b;
}

ToolBar::ToolBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("toolbar"));
    setFixedHeight(46);

    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(8, 6, 8, 6);
    lay->setSpacing(4);

    m_back = makeButton("back", tr("Back (Alt+Left)"));
    m_forward = makeButton("forward", tr("Forward (Alt+Right)"));
    m_reloadStop = makeButton("reload", tr("Reload this page (Ctrl+R)"));

    lay->addWidget(m_back);
    lay->addWidget(m_forward);
    lay->addWidget(m_reloadStop);

    m_omnibox = new Omnibox(this);
    lay->addWidget(m_omnibox, 1);

    m_downloads = makeButton("download", tr("Downloads (Ctrl+J)"));
    m_downloads->hide();                       // appears only while downloads run
    lay->addWidget(m_downloads);

    m_menu = makeButton("menu", tr("Settings and more (Alt+F)"));
    lay->addWidget(m_menu);

    connect(m_back, &QToolButton::clicked, this, &ToolBar::backClicked);
    connect(m_forward, &QToolButton::clicked, this, &ToolBar::forwardClicked);
    connect(m_reloadStop, &QToolButton::clicked, this, [this]() {
        if (m_loading)
            emit stopClicked();
        else
            emit reloadClicked();
    });
    connect(m_omnibox, &Omnibox::navigateRequested, this, &ToolBar::navigateRequested);
    connect(m_omnibox, &Omnibox::shieldClicked, this, &ToolBar::shieldClicked);
    connect(m_omnibox->starAction(), &QAction::triggered, this, &ToolBar::bookmarkStarClicked);
    connect(m_downloads, &QToolButton::clicked, this, &ToolBar::downloadsClicked);
    connect(m_menu, &QToolButton::clicked, this, [this]() {
        emit menuRequested(m_menu->mapToGlobal(QPoint(0, m_menu->height())));
    });
}

void ToolBar::updateNavigationState(bool canBack, bool canForward, bool loading)
{
    m_back->setEnabled(canBack);
    m_forward->setEnabled(canForward);
    if (m_loading != loading) {
        m_loading = loading;
        m_reloadStop->setIcon(Theme::icon(loading ? "close" : "reload"));
        m_reloadStop->setToolTip(loading ? tr("Stop loading this page (Esc)")
                                         : tr("Reload this page (Ctrl+R)"));
    }
}

void ToolBar::setDownloadsBadge(int active)
{
    m_downloads->setVisible(active > 0);
    m_downloads->setText(active > 0 ? QString::number(active) : QString());
}

void ToolBar::setSecure(bool secure, bool certError)
{
    m_omnibox->setSecure(secure, certError);
}

void ToolBar::setBookmarked(bool bookmarked)
{
    m_omnibox->setBookmarked(bookmarked);
}

void ToolBar::setBlockedCount(qint64 n)
{
    m_omnibox->setBlockedCount(n);
}
