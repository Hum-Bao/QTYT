#pragma once

#include <QByteArray>
#include <QSet>
#include <QString>
#include <mutex>

class AppSettings {
public:
    [[nodiscard]] static AppSettings &instance();

    AppSettings(const AppSettings &) = delete;
    AppSettings &operator=(const AppSettings &) = delete;

    // Window geometry and state
    [[nodiscard]] QByteArray getGeometry() const;
    void setGeometry(const QByteArray &geometry);

    [[nodiscard]] QByteArray getWindowState() const;
    void setWindowState(const QByteArray &state);

    // Audio volume (0 - 100, default: 100)
    [[nodiscard]] int getVolume() const;
    void setVolume(int volume);

    // Playback quality (default: "1080p")
    [[nodiscard]] QString getQuality() const;
    void setQuality(const QString &quality);

    // Hide Shorts in feed (default: false)
    [[nodiscard]] bool getHideShorts() const;
    void setHideShorts(bool hide);

    // Allow AV1 video streams (default: false)
    [[nodiscard]] bool getAllowAv1() const;
    void setAllowAv1(bool allow);

    // Autoplay next video (default: true)
    [[nodiscard]] bool getAutoplay() const;
    void setAutoplay(bool autoplay);

    // Theater mode watch layout (default: false)
    [[nodiscard]] bool getTheaterMode() const;
    void setTheaterMode(bool theater);

    // Channel selection state
    [[nodiscard]] QString getSelectedChannelPageId() const;
    [[nodiscard]] int getSelectedAuthUserIndex() const;
    [[nodiscard]] QString getSelectedChannelName() const;
    void setSelectedChannel(const QString &pageId, int authUser, const QString &name);

    // Logging level ("warn", "info", "debug", "trace", "error", "none", default: "warn")
    [[nodiscard]] QString getLogLevel() const;
    void setLogLevel(const QString &level);

    // Force flush in-memory changes to disk immediately
    void sync();

    // Check if a specific setting exists in memory or persistent storage
    [[nodiscard]] bool containsKey(const QString &key) const;

    // Check if a specific setting has been explicitly saved to persistent disk storage
    [[nodiscard]] bool hasPersistedKey(const QString &key) const;

private:
    AppSettings();
    ~AppSettings();

    void loadFromDisk();
    void saveToDiskLocked();

    mutable std::mutex m_mutex;

    QByteArray m_geometry;
    QByteArray m_windowState;
    int m_volume = 100;
    QString m_quality = "1080p";
    bool m_hideShorts = false;
    bool m_allowAv1 = false;
    bool m_autoplay = true;
    bool m_theaterMode = false;

    QString m_selectedChannelPageId;
    int m_selectedAuthUserIndex = -1;
    QString m_selectedChannelName;

    QString m_logLevel = "warn";

    bool m_dirty = false;
    QSet<QString> m_cachedDiskKeys;
};
