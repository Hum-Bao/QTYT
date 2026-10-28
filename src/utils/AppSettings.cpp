#include "AppSettings.h"

#include "AppPaths.h"
#include "Logger.h"

#include <QSettings>
#include <algorithm>
#include <filesystem>

namespace {
QString pathToQString(const std::filesystem::path &p) {
#ifdef _WIN32
    return QString::fromStdWString(p.wstring());
#else
    return QString::fromUtf8(p.string().c_str());
#endif
}

bool isKnownSettingKey(const QString &key) {
    // 1. Audio volume
    if (key == "volume" || key == "player/volume") return true;
    // 2. Playback quality
    if (key == "quality" || key == "player/quality") return true;
    // 3. Hide Shorts in feed
    if (key == "hideShorts" || key == "hide_shorts" || 
        key == "feed/hide_shorts" || key == "feed/hideShorts") return true;
    // 4. Allow AV1 video streams
    if (key == "allowAv1" || key == "allow_av1" || 
        key == "playback/allow_av1" || key == "playback/allowAv1") return true;
    // 5. Autoplay next video
    if (key == "autoplay" || key == "player/autoplay") return true;
    // 6. Theater mode
    if (key == "theaterMode" || key == "theater_mode" || 
        key == "player/theater_mode" || key == "player/theaterMode") return true;
    // 7. Window geometry
    if (key == "geometry" || key == "window/geometry") return true;
    // 8. Window state
    if (key == "windowState" || key == "window_state" || 
        key == "window/state" || key == "window/windowState") return true;
    // 9. Selected Channel Page ID
    if (key == "selectedChannelPageId" || key == "account/channel_page_id" || 
        key == "account/channelPageId" || key == "channelPageId") return true;
    // 10. Selected Auth User Index
    if (key == "selectedAuthUserIndex" || key == "account/auth_user_index" || 
        key == "account/authUserIndex" || key == "authUserIndex") return true;
    // 11. Selected Channel Name
    if (key == "selectedChannelName" || key == "account/channel_name" || 
        key == "account/channelName" || key == "channelName") return true;
    // 12. Log level
    if (key == "logLevel" || key == "log_level" || key == "app/log_level") return true;

    return false;
}
}  // namespace

AppSettings &AppSettings::instance() {
    static AppSettings s_instance;
    return s_instance;
}

AppSettings::AppSettings() {
    loadFromDisk();
}

AppSettings::~AppSettings() {
    sync();
}

void AppSettings::loadFromDisk() {
    auto settingsPath = lightyt::paths::getSettingsFilePath();
    bool iniExists = std::filesystem::exists(settingsPath);

    QSettings settings(pathToQString(settingsPath), QSettings::IniFormat);
    QSettings legacySettings("LightYT", "LightYT");

    if (iniExists) {
        const auto keys = settings.allKeys();
        for (const auto &k : keys) {
            m_cachedDiskKeys.insert(k);
        }
    } else {
        const auto keys = legacySettings.allKeys();
        for (const auto &k : keys) {
            m_cachedDiskKeys.insert(k);
        }
    }

    auto readValue = [&](const QString &key, const QVariant &defaultVal) -> QVariant {
        if (settings.contains(key)) {
            return settings.value(key);
        }
        if (!iniExists && legacySettings.contains(key)) {
            return legacySettings.value(key);
        }
        return defaultVal;
    };

    m_geometry = readValue("geometry", QByteArray()).toByteArray();
    m_windowState = readValue("windowState", QByteArray()).toByteArray();
    m_volume = std::clamp(readValue("volume", 100).toInt(), 0, 100);
    m_quality = readValue("quality", "1080p").toString();
    m_hideShorts = readValue("hideShorts", false).toBool();
    m_allowAv1 = readValue("allowAv1", false).toBool();
    m_autoplay = readValue("autoplay", true).toBool();
    m_theaterMode = readValue("theaterMode", false).toBool();

    m_selectedChannelPageId = readValue("selectedChannelPageId", QString()).toString();
    m_selectedAuthUserIndex = readValue("selectedAuthUserIndex", -1).toInt();
    m_selectedChannelName = readValue("selectedChannelName", QString()).toString();
    m_logLevel = readValue("logLevel", "warn").toString();

    // If migrated from legacy registry/format, ensure settings.ini is saved
    if (!iniExists && (legacySettings.contains("geometry") || legacySettings.contains("quality"))) {
        LOG_INFO("[AppSettings] Migrated legacy settings into settings.ini");
        m_dirty = true;
        saveToDiskLocked();
    }
}

