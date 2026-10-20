#pragma once

#include "IMpvBackend.h"
#include "MpvApi.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace lightyt {

class LibMpvBackend : public IMpvBackend {
public:
    explicit LibMpvBackend(PlayerController *controller);
    ~LibMpvBackend() override;

    [[nodiscard]] bool isRunning() const override;
    [[nodiscard]] bool start(int64_t windowId) override;
    void stop() override;
    void loadFile(const std::string &url, const std::string &audioUrl = "", double startPos = 0.0) override;
    void command(const std::vector<std::string> &args) override;
    void setPropertyString(std::string_view name, std::string_view val) override;
    void setPropertyDouble(std::string_view name, double val) override;
    void setPropertyFlag(std::string_view name, bool val) override;
    void setVolume(int volume) override;
    void seek(double seconds, bool relative) override;

    [[nodiscard]] mpv_handle *getMpvHandle() const noexcept override { return m_mpv; }
    [[nodiscard]] bool hasRenderContext() const noexcept override;

private:
    void attachPendingAudio();
    void stopInternal();
    void eventLoop();

    mpv_handle *m_mpv = nullptr;
    PlayerController *m_controller = nullptr;
    std::atomic<bool> m_running{false};
    std::thread m_eventThread;
    std::mutex m_audioMutex;
    std::string m_pendingAudioUrl;
};

}  // namespace lightyt
