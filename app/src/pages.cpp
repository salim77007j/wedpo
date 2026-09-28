#include "pages.h"
#include "settings.h"
#include "privacy.h"
#include "stores.h"
#include "downloads.h"
#include "theme.h"

#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QDateTime>
#include <QDesktopServices>
#include <QStyle>
#include <QMenu>
#include <QFileInfo>
#include <QCursor>
#include <QSettings>
#include <algorithm>
#include <QShortcut>
#include <QKeySequence>

// ===========================================================================
// SettingsPage
// ===========================================================================
SettingsPage::SettingsPage(QWidget *parent)
    : QWidget(parent)
{
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *root = new QWidget(scroll);
    auto *lay = new QVBoxLayout(root);
    lay->setContentsMargins(32, 24, 32, 24);
    lay->setSpacing(16);

    auto *title = new QLabel(tr("Settings"), root);
    title->setObjectName(QStringLiteral("h1"));
    lay->addWidget(title);

    auto addCard = [lay, root]() {
        auto *card = new QFrame(root);
        card->setObjectName(QStringLiteral("card"));
        lay->addWidget(card);
        return card;
    };

    // ---- General ----------------------------------------------------------
    {
        auto *card = addCard();
        auto *v = new QVBoxLayout(card);
        auto *h1 = new QLabel(tr("General"), card);
        h1->setObjectName(QStringLiteral("h2"));
        v->addWidget(h1);
        auto *form = new QFormLayout;
        form->setLabelAlignment(Qt::AlignLeft);

        auto *startup = new QComboBox(card);
        startup->addItem(tr("Open the new tab page"), "newtab");
        startup->addItem(tr("Continue where you left off"), "continue");
        const int si = startup->findData(Settings::instance()->startupMode());
        startup->setCurrentIndex(qMax(0, si));
        connect(startup, &QComboBox::activated, this, [startup](int) {
            Settings::instance()->setStartupMode(startup->currentData().toString());
        });
        form->addRow(tr("On startup"), startup);

        auto *home = new QLineEdit(card);
        home->setPlaceholderText(QStringLiteral("wedpo://newtab"));
        home->setText(Settings::instance()->homeUrl());
        connect(home, &QLineEdit::editingFinished, this, [home]() {
            Settings::instance()->setHomeUrl(home->text().trimmed().isEmpty()
                                                 ? QStringLiteral("wedpo://newtab")
                                                 : home->text().trimmed());
        });
        form->addRow(tr("Home page"), home);

        m_engineCombo = new QComboBox(card);
        m_engineCombo->addItem(QStringLiteral("DuckDuckGo"), "duckduckgo");
        m_engineCombo->addItem(QStringLiteral("Google"), "google");
        m_engineCombo->addItem(QStringLiteral("Bing"), "bing");
        m_engineCombo->addItem(QStringLiteral("Brave Search"), "brave");
        m_engineCombo->addItem(QStringLiteral("Startpage"), "startpage");
        m_engineCombo->addItem(tr("Custom…"), "custom");
        m_engineCombo->setCurrentIndex(m_engineCombo->findData(Settings::instance()->searchEngine()));
        form->addRow(tr("Search engine"), m_engineCombo);

        m_customEngine = new QLineEdit(card);
        m_customEngine->setPlaceholderText(QStringLiteral("https://example.com/search?q=%s"));
        m_customEngine->setText(Settings::instance()->customSearchUrl());
        m_customEngine->setVisible(Settings::instance()->searchEngine() == "custom");
        form->addRow(QString(), m_customEngine);
        connect(m_engineCombo, &QComboBox::activated, this, [this](int) {
            Settings::instance()->setSearchEngine(m_engineCombo->currentData().toString());
            m_customEngine->setVisible(m_engineCombo->currentData().toString() == "custom");
        });
        connect(m_customEngine, &QLineEdit::editingFinished, this, [this]() {
            Settings::instance()->setCustomSearchUrl(m_customEngine->text().trimmed());
        });

        auto *suggest = new QCheckBox(tr("Show search suggestions (DuckDuckGo)"), card);
        suggest->setChecked(Settings::instance()->searchSuggestions());
        connect(suggest, &QCheckBox::toggled, this, [suggest]() {
            Settings::instance()->setSearchSuggestions(suggest->isChecked());
        });
        form->addRow(QString(), suggest);

        auto *dlDir = new QLineEdit(card);
        dlDir->setText(Settings::instance()->downloadsDir());
        connect(dlDir, &QLineEdit::editingFinished, this, [dlDir]() {
            Settings::instance()->setDownloadsDir(dlDir->text());
        });
        auto *dlRow = new QHBoxLayout;
        dlRow->addWidget(dlDir, 1);
        auto *browse = new QPushButton(tr("Browse…"), card);
        connect(browse, &QPushButton::clicked, this, [this, dlDir]() {
            const QString dir = QFileDialog::getExistingDirectory(this, tr("Download location"),
                                                                  dlDir->text());
            if (!dir.isEmpty()) {
                dlDir->setText(dir);
                Settings::instance()->setDownloadsDir(dir);
            }
        });
        dlRow->addWidget(browse);
        form->addRow(tr("Downloads"), dlRow);

        auto *ask = new QCheckBox(tr("Ask where to save each download"), card);
        ask->setChecked(Settings::instance()->askWhereToSave());
        connect(ask, &QCheckBox::toggled, this, [ask]() {
            Settings::instance()->setAskWhereToSave(ask->isChecked());
        });
        form->addRow(QString(), ask);

        v->addLayout(form);
    }

    // ---- Appearance ---------------------------------------------------------
    {
        auto *card = addCard();
        auto *v = new QVBoxLayout(card);
        auto *h = new QLabel(tr("Appearance"), card);
        h->setObjectName(QStringLiteral("h2"));
        v->addWidget(h);
        auto *form = new QFormLayout;

        m_themeCombo = new QComboBox(card);
        m_themeCombo->addItem(tr("Match system"), "system");
        m_themeCombo->addItem(tr("Light"), "light");
        m_themeCombo->addItem(tr("Dark"), "dark");
        m_themeCombo->setCurrentIndex(m_themeCombo->findData(Settings::instance()->theme()));
        connect(m_themeCombo, &QComboBox::activated, this, [this](int) {
            Settings::instance()->setTheme(m_themeCombo->currentData().toString());
            Theme::apply();
        });
        form->addRow(tr("Theme"), m_themeCombo);

        auto *accents = new QWidget(card);
        auto *alay = new QHBoxLayout(accents);
        alay->setContentsMargins(0, 0, 0, 0);
        static const QStringList colors = {"#2563eb", "#0d9488", "#7c3aed", "#db2777",
                                           "#ea580c", "#16a34a", "#475569", "#dc2626"};
        for (int i = 0; i < colors.size(); ++i) {
            auto *b = new QPushButton(accents);
            b->setFixedSize(26, 26);
            b->setCheckable(true);
            b->setChecked(i == Settings::instance()->accentIndex());
            b->setStyleSheet(QStringLiteral("QPushButton{background:%1;border:2px solid transparent;border-radius:13px;}"
                                            "QPushButton:checked{border-color:%2;}")
                                 .arg(colors[i], Theme::paletteColor("text").name()));
            connect(b, &QPushButton::clicked, this, [i, colors, card]() {
                Settings::instance()->setAccentIndex(i);
                Theme::apply();
                // uncheck siblings
                const auto buttons = card->findChildren<QPushButton *>();
                for (QPushButton *other : buttons)
                    if (other->isCheckable() && other->width() == 26)
                        other->setChecked(other == sender());
            });
            alay->addWidget(b);
        }
        alay->addStretch();
        form->addRow(tr("Accent"), accents);

        auto *bar = new QCheckBox(tr("Show bookmarks bar"), card);
        bar->setChecked(Settings::instance()->showBookmarksBar());
        connect(bar, &QCheckBox::toggled, this, [bar]() {
            Settings::instance()->setShowBookmarksBar(bar->isChecked());
        });
        form->addRow(QString(), bar);

        v->addLayout(form);
    }

    // ---- Privacy & security ---------------------------------------------------
    {
        auto *card = addCard();
        auto *v = new QVBoxLayout(card);
        auto *h = new QLabel(tr("Privacy & security"), card);
        h->setObjectName(QStringLiteral("h2"));
        v->addWidget(h);
        auto *form = new QFormLayout;

        auto *adblock = new QCheckBox(tr("Block ads and trackers"), card);
        adblock->setChecked(Settings::instance()->adblockEnabled());
        connect(adblock, &QCheckBox::toggled, this, [adblock]() {
            Settings::instance()->setAdblockEnabled(adblock->isChecked());
            Privacy::instance()->reloadLists();
        });
        form->addRow(QString(), adblock);

        auto *cosmetic = new QCheckBox(tr("Hide leftover ad spaces (cosmetic filtering)"), card);
        cosmetic->setChecked(Settings::instance()->cosmeticFiltering());
        connect(cosmetic, &QCheckBox::toggled, this, [cosmetic]() {
            Settings::instance()->setCosmeticFiltering(cosmetic->isChecked());
        });
        form->addRow(QString(), cosmetic);

        auto *strip = new QCheckBox(tr("Strip tracking parameters from URLs"), card);
        strip->setChecked(Settings::instance()->stripTrackingParams());
        connect(strip, &QCheckBox::toggled, this, [strip]() {
            Settings::instance()->setStripTrackingParams(strip->isChecked());
        });
        form->addRow(QString(), strip);

        auto *https = new QCheckBox(tr("HTTPS-only mode (upgrade insecure pages)"), card);
        https->setChecked(Settings::instance()->httpsOnly());
        connect(https, &QCheckBox::toggled, this, [https]() {
            Settings::instance()->setHttpsOnly(https->isChecked());
        });
        form->addRow(QString(), https);

        auto *gpc = new QCheckBox(tr("Send \"Do Not Sell\" signal (Global Privacy Control)"), card);
        gpc->setChecked(Settings::instance()->sendGpc());
        connect(gpc, &QCheckBox::toggled, this, [gpc]() {
            Settings::instance()->setSendGpc(gpc->isChecked());
        });
        form->addRow(QString(), gpc);

        auto *dnt = new QCheckBox(tr("Send Do-Not-Track header"), card);
        dnt->setChecked(Settings::instance()->sendDnt());
        connect(dnt, &QCheckBox::toggled, this, [dnt]() {
            Settings::instance()->setSendDnt(dnt->isChecked());
        });
        form->addRow(QString(), dnt);

        m_fpCombo = new QComboBox(card);
        m_fpCombo->addItem(tr("Off"), 0);
        m_fpCombo->addItem(tr("Standard (canvas, audio, WebGL noise)"), 1);
        m_fpCombo->addItem(tr("Strict (also limits hardware reporting)"), 2);
        m_fpCombo->setCurrentIndex(Settings::instance()->fingerprintMode());
        connect(m_fpCombo, &QComboBox::activated, this, [this](int) {
            Settings::instance()->setFingerprintMode(m_fpCombo->currentData().toInt());
        });
        form->addRow(tr("Fingerprint protection"), m_fpCombo);

        m_webrtcCombo = new QComboBox(card);
        m_webrtcCombo->addItem(tr("Default"), 0);
        m_webrtcCombo->addItem(tr("No public IP (prevent leaks)"), 1);
        m_webrtcCombo->addItem(tr("Disable WebRTC"), 2);
        m_webrtcCombo->setCurrentIndex(Settings::instance()->webRtcMode());
        connect(m_webrtcCombo, &QComboBox::activated, this, [this](int) {
            Settings::instance()->setWebRtcMode(m_webrtcCombo->currentData().toInt());
            QMessageBox::information(this, tr("Restart needed"),
                                     tr("WebRTC handling changes apply after restarting Wedpo."));
        });
        form->addRow(tr("WebRTC IP policy"), m_webrtcCombo);

        m_cookieCombo = new QComboBox(card);
        m_cookieCombo->addItem(tr("Allow all cookies"), 0);
        m_cookieCombo->addItem(tr("Block third-party cookies"), 1);
        m_cookieCombo->addItem(tr("Block all cookies"), 2);
        m_cookieCombo->setCurrentIndex(Settings::instance()->cookieMode());
        connect(m_cookieCombo, &QComboBox::activated, this, [this](int) {
            Settings::instance()->setCookieMode(m_cookieCombo->currentData().toInt());
        });
        form->addRow(tr("Cookies"), m_cookieCombo);

        auto *popups = new QCheckBox(tr("Block pop-up windows"), card);
        popups->setChecked(Settings::instance()->blockPopups());
        connect(popups, &QCheckBox::toggled, this, [popups]() {
            Settings::instance()->setBlockPopups(popups->isChecked());
        });
        form->addRow(QString(), popups);

        auto *autoplay = new QCheckBox(tr("Block automatic video playback"), card);
        autoplay->setChecked(Settings::instance()->autoplayBlocked());
        connect(autoplay, &QCheckBox::toggled, this, [autoplay]() {
            Settings::instance()->setAutoplayBlocked(autoplay->isChecked());
        });
        form->addRow(QString(), autoplay);

        v->addLayout(form);

        // filter lists table
        auto *lh = new QLabel(tr("Filter lists"), card);
        lh->setObjectName(QStringLiteral("h2"));
        v->addWidget(lh);
        m_listsTable = new QTableWidget(0, 3, card);
        m_listsTable->setHorizontalHeaderLabels({tr("List"), tr("Description"), tr("Enabled")});
        m_listsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        m_listsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        m_listsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        m_listsTable->verticalHeader()->setVisible(false);
        m_listsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        const QStringList enabled = Settings::instance()->filterLists();
        for (const Privacy::ListDef &def : Privacy::instance()->availableLists()) {
            const int row = m_listsTable->rowCount();
            m_listsTable->insertRow(row);
            m_listsTable->setItem(row, 0, new QTableWidgetItem(def.name));
            m_listsTable->setItem(row, 1, new QTableWidgetItem(def.description));
            auto *cb = new QCheckBox(m_listsTable);
            cb->setChecked(enabled.contains(def.id));
            connect(cb, &QCheckBox::toggled, this, [this, def]() { Q_UNUSED(def); });
            connect(cb, &QCheckBox::toggled, this, [this, def, cb]() {
                QStringList lists = Settings::instance()->filterLists();
                if (cb->isChecked()) {
                    if (!lists.contains(def.id))
                        lists << def.id;
                } else {
                    lists.removeAll(def.id);
                }
                Settings::instance()->setFilterLists(lists);
                Privacy::instance()->reloadLists();
                Privacy::instance()->updateList(def.id);
            });
            m_listsTable->setCellWidget(row, 2, cb);
        }
        v->addWidget(m_listsTable);

        auto *btnRow = new QHBoxLayout;
        auto *updateBtn = new QPushButton(tr("Update lists now"), card);
        connect(updateBtn, &QPushButton::clicked, this, []() {
            Privacy::instance()->updateAllLists();
        });
        btnRow->addWidget(updateBtn);
        btnRow->addStretch();
        v->addLayout(btnRow);

        // custom list
        auto *customRow = new QHBoxLayout;
        auto *nameEdit = new QLineEdit(card);
        nameEdit->setPlaceholderText(tr("List name"));
        auto *urlEdit = new QLineEdit(card);
        urlEdit->setPlaceholderText(tr("https://example.com/list.txt"));
        auto *addBtn = new QPushButton(tr("Add list"), card);
        connect(addBtn, &QPushButton::clicked, this, [this, nameEdit, urlEdit]() {
            if (!nameEdit->text().trimmed().isEmpty() && !urlEdit->text().trimmed().isEmpty()) {
                Privacy::instance()->addCustomList(nameEdit->text().trimmed(), urlEdit->text().trimmed());
                nameEdit->clear();
                urlEdit->clear();
            }
        });
        customRow->addWidget(nameEdit);
        customRow->addWidget(urlEdit, 1);
        customRow->addWidget(addBtn);
        v->addLayout(customRow);
    }

    // ---- Site permissions ------------------------------------------------------
    {
        auto *card = addCard();
        auto *v = new QVBoxLayout(card);
        auto *h = new QLabel(tr("Site permissions"), card);
        h->setObjectName(QStringLiteral("h2"));
        v->addWidget(h);
        m_permsTable = new QTableWidget(0, 3, card);
        m_permsTable->setHorizontalHeaderLabels({tr("Site"), tr("Permission"), tr("Decision")});
        m_permsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        m_permsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        m_permsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        m_permsTable->verticalHeader()->setVisible(false);
        m_permsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        static const QStringList features = {QStringLiteral("camera"), QStringLiteral("microphone"),
                                             QStringLiteral("geolocation"), QStringLiteral("notifications"),
                                             QStringLiteral("clipboard"), QStringLiteral("screen")};
        for (const QString &feature : features) {
            const QHash<QString, int> perms = Settings::instance()->allPermissions(feature);
            for (auto it = perms.begin(); it != perms.end(); ++it) {
                if (it.value() == 0)
                    continue;
                const int row = m_permsTable->rowCount();
                m_permsTable->insertRow(row);
                m_permsTable->setItem(row, 0, new QTableWidgetItem(it.key()));
                m_permsTable->setItem(row, 1, new QTableWidgetItem(feature));
                m_permsTable->setItem(row, 2, new QTableWidgetItem(it.value() == 1 ? tr("Allow") : tr("Block")));
            }
        }
        v->addWidget(m_permsTable);
        auto *clearPerms = new QPushButton(tr("Reset all remembered permissions"), card);
        connect(clearPerms, &QPushButton::clicked, this, [this]() {
            Settings::instance()->clearPermissions();
            m_permsTable->setRowCount(0);
        });
        v->addWidget(clearPerms);
    }

    // ---- Shortcuts reference ------------------------------------------------------
    {
        auto *card = addCard();
        auto *v = new QVBoxLayout(card);
        auto *h = new QLabel(tr("Keyboard shortcuts"), card);
        h->setObjectName(QStringLiteral("h2"));
        v->addWidget(h);
        auto *form = new QFormLayout;
        form->addRow(QStringLiteral("Ctrl+T / Ctrl+W"), new QLabel(tr("New tab / close tab"), card));
        form->addRow(QStringLiteral("Ctrl+Shift+T"), new QLabel(tr("Reopen closed tab"), card));
        form->addRow(QStringLiteral("Ctrl+L"), new QLabel(tr("Focus address bar"), card));
        form->addRow(QStringLiteral("Ctrl+D"), new QLabel(tr("Bookmark this page"), card));
        form->addRow(QStringLiteral("Ctrl+F"), new QLabel(tr("Find in page"), card));
        form->addRow(QStringLiteral("Ctrl+H / Ctrl+J"), new QLabel(tr("History / Downloads"), card));
        form->addRow(QStringLiteral("Ctrl+Shift+O"), new QLabel(tr("Bookmarks manager"), card));
        form->addRow(QStringLiteral("Ctrl+P / Ctrl+S"), new QLabel(tr("Print / Save page"), card));
        form->addRow(QStringLiteral("Ctrl+Plus / Ctrl+Minus / Ctrl+0"), new QLabel(tr("Zoom in / out / reset"), card));
        form->addRow(QStringLiteral("F11"), new QLabel(tr("Full screen"), card));
        form->addRow(QStringLiteral("F12"), new QLabel(tr("Developer tools"), card));
        form->addRow(QStringLiteral("Ctrl+Shift+N"), new QLabel(tr("New private window"), card));
        v->addLayout(form);
    }

    lay->addStretch();
    scroll->setWidget(root);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);
}