void AppSettings::saveToDiskLocked() {
    if (!m_dirty)
        return;

    auto settingsPath = lightyt::paths::getSettingsFilePath();
    lightyt::paths::ensureDirExists(settingsPath.parent_path());

    QSettings settings(pathToQString(settingsPath), QSettings::IniFormat);
    settings.setValue("geometry", m_geometry);
    settings.setValue("windowState", m_windowState);
    settings.setValue("volume", m_volume);
    settings.setValue("quality", m_quality);
    settings.setValue("hideShorts", m_hideShorts);
    settings.setValue("allowAv1", m_allowAv1);
    settings.setValue("autoplay", m_autoplay);
    settings.setValue("theaterMode", m_theaterMode);
    settings.setValue("selectedChannelPageId", m_selectedChannelPageId);
    settings.setValue("selectedAuthUserIndex", m_selectedAuthUserIndex);
    settings.setValue("selectedChannelName", m_selectedChannelName);
    settings.setValue("logLevel", m_logLevel);
    settings.sync();

    m_cachedDiskKeys.insert("geometry");
    m_cachedDiskKeys.insert("windowState");
    m_cachedDiskKeys.insert("volume");
    m_cachedDiskKeys.insert("quality");
    m_cachedDiskKeys.insert("hideShorts");
    m_cachedDiskKeys.insert("allowAv1");
    m_cachedDiskKeys.insert("autoplay");
    m_cachedDiskKeys.insert("theaterMode");
    m_cachedDiskKeys.insert("selectedChannelPageId");
    m_cachedDiskKeys.insert("selectedAuthUserIndex");
    m_cachedDiskKeys.insert("selectedChannelName");
    m_cachedDiskKeys.insert("logLevel");

    m_dirty = false;
}

void AppSettings::sync() {
    std::lock_guard lock(m_mutex);
    saveToDiskLocked();
}

bool AppSettings::containsKey(const QString &key) const {
    std::lock_guard lock(m_mutex);
    return isKnownSettingKey(key) || m_cachedDiskKeys.contains(key);
}

bool AppSettings::hasPersistedKey(const QString &key) const {
    std::lock_guard lock(m_mutex);
    return m_cachedDiskKeys.contains(key);
}

QByteArray AppSettings::getGeometry() const {
    std::lock_guard lock(m_mutex);
    return m_geometry;
}

void AppSettings::setGeometry(const QByteArray &geometry) {
    std::lock_guard lock(m_mutex);
    if (m_geometry != geometry) {
        m_geometry = geometry;
        m_dirty = true;
    }
}

QByteArray AppSettings::getWindowState() const {
    std::lock_guard lock(m_mutex);
    return m_windowState;
}

void AppSettings::setWindowState(const QByteArray &state) {
    std::lock_guard lock(m_mutex);
    if (m_windowState != state) {
        m_windowState = state;
        m_dirty = true;
    }
}

int AppSettings::getVolume() const {
    std::lock_guard lock(m_mutex);
    return m_volume;
}

void AppSettings::setVolume(int volume) {
    int clamped = std::clamp(volume, 0, 100);
    std::lock_guard lock(m_mutex);
    if (m_volume != clamped) {
        m_volume = clamped;
        m_dirty = true;
    }
}

QString AppSettings::getQuality() const {
    std::lock_guard lock(m_mutex);
    return m_quality;
}

void AppSettings::setQuality(const QString &quality) {
    std::lock_guard lock(m_mutex);
    if (m_quality != quality) {
        m_quality = quality;
        m_dirty = true;
        saveToDiskLocked();
    }
}

bool AppSettings::getHideShorts() const {
    std::lock_guard lock(m_mutex);
    return m_hideShorts;
}

void AppSettings::setHideShorts(bool hide) {
    std::lock_guard lock(m_mutex);
    if (m_hideShorts != hide) {
        m_hideShorts = hide;
        m_dirty = true;
        saveToDiskLocked();
    }
}

bool AppSettings::getAllowAv1() const {
    std::lock_guard lock(m_mutex);
    return m_allowAv1;
}

void AppSettings::setAllowAv1(bool allow) {
    std::lock_guard lock(m_mutex);
    if (m_allowAv1 != allow) {
        m_allowAv1 = allow;
        m_dirty = true;
        saveToDiskLocked();
    }
}

bool AppSettings::getAutoplay() const {
    std::lock_guard lock(m_mutex);
    return m_autoplay;
}

void AppSettings::setAutoplay(bool autoplay) {
    std::lock_guard lock(m_mutex);
    if (m_autoplay != autoplay) {
        m_autoplay = autoplay;
        m_dirty = true;
        saveToDiskLocked();
    }
}

bool AppSettings::getTheaterMode() const {
    std::lock_guard lock(m_mutex);
    return m_theaterMode;
}

void AppSettings::setTheaterMode(bool theater) {
    std::lock_guard lock(m_mutex);
    if (m_theaterMode != theater) {
        m_theaterMode = theater;
        m_dirty = true;
        saveToDiskLocked();
    }
}

QString AppSettings::getSelectedChannelPageId() const {
    std::lock_guard lock(m_mutex);
    return m_selectedChannelPageId;
}

int AppSettings::getSelectedAuthUserIndex() const {
    std::lock_guard lock(m_mutex);
    return m_selectedAuthUserIndex;
}

QString AppSettings::getSelectedChannelName() const {
    std::lock_guard lock(m_mutex);
    return m_selectedChannelName;
}

void AppSettings::setSelectedChannel(const QString &pageId, int authUser, const QString &name) {
    std::lock_guard lock(m_mutex);
    if (m_selectedChannelPageId != pageId || m_selectedAuthUserIndex != authUser || m_selectedChannelName != name) {
        m_selectedChannelPageId = pageId;
        m_selectedAuthUserIndex = authUser;
        m_selectedChannelName = name;
        m_dirty = true;
        saveToDiskLocked();
    }
}

QString AppSettings::getLogLevel() const {
    std::lock_guard lock(m_mutex);
    return m_logLevel;
}

void AppSettings::setLogLevel(const QString &level) {
    std::lock_guard lock(m_mutex);
    if (m_logLevel != level) {
        m_logLevel = level;
        m_dirty = true;
        saveToDiskLocked();
    }
}
