#include "IpcMpvBackend.h"

#include "MpvApi.h"
#include "PlayerController.h"
#include "core/HttpClient.h"
#include "core/Process.h"
#include "utils/AppPaths.h"
#include "utils/Logger.h"
#include "utils/StringUtils.h"

#include <algorithm>
#include <array>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#else
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace lightyt {

IpcMpvBackend::IpcMpvBackend(PlayerController *controller) : m_controller(controller) {
#ifdef _WIN32
    m_writeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
#endif
}

IpcMpvBackend::~IpcMpvBackend() {
    stopInternal();
#ifdef _WIN32
    if (m_writeEvent != nullptr) {
        CloseHandle(m_writeEvent);
        m_writeEvent = nullptr;
    }
#endif
}

bool IpcMpvBackend::isRunning() const {
    return m_running.load();
}

bool IpcMpvBackend::start(int64_t windowId) {
#ifdef _WIN32
    std::string mpvExe = findFileInPaths("mpv.exe");
    if (mpvExe.empty()) {
        return false;
    }

    static std::atomic<uint64_t> s_pipeCounter{0};
    m_pipeName =
        "\\\\.\\pipe\\lightyt_mpv_" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(++s_pipeCounter);

    auto bufOpts = PlayerController::getStreamingBufferOptions();
    std::vector<std::string> args = {mpvExe,
                                     "--wid=" + std::to_string(windowId),
                                     "--vo=gpu",
                                     "--idle=yes",
                                     "--keep-open=yes",
                                     "--hwdec=auto-safe",
                                     "--force-window=yes",
                                     "--osc=no",
                                     "--osd-level=0",
                                     "--osd-bar=no",
                                     "--script-opts=osc-idle_screen=no",
                                     "--input-ipc-server=" + m_pipeName,
                                     "--ytdl=no",
                                     "--load-scripts=no",
                                     "--config=no",
                                     "--user-agent=" + std::string(DEFAULT_USER_AGENT),
                                     "--referrer=https://www.youtube.com/",
                                     "--http-header-fields=Origin: https://www.youtube.com",
                                     "--cache=" + std::string(bufOpts.cache),
                                     "--cache-pause-initial=" + std::string(bufOpts.cachePauseInitial),
                                     "--cache-pause-wait=" + std::string(bufOpts.cachePauseWait),
                                     "--cache-secs=" + std::string(bufOpts.cacheSecs),
                                     "--demuxer-max-bytes=" + std::string(bufOpts.demuxerMaxBytes),
                                     "--demuxer-max-back-bytes=" + std::string(bufOpts.demuxerMaxBackBytes),
                                     "--demuxer-readahead-secs=" + std::string(bufOpts.demuxerReadaheadSecs),
                                     "--demuxer-cache-wait=" + std::string(bufOpts.demuxerCacheWait),
                                     "--force-seekable=yes",
                                     "--stream-buffer-size=" + std::string(bufOpts.streamBufferSize),
                                     "--stream-lavf-o=" + std::string(bufOpts.streamLavfOpts),
                                     "--demuxer-donate-buffer=yes"};

    std::string cmd = lightyt::Process::buildCommandLine(args);
    std::wstring wCmd = utils::toWString(cmd);

    STARTUPINFOW si{};
    si.cb = sizeof(si);

    PROCESS_INFORMATION pi{};
    DWORD creationFlags = 0x08000000;  // CREATE_NO_WINDOW
    if (!CreateProcessW(nullptr, wCmd.data(), nullptr, nullptr, FALSE, creationFlags, nullptr, nullptr, &si, &pi)) {
        LOG_WARN("[PlayerController] Failed to spawn mpv.exe (Error: {})", GetLastError());
        return false;
    }

    m_hProcess = pi.hProcess;
    CloseHandle(pi.hThread);
    lightyt::Process::assignToJobObject(m_hProcess);

    DWORD exitCode = 0;
    DWORD lastErr = 0;
    std::wstring wPipeName = utils::toWString(m_pipeName);
    for (int i = 0; i < 30; ++i) {
        if (GetExitCodeProcess(m_hProcess, &exitCode) && exitCode != STILL_ACTIVE) {
            LOG_WARN("[PlayerController] mpv.exe terminated early (Exit code: {})", exitCode);
            break;
        }
        m_hPipe = CreateFileW(wPipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OVERLAPPED, nullptr);
        if (m_hPipe != INVALID_HANDLE_VALUE) {
            break;
        }
        lastErr = GetLastError();
        m_hPipe = nullptr;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    if (m_hPipe == nullptr || m_hPipe == INVALID_HANDLE_VALUE) {
        m_hPipe = nullptr;
        LOG_WARN("[PlayerController] Failed to open mpv named pipe (Error: {})", lastErr);
        stopInternal();
        return false;
    }

    m_hStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    m_running = true;
    m_readerThread = std::thread([this]() { readerLoop(); });
    observeProperties();
    LOG_INFO("[PlayerController] Initialized using embedded mpv.exe with named pipe IPC.");
    return true;
#else
    static std::atomic<uint64_t> s_ipcCounter{0};
    std::string runtimeDir;
    const char *xdg = std::getenv("XDG_RUNTIME_DIR");
    if (xdg != nullptr && xdg[0] != '\0') {
        runtimeDir = xdg;
    } else {
        runtimeDir = paths::getAppDataDir().string();
        paths::ensureDirExists(runtimeDir);
    }

    auto ts = std::chrono::steady_clock::now().time_since_epoch().count();
    m_pipeName = runtimeDir + "/lightyt_mpv_" + std::to_string(getpid()) + "_" + std::to_string(ts) + "_" +
                 std::to_string(++s_ipcCounter) + ".sock";
    unlink(m_pipeName.c_str());

    if (pipe2(m_stopPipe, O_CLOEXEC) < 0) {
        m_stopPipe[0] = -1;
        m_stopPipe[1] = -1;
    }

    // Pre-allocate all arguments before calling fork() to guarantee async-signal-safety
    std::string widStr = std::to_string(windowId);
    std::string widArg = "--wid=" + widStr;
    std::string ipcArg = "--input-ipc-server=" + m_pipeName;
    std::string userAgentArg = "--user-agent=" + std::string(DEFAULT_USER_AGENT);
    auto bufOpts = PlayerController::getStreamingBufferOptions();
    std::string cachePauseInitialArg = std::string("--cache-pause-initial=") + bufOpts.cachePauseInitial;
    std::string cachePauseWaitArg = std::string("--cache-pause-wait=") + bufOpts.cachePauseWait;
    std::string cacheSecsArg = std::string("--cache-secs=") + bufOpts.cacheSecs;
    std::string demuxerMaxBytesArg = std::string("--demuxer-max-bytes=") + bufOpts.demuxerMaxBytes;
    std::string demuxerMaxBackBytesArg = std::string("--demuxer-max-back-bytes=") + bufOpts.demuxerMaxBackBytes;
    std::string demuxerReadaheadSecsArg = std::string("--demuxer-readahead-secs=") + bufOpts.demuxerReadaheadSecs;
    std::string demuxerCacheWaitArg = std::string("--demuxer-cache-wait=") + bufOpts.demuxerCacheWait;
    std::string streamBufferSizeArg = std::string("--stream-buffer-size=") + bufOpts.streamBufferSize;
    std::string streamLavfOptsArg = std::string("--stream-lavf-o=") + bufOpts.streamLavfOpts;

    auto exeDir = paths::getExecutableDir();
    std::string mpvBin = "mpv";
    const std::vector<std::filesystem::path> localMpvCandidates = {
        exeDir / "lib" / "mpv", exeDir.parent_path() / "lib" / "mpv", std::filesystem::path("lib") / "mpv",
        exeDir / "mpv"};
    auto itMpv =
        std::ranges::find_if(localMpvCandidates, [](const auto &cand) { return access(cand.c_str(), X_OK) == 0; });
    if (itMpv != localMpvCandidates.end()) {
        mpvBin = itMpv->string();
    }

    std::vector<const char *> argv = {mpvBin.c_str(),
                                      widArg.c_str(),
                                      "--idle=yes",
                                      "--keep-open=yes",
                                      "--vo=gpu",
                                      "--hwdec=auto-safe",
                                      "--force-window=yes",
                                      "--osc=no",
                                      "--osd-level=0",
                                      "--osd-bar=no",
                                      "--script-opts=osc-idle_screen=no",
                                      ipcArg.c_str(),
                                      "--ytdl=no",
                                      "--load-scripts=no",
                                      "--config=no",
                                      userAgentArg.c_str(),
                                      "--referrer=https://www.youtube.com/",
                                      "--http-header-fields=Origin: https://www.youtube.com",
                                      "--cache=yes",
                                      cachePauseInitialArg.c_str(),
                                      cachePauseWaitArg.c_str(),
                                      cacheSecsArg.c_str(),
                                      demuxerMaxBytesArg.c_str(),
                                      demuxerMaxBackBytesArg.c_str(),
                                      demuxerReadaheadSecsArg.c_str(),
                                      demuxerCacheWaitArg.c_str(),
                                      "--force-seekable=yes",
                                      streamBufferSizeArg.c_str(),
                                      streamLavfOptsArg.c_str(),
                                      "--demuxer-donate-buffer=yes"};
    argv.push_back(nullptr);
    char *const *rawArgv = const_cast<char *const *>(argv.data());

    pid_t pid = fork();
    if (pid < 0) {
        LOG_WARN("[PlayerController] Failed to fork mpv process.");
        return false;
    }

    if (pid == 0) {
        // Child mpv process: async-signal-safe operations only (zero heap allocations)
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        if (getppid() == 1) {
            _exit(1);
        }
        setpgid(0, 0);

        int devNull = open("/dev/null", O_RDWR);
        if (devNull >= 0) {
            dup2(devNull, STDIN_FILENO);
            dup2(devNull, STDOUT_FILENO);
            dup2(devNull, STDERR_FILENO);
            close(devNull);
        }

        execvp(rawArgv[0], rawArgv);
        _exit(127);
    }

    setpgid(pid, pid);
    m_childPid = pid;

    m_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_sock < 0) {
        stopInternal();
        return false;
    }

    struct sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, m_pipeName.c_str(), sizeof(addr.sun_path) - 1);

    bool connected = false;
    for (int i = 0; i < 40; ++i) {
        if (connect(m_sock, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) == 0) {
            connected = true;
            break;
        }
        int status = 0;
        if (waitpid(m_childPid, &status, WNOHANG) > 0) {
            m_childPid = -1;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    if (!connected) {
        close(m_sock);
        m_sock = -1;
        stopInternal();
        return false;
    }

    m_running = true;
    m_readerThread = std::thread([this]() { readerLoop(); });
    observeProperties();
    LOG_INFO("[PlayerController] Initialized using embedded mpv process with unix socket IPC.");
    return true;
#endif
}

void IpcMpvBackend::stop() {
    {
        std::lock_guard lock(m_audioMutex);
        m_pendingAudioUrl.clear();
    }
    sendJsonCommand("{\"command\": [\"stop\"]}\n");
}

void IpcMpvBackend::loadFile(const std::string &url, const std::string &audioUrl, double startPos) {
    // mpv 0.38+ compatibility: set the 'start' property before loadfile
    if (startPos > 0.0) {
        setPropertyString("start", std::to_string(startPos));
    } else {
        setPropertyString("start", "none");
    }

    {
        std::lock_guard lock(m_audioMutex);
        m_pendingAudioUrl = audioUrl;
    }

    nlohmann::json cmd;
    cmd["command"] = nlohmann::json::array({"loadfile", url, "replace"});
    sendJsonCommand(cmd.dump() + "\n");
}

void IpcMpvBackend::command(const std::vector<std::string> &args) {
    nlohmann::json cmd;
    cmd["command"] = args;
    sendJsonCommand(cmd.dump() + "\n");
}

void IpcMpvBackend::setPropertyString(std::string_view name, std::string_view val) {
    nlohmann::json cmd;
    cmd["command"] = {"set_property", std::string(name), std::string(val)};
    sendJsonCommand(cmd.dump() + "\n");
}

void IpcMpvBackend::setPropertyDouble(std::string_view name, double val) {
    nlohmann::json cmd;
    cmd["command"] = {"set_property", std::string(name), val};
    sendJsonCommand(cmd.dump() + "\n");
}

void IpcMpvBackend::setPropertyFlag(std::string_view name, bool val) {
    nlohmann::json cmd;
    cmd["command"] = {"set_property", std::string(name), val};
    sendJsonCommand(cmd.dump() + "\n");
}

void IpcMpvBackend::setVolume(int volume) {
    setPropertyDouble("volume", static_cast<double>(volume));
}

void IpcMpvBackend::seek(double seconds, bool relative) {
    nlohmann::json cmd;
    cmd["command"] = {"seek", seconds, relative ? "relative" : "absolute"};
    sendJsonCommand(cmd.dump() + "\n");
}

std::string IpcMpvBackend::getPropertyString(std::string_view name) const {
    std::lock_guard lock(m_propMutex);
    auto it = m_stringProps.find(std::string(name));
    return (it != m_stringProps.end()) ? it->second : std::string{};
}

double IpcMpvBackend::getPropertyDouble(std::string_view name, double defaultVal) const {
    std::lock_guard lock(m_propMutex);
    auto it = m_doubleProps.find(std::string(name));
    return (it != m_doubleProps.end()) ? it->second : defaultVal;
}

int64_t IpcMpvBackend::getPropertyInt(std::string_view name, int64_t defaultVal) const {
    std::lock_guard lock(m_propMutex);
    auto it = m_intProps.find(std::string(name));
    if (it != m_intProps.end()) {
        return it->second;
    }
    auto itD = m_doubleProps.find(std::string(name));
    if (itD != m_doubleProps.end()) {
        return static_cast<int64_t>(itD->second);
    }
    return defaultVal;
}

void IpcMpvBackend::attachPendingAudio() {
    std::string audio;
    {
        std::lock_guard lock(m_audioMutex);
        audio = std::move(m_pendingAudioUrl);
        m_pendingAudioUrl.clear();
    }
    if (!audio.empty()) {
        LOG_INFO("[PlayerController] IPC media loaded. Attaching audio stream...");
        nlohmann::json audioCmd;
        audioCmd["command"] = nlohmann::json::array({"audio-add", audio, "select"});
        sendJsonCommand(audioCmd.dump() + "\n");
    }
}

void IpcMpvBackend::stopInternal() {
    m_running = false;
    {
        std::lock_guard lock(m_audioMutex);
        m_pendingAudioUrl.clear();
    }
    sendJsonCommand("{\"command\": [\"quit\"]}\n");

#ifdef _WIN32
    if (m_hStopEvent != nullptr) {
        SetEvent(m_hStopEvent);
    }
    if (m_hPipe != nullptr) {
        CancelIo(m_hPipe);
        CloseHandle(m_hPipe);
        m_hPipe = nullptr;
    }
    if (m_readerThread.joinable()) {
        m_readerThread.join();
    }
    if (m_hStopEvent != nullptr) {
        CloseHandle(m_hStopEvent);
        m_hStopEvent = nullptr;
    }
    if (m_hProcess != nullptr) {
        WaitForSingleObject(m_hProcess, 500);
        TerminateProcess(m_hProcess, 0);
        CloseHandle(m_hProcess);
        m_hProcess = nullptr;
    }
#else
    if (m_stopPipe[1] >= 0) {
        char b = 1;
        [[maybe_unused]] auto res = write(m_stopPipe[1], &b, 1);
    }
    if (m_sock >= 0) {
        shutdown(m_sock, SHUT_RDWR);
        close(m_sock);
        m_sock = -1;
    }
    if (m_readerThread.joinable()) {
        m_readerThread.join();
    }
    if (m_stopPipe[0] >= 0) {
        close(m_stopPipe[0]);
        close(m_stopPipe[1]);
        m_stopPipe[0] = -1;
        m_stopPipe[1] = -1;
    }
    if (!m_pipeName.empty()) {
        unlink(m_pipeName.c_str());
    }

    if (m_childPid > 0) {
        int status = 0;
        pid_t res = waitpid(m_childPid, &status, WNOHANG);
        if (res == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            res = waitpid(m_childPid, &status, WNOHANG);
            if (res == 0) {
                kill(-m_childPid, SIGTERM);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                res = waitpid(m_childPid, &status, WNOHANG);
                if (res == 0) {
                    kill(-m_childPid, SIGKILL);
                }
            }
        }
        waitpid(m_childPid, nullptr, 0);
        m_childPid = -1;
    }
#endif
}

void IpcMpvBackend::observeProperties() {
    setPropertyString("curl-max-request-size", PlayerController::getStreamingBufferOptions().curlMaxRequestSize);
    sendJsonCommand("{\"command\": [\"observe_property\", 1, \"time-pos\"]}\n");
    sendJsonCommand("{\"command\": [\"observe_property\", 2, \"duration\"]}\n");
    sendJsonCommand("{\"command\": [\"observe_property\", 3, \"pause\"]}\n");
    sendJsonCommand("{\"command\": [\"observe_property\", 4, \"paused-for-cache\"]}\n");
    sendJsonCommand("{\"command\": [\"observe_property\", 5, \"demuxer-cache-duration\"]}\n");
    sendJsonCommand("{\"command\": [\"observe_property\", 6, \"eof-reached\"]}\n");
    sendJsonCommand("{\"command\": [\"observe_property\", 7, \"width\"]}\n");
    sendJsonCommand("{\"command\": [\"observe_property\", 8, \"height\"]}\n");
    sendJsonCommand("{\"command\": [\"observe_property\", 9, \"container-fps\"]}\n");
    sendJsonCommand("{\"command\": [\"observe_property\", 10, \"frame-drop-count\"]}\n");
    sendJsonCommand("{\"command\": [\"observe_property\", 11, \"decoder-frame-drop-count\"]}\n");
    sendJsonCommand("{\"command\": [\"observe_property\", 12, \"video-codec\"]}\n");
    sendJsonCommand("{\"command\": [\"observe_property\", 13, \"audio-codec\"]}\n");
    sendJsonCommand("{\"command\": [\"observe_property\", 14, \"colormatrix\"]}\n");
    sendJsonCommand("{\"command\": [\"observe_property\", 15, \"primaries\"]}\n");
    sendJsonCommand("{\"command\": [\"observe_property\", 16, \"video-bitrate\"]}\n");
    sendJsonCommand("{\"command\": [\"observe_property\", 17, \"audio-bitrate\"]}\n");
    sendJsonCommand("{\"command\": [\"observe_property\", 18, \"cache-speed\"]}\n");
}

void IpcMpvBackend::sendJsonCommand(std::string_view cmd) {
    std::lock_guard lock(m_writeMutex);
#ifdef _WIN32
    if (m_hPipe != nullptr && m_hPipe != INVALID_HANDLE_VALUE) {
        OVERLAPPED ovWrite{};
        if (m_writeEvent != nullptr) {
            ResetEvent(m_writeEvent);
        }
        ovWrite.hEvent = m_writeEvent;
        DWORD written = 0;
        BOOL ok = WriteFile(m_hPipe, cmd.data(), static_cast<DWORD>(cmd.size()), &written, &ovWrite);
        if (!ok && GetLastError() == ERROR_IO_PENDING) {
            GetOverlappedResult(m_hPipe, &ovWrite, &written, TRUE);
        }
    }
#else
    if (m_sock >= 0) {
        send(m_sock, cmd.data(), cmd.size(), MSG_NOSIGNAL);
    }
#endif
}

void IpcMpvBackend::readerLoop() {
    std::string buffer;
    std::array<char, 4096> chunk{};

#ifdef _WIN32
    OVERLAPPED ovRead{};
    ovRead.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE waitHandles[2] = {m_hStopEvent, ovRead.hEvent};

    while (m_running && m_hPipe != nullptr) {
        DWORD readBytes = 0;
        ResetEvent(ovRead.hEvent);
        BOOL ok = ReadFile(m_hPipe, chunk.data(), static_cast<DWORD>(chunk.size() - 1), &readBytes, &ovRead);
        if (!ok) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                DWORD waitRes = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
                if (waitRes == WAIT_OBJECT_0) {
                    // Stop event signaled
                    CancelIo(m_hPipe);
                    break;
                } else if (waitRes == WAIT_OBJECT_0 + 1) {
                    if (!GetOverlappedResult(m_hPipe, &ovRead, &readBytes, FALSE)) {
                        break;
                    }
                } else {
                    break;
                }
            } else {
                break;
            }
        }

        if (readBytes > 0) {
            buffer.append(chunk.data(), readBytes);
            size_t nl = 0;
            while ((nl = buffer.find('\n')) != std::string::npos) {
                std::string line = buffer.substr(0, nl);
                buffer.erase(0, nl + 1);
                handleIpcMessage(line);
            }
        }
    }
    CloseHandle(ovRead.hEvent);
#else
    struct pollfd pfds[2] = {{m_stopPipe[0], POLLIN, 0}, {m_sock, POLLIN, 0}};

    while (m_running && m_sock >= 0) {
        int ret = poll(pfds, 2, -1);
        if (ret <= 0) {
            if (ret < 0 && errno == EINTR)
                continue;
            break;
        }

        if (pfds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            // Stop pipe signaled
            break;
        }

        if (pfds[1].revents & (POLLIN | POLLHUP)) {
            ssize_t n = recv(m_sock, chunk.data(), chunk.size() - 1, 0);
            if (n <= 0)
                break;

            buffer.append(chunk.data(), static_cast<size_t>(n));
            size_t nl = 0;
            while ((nl = buffer.find('\n')) != std::string::npos) {
                std::string line = buffer.substr(0, nl);
                buffer.erase(0, nl + 1);
                handleIpcMessage(line);
            }
        }
    }
#endif
}