// ===========================================================================
// PrivacyDashboard
// ===========================================================================
PrivacyDashboard::PrivacyDashboard(QWidget *parent)
    : QWidget(parent)
{
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *root = new QWidget(scroll);
    auto *lay = new QVBoxLayout(root);
    lay->setContentsMargins(32, 24, 32, 24);
    lay->setSpacing(16);

    auto *title = new QLabel(tr("Privacy dashboard"), root);
    title->setObjectName(QStringLiteral("h1"));
    lay->addWidget(title);

    auto *cards = new QHBoxLayout;
    auto statCard = [root](QLabel **value, const QString &label) -> QFrame * {
        auto *card = new QFrame(root);
        card->setObjectName(QStringLiteral("card"));
        auto *v = new QVBoxLayout(card);
        *value = new QLabel(card);
        (*value)->setObjectName(QStringLiteral("stat"));
        (*value)->setAlignment(Qt::AlignCenter);
        v->addWidget(*value);
        auto *l = new QLabel(label, card);
        l->setAlignment(Qt::AlignCenter);
        l->setObjectName(QStringLiteral("muted"));
        v->addWidget(l);
        return card;
    };
    cards->addWidget(statCard(&m_today, tr("Trackers blocked today")));
    cards->addWidget(statCard(&m_total, tr("Blocked all time")));
    lay->addLayout(cards);

    auto *listsCard = new QFrame(root);
    listsCard->setObjectName(QStringLiteral("card"));
    auto *lv = new QVBoxLayout(listsCard);
    m_lists = new QLabel(listsCard);
    m_lists->setWordWrap(true);
    lv->addWidget(m_lists);
    auto *updateBtn = new QPushButton(tr("Update filter lists now"), listsCard);
    connect(updateBtn, &QPushButton::clicked, this, []() {
        Privacy::instance()->updateAllLists();
    });
    lv->addWidget(updateBtn);
    lay->addWidget(listsCard);

    auto *cols = new QHBoxLayout;
    auto hostCard = new QFrame(root);
    hostCard->setObjectName(QStringLiteral("card"));
    auto *hv = new QVBoxLayout(hostCard);
    hv->addWidget(new QLabel(tr("Top blocked sites"), hostCard));
    m_hosts = new QListWidget(hostCard);
    hv->addWidget(m_hosts);
    cols->addWidget(hostCard, 1);

    auto typeCard = new QFrame(root);
    typeCard->setObjectName(QStringLiteral("card"));
    auto *tv = new QVBoxLayout(typeCard);
    tv->addWidget(new QLabel(tr("Blocked request types"), typeCard));
    m_types = new QListWidget(typeCard);
    tv->addWidget(m_types);
    cols->addWidget(typeCard, 1);
    lay->addLayout(cols);

    // site exceptions
    auto *excCard = new QFrame(root);
    excCard->setObjectName(QStringLiteral("card"));
    auto *ev = new QVBoxLayout(excCard);
    ev->addWidget(new QLabel(tr("Sites always allowed (no blocking)"), excCard));
    auto *erow = new QHBoxLayout;
    m_exceptionHost = new QLineEdit(excCard);
    m_exceptionHost->setPlaceholderText(tr("example.com"));
    erow->addWidget(m_exceptionHost, 1);
    auto *addExc = new QPushButton(tr("Always allow"), excCard);
    connect(addExc, &QPushButton::clicked, this, &PrivacyDashboard::addSiteException);
    erow->addWidget(addExc);
    auto *rmExc = new QPushButton(tr("Remove selected"), excCard);
    connect(rmExc, &QPushButton::clicked, this, &PrivacyDashboard::removeSiteException);
    erow->addWidget(rmExc);
    ev->addLayout(erow);
    m_exceptions = new QListWidget(excCard);
    ev->addWidget(m_exceptions);
    lay->addWidget(excCard);

    auto *resetBtn = new QPushButton(tr("Reset statistics"), root);
    connect(resetBtn, &QPushButton::clicked, this, []() {
        if (QMessageBox::question(nullptr, tr("Reset statistics"),
                                  tr("Clear all blocking statistics?")) == QMessageBox::Yes)
            Privacy::instance()->resetStats();
    });
    lay->addWidget(resetBtn);

    lay->addStretch();
    scroll->setWidget(root);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);

    refresh();
    connect(Privacy::instance(), &Privacy::statsChanged, this, &PrivacyDashboard::refresh);
    connect(Privacy::instance(), &Privacy::listsUpdated, this, &PrivacyDashboard::refresh);
}

