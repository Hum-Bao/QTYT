#pragma once

#include "IMpvBackend.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#endif

namespace lightyt {

class IpcMpvBackend : public IMpvBackend {
public:
    explicit IpcMpvBackend(PlayerController *controller);
    ~IpcMpvBackend() override;

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

private:
    void attachPendingAudio();
    void stopInternal();
    void observeProperties();
    void sendJsonCommand(std::string_view cmd);
    void readerLoop();
    void handleIpcMessage(std::string_view line);

    PlayerController *m_controller = nullptr;
    std::string m_pipeName;
    std::atomic<bool> m_running{false};
    std::thread m_readerThread;

#ifdef _WIN32
    HANDLE m_hProcess = nullptr;
    HANDLE m_hPipe = nullptr;
    HANDLE m_hStopEvent = nullptr;
    std::mutex m_writeMutex;
#else
    int m_sock = -1;
    int m_stopPipe[2] = {-1, -1};
    pid_t m_childPid = -1;
    std::mutex m_writeMutex;
#endif

    std::mutex m_audioMutex;
    std::string m_pendingAudioUrl;
};

}  // namespace lightyt