void IpcMpvBackend::handleIpcMessage(std::string_view line) {
    try {
        auto j = nlohmann::json::parse(line);
        if (j.contains("event")) {
            std::string ev = j["event"].get<std::string>();
            if (ev == "property-change") {
                std::string name = j.value("name", "");
                if (name == "time-pos" && j.contains("data") && j["data"].is_number()) {
                    double pos = j["data"].get<double>();
                    m_controller->notifyTime(pos, m_controller->getDuration());
                } else if (name == "duration" && j.contains("data") && j["data"].is_number()) {
                    double dur = j["data"].get<double>();
                    m_controller->notifyTime(m_controller->getPosition(), dur);
                } else if (name == "pause" && j.contains("data") && j["data"].is_boolean()) {
                    bool paused = j["data"].get<bool>();
                    m_controller->notifyPause(paused);
                } else if (name == "paused-for-cache" && j.contains("data") && j["data"].is_boolean()) {
                    bool buffering = j["data"].get<bool>();
                    if (buffering) {
                        m_controller->setState(PlaybackState::Buffering);
                    } else if (!m_controller->isPaused()) {
                        m_controller->setState(PlaybackState::Playing);
                    }
                } else if (name == "demuxer-cache-duration" && j.contains("data") && j["data"].is_number()) {
                    double cached = j["data"].get<double>();
                    m_controller->notifyCache(cached);
                } else if (name == "eof-reached" && j.contains("data") && j["data"].is_boolean()) {
                    bool eofReached = j["data"].get<bool>();
                    if (eofReached) {
                        m_controller->setState(PlaybackState::Ended);
                    }
                }

                if (j.contains("data")) {
                    const auto &val = j["data"];
                    std::lock_guard lock(m_propMutex);
                    if (val.is_string()) {
                        m_stringProps[name] = val.get<std::string>();
                    } else if (val.is_number_integer()) {
                        m_intProps[name] = val.get<int64_t>();
                        m_doubleProps[name] = static_cast<double>(val.get<int64_t>());
                    } else if (val.is_number()) {
                        m_doubleProps[name] = val.get<double>();
                    }
                }
            } else if (ev == "file-loaded") {
                attachPendingAudio();
            } else if (ev == "playback-restart") {
                attachPendingAudio();
                if (!m_controller->isPaused()) {
                    m_controller->setState(PlaybackState::Playing);
                }
            } else if (ev == "end-file") {
                std::string reason = j.value("reason", "");
                if (reason == "eof") {
                    m_controller->setState(PlaybackState::Ended);
                } else if (reason == "error") {
                    m_controller->setState(PlaybackState::Error);
                    m_controller->notifyError("Playback error encountered.");
                }
            }
        }
    } catch (const std::exception &e) {
        LOG_DEBUG("[PlayerController] Malformed IPC payload discarded: {}", e.what());
    }
}

}  // namespace lightyt
