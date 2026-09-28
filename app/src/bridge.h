// Wedpo browser — bridge object for the new tab page, exposed to JS over
// QWebChannel as `wedpo`: greeting, clock handled client-side, weather via
// open-meteo (opt-in, city-based — never IP geolocation), shortcut tiles,
// search handoff, live privacy stats.
#pragma once
#include <QObject>
#include <QJsonArray>
#include <QJsonObject>

class NtpBridge : public QObject
{
    Q_OBJECT
public:
    explicit NtpBridge(QObject *parent = nullptr);

    Q_INVOKABLE QString greeting() const;
    Q_INVOKABLE QString tagline() const;
    Q_INVOKABLE QJsonArray shortcuts() const;
    Q_INVOKABLE QJsonArray topSites(int limit) const;
    Q_INVOKABLE void addShortcut(const QString &title, const QString &url);
    Q_INVOKABLE void removeShortcut(int index);
    Q_INVOKABLE void search(const QString &query);
    Q_INVOKABLE void openUrl(const QString &url);
    Q_INVOKABLE QJsonObject weather() const;
    Q_INVOKABLE void requestWeather();
    Q_INVOKABLE QJsonObject stats() const;
    Q_INVOKABLE void openSettings();

signals:
    void navigateRequested(const QUrl &url);
    void openSettingsRequested();
    void weatherChanged();
    void shortcutsChanged();

private:
    void fetchWeather();
    QJsonObject m_weather;
};