void PrivacyDashboard::refresh()
{
    m_today->setText(QString::number(Privacy::instance()->blockedToday()));
    m_total->setText(QString::number(Privacy::instance()->blockedTotal()));

    // lists info pretty JSON
    const QJsonDocument info = QJsonDocument::fromJson(Privacy::instance()->listsInfo().toUtf8());
    QStringList lines;
    for (const QJsonValue &v : info.object().value(QStringLiteral("lists")).toArray()) {
        const QJsonObject o = v.toObject();
        lines << tr("%1 — %2 rules").arg(o.value("name").toString()).arg(o.value("rules").toInt());
    }
    if (lines.isEmpty())
        lines << tr("No filter lists loaded yet");
    m_lists->setText(lines.join(QStringLiteral(" • ")));

    m_hosts->clear();
    const QHash<QString, qint64> hosts = Privacy::instance()->blockedByHost(12);
    QList<QPair<qint64, QString>> sorted;
    for (auto it = hosts.begin(); it != hosts.end(); ++it)
        sorted << qMakePair(it.value(), it.key());
    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    for (const auto &p : sorted)
        m_hosts->addItem(QStringLiteral("%1  —  %2").arg(p.second).arg(p.first));

    m_types->clear();
    const QHash<QString, qint64> types = Privacy::instance()->blockedByType();
    for (auto it = types.begin(); it != types.end(); ++it)
        m_types->addItem(QStringLiteral("%1  —  %2").arg(it.key()).arg(it.value()));

    // exceptions
    QSettings st("Wedpo", "Wedpo");
    st.beginGroup("privacy/allowSites");
    m_exceptions->clear();
    for (const QString &host : st.childKeys())
        m_exceptions->addItem(host);
    st.endGroup();
}

