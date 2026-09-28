#include "omnibox.h"
#include "settings.h"
#include "stores.h"
#include "theme.h"

#include <QCompleter>
#include <QStandardItemModel>
#include <QAbstractItemView>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QUrl>
#include <QKeyEvent>
#include <QStyledItemDelegate>
#include <QScrollBar>

QUrl wedpoParseToUrl(const QString &raw)
{
    QString text = raw.trimmed();
    if (text.isEmpty())
        return QUrl();
    if (text.startsWith(QStringLiteral("wedpo://")) || text.startsWith(QStringLiteral("about:"))
        || text.startsWith(QStringLiteral("view-source:")) || text.startsWith(QStringLiteral("file://")))
        return QUrl(text);

    const bool hasScheme = text.startsWith("http://") || text.startsWith("https://");
    if (hasScheme)
        return QUrl(text, QUrl::TolerantMode);

    const int firstSlash = text.indexOf('/');
    const QString host = (firstSlash < 0 ? text : text.left(firstSlash)).toLower();
    const bool looksLikeHost = (host.contains('.') && !host.contains(' ') && !host.endsWith('.'))
                               || host == "localhost" || host.startsWith("127.");

    if (!looksLikeHost)
        return Settings::instance()->searchUrlFor(text);
    return QUrl(QStringLiteral("https://%1").arg(text), QUrl::TolerantMode);
}

namespace {
class SuggestionDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        Q_UNUSED(option)
        Q_UNUSED(index)
        return {360, 38};
    }
};
}

Omnibox::Omnibox(QWidget *parent)
    : QLineEdit(parent)
{
    setObjectName(QStringLiteral("omnibox"));
    setPlaceholderText(tr("Search DuckDuckGo or type a URL"));
    setClearButtonEnabled(true);

    m_lockAction = addAction(Theme::icon("lock", Theme::paletteColor("muted")), QLineEdit::LeadingPosition);
    m_lockAction->setToolTip(tr("Connection is secure"));

    m_shieldAction = addAction(Theme::icon("shield", Theme::paletteColor("ok")), QLineEdit::LeadingPosition);
    m_shieldAction->setToolTip(tr("Trackers and ads blocked on this site"));
    connect(m_shieldAction, &QAction::triggered, this, &Omnibox::shieldClicked);

    m_starAction = addAction(Theme::icon("star", Theme::paletteColor("muted")), QLineEdit::TrailingPosition);
    m_starAction->setToolTip(tr("Bookmark this page"));

    m_model = new QStandardItemModel(0, 1, this);
    m_completer = new QCompleter(m_model, this);
    m_completer->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setMaxVisibleItems(10);
    m_completer->popup()->setItemDelegate(new SuggestionDelegate(m_completer));
    m_completer->popup()->setWindowFlag(Qt::FramelessWindowHint);
    setCompleter(m_completer);
    connect(m_completer, QOverload<const QModelIndex &>::of(&QCompleter::activated),
            this, &Omnibox::onCompleterActivated);

    m_nam = new QNetworkAccessManager(this);

    connect(this, &QLineEdit::returnPressed, this, &Omnibox::onReturnPressed);
    connect(this, &QLineEdit::textEdited, this, &Omnibox::refreshSuggestions);
    connect(Stores::instance(), &Stores::bookmarksChanged, this, [this]() {
        setBookmarked(Stores::instance()->isBookmarked(m_currentUrl.toString()));
    });
}

void Omnibox::setUrl(const QUrl &url, bool showFull)
{
    m_currentUrl = url;
    if (url.scheme() == "wedpo") {
        setText(QString());
        setPlaceholderText(tr("Search the web privately or type a URL"));
    } else {
        setText(showFull ? url.toString() : url.toDisplayString());
        setPlaceholderText(tr("Search DuckDuckGo or type a URL"));
    }
    setSecure(url.scheme() == "https");
    setBookmarked(Stores::instance()->isBookmarked(url.toString()));
    m_completer->popup()->hide();
}

void Omnibox::setSecure(bool secure, bool certError)
{
    const QColor color = certError ? Theme::paletteColor("danger")
                                   : Theme::paletteColor(secure ? "ok" : "muted");
    m_lockAction->setIcon(Theme::icon(certError ? "alert" : (secure ? "lock" : "unlock"), color));
    m_lockAction->setToolTip(certError ? tr("Certificate error — connection is not verified")
                              : secure ? tr("Connection is secure (HTTPS)")
                                       : tr("Connection is not secure (HTTP)"));
}

