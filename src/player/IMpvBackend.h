#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class PlayerController;

struct mpv_handle;

class IMpvBackend {
public:
    virtual ~IMpvBackend() = default;
    [[nodiscard]] virtual bool start(int64_t windowId) = 0;
    virtual void stop() = 0;
    [[nodiscard]] virtual bool isRunning() const = 0;
    virtual void loadFile(const std::string &url, const std::string &audioUrl = "", double startPos = 0.0) = 0;
    virtual void command(const std::vector<std::string> &args) = 0;
    virtual void setPropertyString(std::string_view name, std::string_view val) = 0;
    virtual void setPropertyDouble(std::string_view name, double val) = 0;
    virtual void setPropertyFlag(std::string_view name, bool val) = 0;
    virtual void setVolume(int volume) = 0;
    virtual void seek(double seconds, bool relative) = 0;
    [[nodiscard]] virtual mpv_handle *getMpvHandle() const noexcept { return nullptr; }
    [[nodiscard]] virtual bool hasRenderContext() const noexcept { return false; }
};
