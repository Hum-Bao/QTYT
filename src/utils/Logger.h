#pragma once

#include <atomic>
#include <chrono>
#include <format>
#include <mutex>
#include <string>
#include <string_view>

namespace lightyt::log {

enum class Level : uint8_t { Trace = 0, Debug, Info, Warn, Error, None };

[[nodiscard]] std::string_view levelToString(Level level) noexcept;
[[nodiscard]] Level stringToLevel(std::string_view str, Level defaultLevel = Level::Warn) noexcept;

class Logger {
public:
    [[nodiscard]] static Logger &instance();

    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

    void setLevel(Level level) noexcept { m_level.store(level, std::memory_order_relaxed); }

    [[nodiscard]] Level getLevel() const noexcept { return m_level.load(std::memory_order_relaxed); }

    void setLogFile(std::string_view path);

    template<typename... Args>
    void log(Level level, std::format_string<Args...> fmt, Args &&...args) {
        if (level < m_level.load(std::memory_order_relaxed) || level == Level::None) {
            return;
        }
        std::string msg;
        try {
            msg = std::format(fmt, std::forward<Args>(args)...);
        } catch (const std::exception &e) {
            msg = std::string("Format error: ") + e.what();
        }
        write(level, msg);
    }

private:
    Logger();
    ~Logger();

    void write(Level level, std::string_view message);

    std::atomic<Level> m_level{Level::Warn};
    std::mutex m_mutex;
    std::string m_filePath;
    void *m_fileHandle = nullptr;
};

// Convenience helper
void initFromEnv();

}  // namespace lightyt::log

#define LOG_TRACE(...) ::lightyt::log::Logger::instance().log(::lightyt::log::Level::Trace, __VA_ARGS__)
#define LOG_DEBUG(...) ::lightyt::log::Logger::instance().log(::lightyt::log::Level::Debug, __VA_ARGS__)
#define LOG_INFO(...) ::lightyt::log::Logger::instance().log(::lightyt::log::Level::Info, __VA_ARGS__)
#define LOG_WARN(...) ::lightyt::log::Logger::instance().log(::lightyt::log::Level::Warn, __VA_ARGS__)
#define LOG_ERROR(...) ::lightyt::log::Logger::instance().log(::lightyt::log::Level::Error, __VA_ARGS__)