void Omnibox::setBookmarked(bool bookmarked)
{
    m_starAction->setIcon(Theme::icon(bookmarked ? "star-filled" : "star",
                                       bookmarked ? Theme::paletteColor("accent") : Theme::paletteColor("muted")));
    m_starAction->setToolTip(bookmarked ? tr("Edit bookmark") : tr("Bookmark this page"));
}

void Omnibox::setBlockedCount(qint64 n)
{
    m_shieldAction->setIcon(Theme::icon("shield", n > 0 ? Theme::paletteColor("ok") : Theme::paletteColor("muted")));
    m_shieldAction->setText(n > 0 ? QString::number(n) : QString());
    m_shieldAction->setToolTip(n > 0 ? tr("%1 tracker request(s) blocked on this site").arg(n)
                                     : tr("No requests blocked on this site yet"));
}

void Omnibox::selectAllAndFocus()
{
    setFocus();
    selectAll();
}

void Omnibox::focusInEvent(QFocusEvent *e)
{
    QLineEdit::focusInEvent(e);
    if (!m_currentUrl.isEmpty())
        setText(m_currentUrl.toString());
    selectAll();
}

void Omnibox::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape) {
        if (!m_currentUrl.isEmpty())
            setText(m_currentUrl.toDisplayString());
        clearFocus();
        e->accept();
        return;
    }
    QLineEdit::keyPressEvent(e);
}

void Omnibox::onReturnPressed()
{
    const QUrl url = wedpoParseToUrl(text());
    if (url.isValid()) {
        m_completer->popup()->hide();
        clearFocus();
        emit navigateRequested(url);
    }
}

void Omnibox::onCompleterActivated(const QModelIndex &index)
{
    const QUrl url = index.data(Qt::UserRole).toUrl();
    if (url.isValid()) {
        clearFocus();
        emit navigateRequested(url);
    }
}

void Omnibox::pushSuggestion(const QString &text, const QString &subtext, const QUrl &url, const QString &icon)
{
    QStandardItem *it = new QStandardItem(Theme::icon(icon, Theme::paletteColor("muted")), text);
    it->setData(url, Qt::UserRole);
    it->setData(subtext, Qt::UserRole + 1);
    m_model->appendRow(it);
}

void Omnibox::rebuildCompleter()
{
    // (nothing cached; rebuilt per keystroke)
}

void Omnibox::refreshSuggestions()
{
    if (m_completing) {
        m_completing = false;
        return;
    }
    const QString query = text().trimmed();
    if (m_pendingReply) {
        m_pendingReply->abort();
        m_pendingReply = nullptr;
    }
    m_model->removeRows(0, m_model->rowCount());
    if (query.length() < 2)
        return;

    // local: bookmarks + history
    for (const Bookmark &b : Stores::instance()->searchBookmarks(query, 4))
        pushSuggestion(b.title.isEmpty() ? b.url : b.title, b.url, QUrl(b.url), "star");
    for (const HistoryEntry &h : Stores::instance()->searchHistory(query, 6))
        pushSuggestion(h.title.isEmpty() ? h.url : h.title, h.url, QUrl(h.url), "clock");

    // network: DuckDuckGo autocomplete
    if (Settings::instance()->searchSuggestions()) {
        QNetworkRequest req(QUrl(QStringLiteral("https://duckduckgo.com/ac/?q=%1&type=list")
                                     .arg(QString::fromUtf8(QUrl::toPercentEncoding(query)))));
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Wedpo/1.0"));
        m_pendingReply = m_nam->get(req);
        connect(m_pendingReply, &QNetworkReply::finished, this, &Omnibox::onSuggestionsFinished);
    }
}

void Omnibox::onSuggestionsFinished()
{
    QNetworkReply *reply = m_pendingReply.data();
    if (!reply)
        return;
    m_pendingReply = nullptr;
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError)
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    // Format: ["query", ["suggestion1", "suggestion2", ...]]
    if (!doc.isArray() || doc.array().size() != 2 || !doc.array().at(1).isArray())
        return;
    for (const QJsonValue &v : doc.array().at(1).toArray()) {
        const QString s = v.toString();
        if (s.isEmpty())
            continue;
        pushSuggestion(s, tr("Search"), Settings::instance()->searchUrlFor(s), "search");
        if (m_model->rowCount() >= 12)
            break;
    }
}