void PrivacyDashboard::addSiteException()
{
    const QString host = m_exceptionHost->text().trimmed().toLower();
    if (host.isEmpty())
        return;
    Settings::instance()->setAdblockSiteException(host, true);
    m_exceptionHost->clear();
    refresh();
}

void PrivacyDashboard::removeSiteException()
{
    QListWidgetItem *item = m_exceptions->currentItem();
    if (!item)
        return;
    Settings::instance()->setAdblockSiteException(item->text(), false);
    refresh();
}

// ===========================================================================
// HistoryPage
// ===========================================================================
HistoryPage::HistoryPage(QWidget *parent)
    : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(32, 24, 32, 24);
    lay->setSpacing(12);

    auto *title = new QLabel(tr("History"), this);
    title->setObjectName(QStringLiteral("h1"));
    lay->addWidget(title);

    auto *row = new QHBoxLayout;
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search history"));
    m_search->setClearButtonEnabled(true);
    row->addWidget(m_search, 1);
    m_period = new QComboBox(this);
    m_period->addItem(tr("Last hour"), 3600);
    m_period->addItem(tr("Last 24 hours"), 86400);
    m_period->addItem(tr("All time"), 0);
    row->addWidget(m_period);
    auto *clearBtn = new QPushButton(tr("Clear browsing data"), this);
    connect(clearBtn, &QPushButton::clicked, this, &HistoryPage::clearPeriod);
    row->addWidget(clearBtn);
    lay->addLayout(row);

    m_list = new QListWidget(this);
    m_list->setAlternatingRowColors(true);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    lay->addWidget(m_list, 1);

    connect(m_search, &QLineEdit::textChanged, this, [this]() { rebuild(); });
    connect(m_list, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QListWidgetItem *item = m_list->itemAt(pos);
        if (!item)
            return;
        QMenu menu(this);
        menu.addAction(tr("Open"), this, [this, item]() {
            emit openUrlRequested(QUrl(item->data(Qt::UserRole).toString()), false);
        });
        menu.addAction(tr("Open in new tab"), this, [this, item]() {
            emit openUrlRequested(QUrl(item->data(Qt::UserRole).toString()), true);
        });
        menu.addSeparator();
        menu.addAction(tr("Remove from history"), this, [this, item]() {
            Stores::instance()->deleteHistoryEntry(item->data(Qt::UserRole).toString());
            rebuild();
        });
        menu.exec(m_list->mapToGlobal(pos));
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        emit openUrlRequested(QUrl(item->data(Qt::UserRole).toString()), false);
    });
    connect(Stores::instance(), &Stores::historyChanged, this, &HistoryPage::rebuild);
    rebuild();
}

