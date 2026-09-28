// Wedpo browser — internal pages shown inside tabs: Settings, Privacy
// dashboard, History, Bookmarks manager, Downloads. Every control is wired
// to real settings / stores / the privacy core.
#pragma once
#include <QWidget>
#include <QHash>

class QLineEdit;
class QListWidget;
class QTableWidget;
class QLabel;
class QComboBox;
class QCheckBox;
class QPushButton;
class QProgressBar;
class QVBoxLayout;

class SettingsPage : public QWidget
{
    Q_OBJECT
public:
    explicit SettingsPage(QWidget *parent = nullptr);

signals:
    void openUrlRequested(const QUrl &url);

private:
    void buildGeneral();
    void buildAppearance();
    void buildPrivacy();
    void buildSitePermissions();
    void buildShortcuts();

    QComboBox *m_themeCombo = nullptr;
    QComboBox *m_engineCombo = nullptr;
    QLineEdit *m_customEngine = nullptr;
    QComboBox *m_fpCombo = nullptr;
    QComboBox *m_webrtcCombo = nullptr;
    QComboBox *m_cookieCombo = nullptr;
    QTableWidget *m_listsTable = nullptr;
    QTableWidget *m_permsTable = nullptr;
};

class PrivacyDashboard : public QWidget
{
    Q_OBJECT
public:
    explicit PrivacyDashboard(QWidget *parent = nullptr);

private slots:
    void refresh();

private:
    void addSiteException();
    void removeSiteException();

    QLabel *m_today = nullptr;
    QLabel *m_total = nullptr;
    QLabel *m_lists = nullptr;
    QListWidget *m_hosts = nullptr;
    QListWidget *m_types = nullptr;
    QListWidget *m_exceptions = nullptr;
    QLineEdit *m_exceptionHost = nullptr;
};

class HistoryPage : public QWidget
{
    Q_OBJECT
public:
    explicit HistoryPage(QWidget *parent = nullptr);

signals:
    void openUrlRequested(const QUrl &url, bool newTab);

private slots:
    void rebuild();
    void clearPeriod();

private:
    QLineEdit *m_search = nullptr;
    QListWidget *m_list = nullptr;
    QComboBox *m_period = nullptr;
};

class BookmarksPage : public QWidget
{
    Q_OBJECT
public:
    explicit BookmarksPage(QWidget *parent = nullptr);

signals:
    void openUrlRequested(const QUrl &url, bool newTab);

private slots:
    void rebuild();
    void editSelected();
    void deleteSelected();

private:
    QLineEdit *m_search = nullptr;
    QListWidget *m_list = nullptr;
    QHash<int, qint64> m_rowToId;
};

class DownloadsPage : public QWidget
{
    Q_OBJECT
public:
    explicit DownloadsPage(QWidget *parent = nullptr);

private slots:
    void rebuild();
    void rowAction();

private:
    QListWidget *m_list = nullptr;
    QHash<int, quint32> m_rowToId;
};
