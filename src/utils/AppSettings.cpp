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
    settings.setValue("selectedChannelPageId", m_selectedChannelPageId);
    settings.setValue("selectedAuthUserIndex", m_selectedAuthUserIndex);
    settings.setValue("selectedChannelName", m_selectedChannelName);
    settings.setValue("logLevel", m_logLevel);
    settings.sync();
    m_dirty = false;
}

void AppSettings::sync() {
    std::lock_guard lock(m_mutex);
    saveToDiskLocked();
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
