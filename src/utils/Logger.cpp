#include "Logger.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace lightyt::log {

std::string_view levelToString(Level level) noexcept {
    switch (level) {
    case Level::Trace:
        return "trace";
    case Level::Debug:
        return "debug";
    case Level::Info:
        return "info";
    case Level::Warn:
        return "warn";
    case Level::Error:
        return "error";
    case Level::None:
        return "none";
    }
    return "warn";
}

Level stringToLevel(std::string_view str, Level defaultLevel) noexcept {
    if (str.empty()) {
        return defaultLevel;
    }
    std::string val(str);
    std::ranges::for_each(val, [](char &c) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });

    if (val == "trace") {
        return Level::Trace;
    }
    if (val == "debug" || val == "1" || val == "true") {
        return Level::Debug;
    }
    if (val == "info") {
        return Level::Info;
    }
    if (val == "warn" || val == "warning") {
        return Level::Warn;
    }
    if (val == "error") {
        return Level::Error;
    }
    if (val == "none" || val == "off" || val == "0") {
        return Level::None;
    }
    return defaultLevel;
}

namespace {
const char *levelToPrefix(Level level) noexcept {
    switch (level) {
    case Level::Trace:
        return "TRACE";
    case Level::Debug:
        return "DEBUG";
    case Level::Info:
        return "INFO ";
    case Level::Warn:
        return "WARN ";
    case Level::Error:
        return "ERROR";
    case Level::None:
        return "NONE ";
    }
    return "UNKN ";
}

Level parseEnvLevel() noexcept {
    const char *env = std::getenv("LIGHTYT_LOG");
    if (env == nullptr || env[0] == '\0') {
        return Level::Warn;
    }
    return stringToLevel(env, Level::Warn);
}
}  // namespace

Logger &Logger::instance() {
    static Logger s_instance;
    return s_instance;
}

Logger::Logger() : m_level(parseEnvLevel()) {}

Logger::~Logger() {
    std::lock_guard lock(m_mutex);
    if (m_fileHandle != nullptr) {
        std::fclose(static_cast<FILE *>(m_fileHandle));
        m_fileHandle = nullptr;
    }
}

void Logger::setLogFile(std::string_view path) {
    std::lock_guard lock(m_mutex);
    if (m_fileHandle != nullptr) {
        std::fclose(static_cast<FILE *>(m_fileHandle));
        m_fileHandle = nullptr;
    }
    m_filePath = std::string(path);
    if (!m_filePath.empty()) {
        m_fileHandle = std::fopen(m_filePath.c_str(), "a");
        if (m_fileHandle == nullptr) {
            std::fprintf(stderr, "[Logger] Failed to open log file: %s\n", m_filePath.c_str());
        }
    }
}

void Logger::write(Level level, std::string_view message) {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm tmBuf{};
#ifdef _WIN32
    localtime_s(&tmBuf, &tt);
#else
    localtime_r(&tt, &tmBuf);
#endif

    char timeStr[32];
    std::snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d.%03d", tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec,
                  static_cast<int>(ms.count()));

    std::string formatted = std::format("[{}] [{}] {}\n", timeStr, levelToPrefix(level), message);

    std::lock_guard lock(m_mutex);
    // Write to stderr / stdout
    FILE *stream = (level >= Level::Warn) ? stderr : stdout;
    std::fwrite(formatted.data(), 1, formatted.size(), stream);
    if (level >= Level::Warn) {
        std::fflush(stream);
    }

    // Optional file logging
    if (m_fileHandle != nullptr) {
        std::fwrite(formatted.data(), 1, formatted.size(), static_cast<FILE *>(m_fileHandle));
        std::fflush(static_cast<FILE *>(m_fileHandle));
    }
}

void initFromEnv() {
    Logger::instance().setLevel(parseEnvLevel());
}

}  // namespace lightyt::log