void HistoryPage::rebuild()
{
    m_list->clear();
    const QString query = m_search->text().trimmed();
    const QList<HistoryEntry> entries = query.isEmpty()
                                            ? Stores::instance()->recentHistory(500)
                                            : Stores::instance()->searchHistory(query, 500);
    for (const HistoryEntry &e : entries) {
        const QDateTime dt = QDateTime::fromSecsSinceEpoch(e.lastVisit);
        QListWidgetItem *it = new QListWidgetItem(
            Theme::icon("clock", Theme::paletteColor("muted")),
            QStringLiteral("%1\n%2  •  %3").arg(e.title.isEmpty() ? e.url : e.title, e.url,
                                                dt.toString(QStringLiteral("MMM d, hh:mm"))));
        it->setData(Qt::UserRole, e.url);
        m_list->addItem(it);
    }
}

void HistoryPage::clearPeriod()
{
    const qint64 secs = m_period->currentData().toLongLong();
    const qint64 since = secs > 0 ? QDateTime::currentSecsSinceEpoch() - secs : 0;
    if (QMessageBox::question(this, tr("Clear browsing data"),
                              secs > 0 ? tr("Clear history for the selected period?")
                                       : tr("Clear all browsing history?"))
        != QMessageBox::Yes)
        return;
    Stores::instance()->clearHistory(since);
    rebuild();
}

// ===========================================================================
// BookmarksPage
// ===========================================================================
BookmarksPage::BookmarksPage(QWidget *parent)
    : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(32, 24, 32, 24);
    lay->setSpacing(12);

    auto *title = new QLabel(tr("Bookmarks"), this);
    title->setObjectName(QStringLiteral("h1"));
    lay->addWidget(title);

    auto *row = new QHBoxLayout;
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search bookmarks"));
    m_search->setClearButtonEnabled(true);
    row->addWidget(m_search, 1);
    auto *editBtn = new QPushButton(tr("Edit"), this);
    connect(editBtn, &QPushButton::clicked, this, &BookmarksPage::editSelected);
    row->addWidget(editBtn);
    auto *delBtn = new QPushButton(tr("Delete"), this);
    connect(delBtn, &QPushButton::clicked, this, &BookmarksPage::deleteSelected);
    row->addWidget(delBtn);
    lay->addLayout(row);

    m_list = new QListWidget(this);
    m_list->setAlternatingRowColors(true);
    lay->addWidget(m_list, 1);

    connect(m_search, &QLineEdit::textChanged, this, [this]() { rebuild(); });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        emit openUrlRequested(QUrl(item->data(Qt::UserRole).toString()), false);
    });
    connect(Stores::instance(), &Stores::bookmarksChanged, this, &BookmarksPage::rebuild);
    rebuild();
}

void BookmarksPage::rebuild()
{
    m_list->clear();
    m_rowToId.clear();
    const QString query = m_search->text().trimmed();
    const QList<Bookmark> marks = query.isEmpty()
                                      ? Stores::instance()->bookmarks()
                                      : Stores::instance()->searchBookmarks(query, 200);
    for (const Bookmark &b : marks) {
        QListWidgetItem *it = new QListWidgetItem(
            Theme::icon("star-filled", Theme::paletteColor("accent")),
            QStringLiteral("%1\n%2").arg(b.title.isEmpty() ? b.url : b.title, b.url));
        it->setData(Qt::UserRole, b.url);
        m_list->addItem(it);
        m_rowToId.insert(m_list->count() - 1, b.id);
    }
}

void BookmarksPage::editSelected()
{
    QListWidgetItem *item = m_list->currentItem();
    if (!item)
        return;
    const Bookmark b = Stores::instance()->bookmarkByUrl(item->data(Qt::UserRole).toString());
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Edit bookmark"));
    auto *form = new QFormLayout(&dlg);
    auto *title = new QLineEdit(b.title, &dlg);
    auto *url = new QLineEdit(b.url, &dlg);
    auto *folder = new QComboBox(&dlg);
    folder->addItem(tr("Bookmarks bar"), "bar");
    folder->addItem(tr("Other bookmarks"), "other");
    folder->setCurrentIndex(folder->findData(b.folder));
    form->addRow(tr("Title"), title);
    form->addRow(tr("URL"), url);
    form->addRow(tr("Folder"), folder);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);
    if (dlg.exec() == QDialog::Accepted && b.id != 0)
        Stores::instance()->updateBookmark(b.id, title->text(), url->text(), folder->currentData().toString());
}

void BookmarksPage::deleteSelected()
{
    QListWidgetItem *item = m_list->currentItem();
    if (!item)
        return;
    const Bookmark b = Stores::instance()->bookmarkByUrl(item->data(Qt::UserRole).toString());
    if (b.id != 0)
        Stores::instance()->removeBookmark(b.id);
}

// ===========================================================================
// DownloadsPage
// ===========================================================================
DownloadsPage::DownloadsPage(QWidget *parent)
    : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(32, 24, 32, 24);
    lay->setSpacing(12);

    auto *row = new QHBoxLayout;
    auto *title = new QLabel(tr("Downloads"), this);
    title->setObjectName(QStringLiteral("h1"));
    row->addWidget(title);
    row->addStretch();
    auto *clearBtn = new QPushButton(tr("Clear finished"), this);
    connect(clearBtn, &QPushButton::clicked, this, []() {
        Downloads::instance()->clearFinished();
    });
    row->addWidget(clearBtn);
    lay->addLayout(row);

    m_list = new QListWidget(this);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->setAlternatingRowColors(true);
    lay->addWidget(m_list, 1);

    connect(Downloads::instance(), &Downloads::itemAdded, this, [this](quint32) { rebuild(); });
    connect(Downloads::instance(), &Downloads::itemChanged, this, [this](quint32 id) {
        // lightweight in-place update
        for (int i = 0; i < m_rowToId.size(); ++i) {
            if (m_rowToId.value(i) == id) {
                const QList<DownloadItem> items = Downloads::instance()->items();
                for (const DownloadItem &d : items) {
                    if (d.id == id && i < m_list->count()) {
                        QWidget *w = m_list->itemWidget(m_list->item(i));
                        if (auto *bar = w->findChild<QProgressBar *>()) {
                            bar->setRange(0, d.total > 0 ? int(qMin<qint64>(d.total, qint64(INT_MAX))) : 0);
                            if (d.total > 0)
                                bar->setValue(int(qMin<qint64>(d.received, qint64(INT_MAX))));
                            else
                                bar->setRange(0, 0);   // busy
                        }
                        if (auto *lbl = w->findChild<QLabel *>()) {
                            const double mb = d.received / 1048576.0;
                            lbl->setText(d.state == 2 ? tr("Completed")
                                         : d.state == 1 ? tr("%1 MB").arg(mb, 0, 'f', 1)
                                         : d.state == 3 ? tr("Cancelled")
                                         : d.state == 4 ? tr("Failed")
                                         : d.state == 5 ? tr("Blocked by safety check") : tr("Starting…"));
                        }
                    }
                }
                return;
            }
        }
    });
    connect(m_list, &QListWidget::customContextMenuRequested, this, &DownloadsPage::rowAction);
    rebuild();
}

void DownloadsPage::rebuild()
{
    m_list->clear();
    m_rowToId.clear();
    const QList<DownloadItem> items = Downloads::instance()->items();
    for (const DownloadItem &d : items) {
        auto *item = new QListWidgetItem(m_list);
        auto *w = new QWidget;
        auto *v = new QVBoxLayout(w);
        v->setContentsMargins(8, 6, 8, 6);
        v->setSpacing(4);
        auto *lbl = new QLabel(
            QStringLiteral("<b>%1</b>  <span style='color:%2'>%3</span>")
                .arg(d.fileName.toHtmlEscaped(),
                     Theme::paletteColor("muted").name(),
                     d.state == 5 ? tr("Blocked — check the Downloads page")
                                  : QUrl(d.url).host()),
            w);
        lbl->setTextFormat(Qt::RichText);
        v->addWidget(lbl);
        auto *bar = new QProgressBar(w);
        bar->setTextVisible(false);
        bar->setFixedHeight(8);
        if (d.total > 0) {
            bar->setRange(0, int(qMin<qint64>(d.total, qint64(INT_MAX))));
            bar->setValue(int(qMin<qint64>(d.received, qint64(INT_MAX))));
        } else {
            bar->setRange(0, 0);
        }
        v->addWidget(bar);
        item->setSizeHint(w->sizeHint());
        m_list->setItemWidget(item, w);
        m_rowToId.insert(m_list->count() - 1, d.id);
    }
}

void DownloadsPage::rowAction()
{
    const QPoint pos = m_list->mapFromGlobal(QCursor::pos());
    QListWidgetItem *item = m_list->itemAt(pos);
    if (!item)
        return;
    const int row = m_list->row(item);
    const quint32 id = m_rowToId.value(row, 0);
    if (id == 0)
        return;
    const QList<DownloadItem> items = Downloads::instance()->items();
    for (const DownloadItem &d : items) {
        if (d.id != id)
            continue;
        QMenu menu(this);
        if (d.state == 1) {
            menu.addAction(tr("Pause"), this, [id]() { Downloads::instance()->pause(id); });
            menu.addAction(tr("Cancel"), this, [id]() { Downloads::instance()->cancel(id); });
        }
        if (d.state == 2) {
            menu.addAction(tr("Open file"), this, [d]() {
                QDesktopServices::openUrl(QUrl::fromLocalFile(d.path));
            });
            menu.addAction(tr("Show in folder"), this, [d]() {
                const QUrl dir = QUrl::fromLocalFile(QFileInfo(d.path).absolutePath());
                QDesktopServices::openUrl(dir);
            });
        }
        menu.addSeparator();
        menu.addAction(tr("Remove from list"), this, [id]() {
            Downloads::instance()->remove(id);
        });
        menu.exec(QCursor::pos());
        return;
    }
}
