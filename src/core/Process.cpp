#include "Process.h"

#include "utils/Logger.h"

#include <array>
#include <chrono>
#include <thread>
#include <vector>

namespace lightyt {

std::string Process::buildCommandLine(const std::vector<std::string> &args) {
    std::string cmd;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            cmd += ' ';
        }
        const auto &arg = args[i];
        bool needQuotes = arg.empty() || arg.find(' ') != std::string::npos || arg.find('\t') != std::string::npos ||
                          arg.find('"') != std::string::npos;
        if (!needQuotes) {
            cmd += arg;
            continue;
        }
        cmd += '"';
        size_t backslashes = 0;
        for (char c : arg) {
            if (c == '\\') {
                ++backslashes;
            } else if (c == '"') {
                cmd.append(backslashes * 2 + 1, '\\');
                cmd += '"';
                backslashes = 0;
            } else {
                cmd.append(backslashes, '\\');
                cmd += c;
                backslashes = 0;
            }
        }
        cmd.append(backslashes * 2, '\\');
        cmd += '"';
    }
    return cmd;
}

}  // namespace lightyt

#ifdef _WIN32
#include "utils/StringUtils.h"

#include <mutex>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>

namespace {
HANDLE g_hJobObject = nullptr;

void ensureJobObject() {
    static std::once_flag s_jobOnce;
    std::call_once(s_jobOnce, []() {
        g_hJobObject = CreateJobObjectW(nullptr, nullptr);
        if (g_hJobObject != nullptr) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
            jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(g_hJobObject, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
        }
    });
}

class ScopedHandle {
public:
    explicit ScopedHandle(HANDLE h = INVALID_HANDLE_VALUE) noexcept : m_handle(h) {}
    ~ScopedHandle() noexcept { reset(); }

    ScopedHandle(const ScopedHandle &) = delete;
    ScopedHandle &operator=(const ScopedHandle &) = delete;
    ScopedHandle(ScopedHandle &&other) noexcept : m_handle(other.m_handle) { other.m_handle = INVALID_HANDLE_VALUE; }
    ScopedHandle &operator=(ScopedHandle &&other) noexcept {
        if (this != &other) {
            reset();
            m_handle = other.m_handle;
            other.m_handle = INVALID_HANDLE_VALUE;
        }
        return *this;
    }

    void reset(HANDLE h = INVALID_HANDLE_VALUE) noexcept {
        if (m_handle != INVALID_HANDLE_VALUE && m_handle != nullptr) {
            CloseHandle(m_handle);
        }
        m_handle = h;
    }

    [[nodiscard]] HANDLE get() const noexcept { return m_handle; }
    [[nodiscard]] HANDLE *put() noexcept {
        reset();
        return &m_handle;
    }

private:
    HANDLE m_handle = INVALID_HANDLE_VALUE;
};

using lightyt::utils::toWString;

std::wstring buildCommandLineW(const std::vector<std::string> &args) {
    return toWString(lightyt::Process::buildCommandLine(args));
}
}  // namespace

namespace lightyt {

void Process::initProcessGroup() {
    ensureJobObject();
}

void Process::assignToJobObject(void *processHandle) {
    ensureJobObject();
    if (g_hJobObject != nullptr && processHandle != nullptr && processHandle != INVALID_HANDLE_VALUE) {
        AssignProcessToJobObject(g_hJobObject, static_cast<HANDLE>(processHandle));
    }
}

ProcessResult Process::run(const std::vector<std::string> &args, std::chrono::milliseconds timeout,
                           const std::atomic<bool> *cancelToken) {
    if (args.empty()) {
        return {};
    }
    return run(args[0], args, timeout, cancelToken);
}

ProcessResult Process::run(std::string_view program, const std::vector<std::string> &args,
                           std::chrono::milliseconds timeout, const std::atomic<bool> *cancelToken) {
    ensureJobObject();
    ProcessResult res;
    if (args.empty()) {
        return res;
    }

    ScopedHandle hStdOutRead;
    ScopedHandle hStdOutWrite;
    ScopedHandle hStdErrRead;
    ScopedHandle hStdErrWrite;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;

    if (!CreatePipe(hStdOutRead.put(), hStdOutWrite.put(), &sa, 0)) {
        return res;
    }
    SetHandleInformation(hStdOutRead.get(), HANDLE_FLAG_INHERIT, 0);

    if (!CreatePipe(hStdErrRead.put(), hStdErrWrite.put(), &sa, 0)) {
        return res;
    }
    SetHandleInformation(hStdErrRead.get(), HANDLE_FLAG_INHERIT, 0);

    // Isolate handle inheritance using STARTUPINFOEXW to avoid leaking pipes across threads
    SIZE_T attrListSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrListSize);
    std::vector<BYTE> attrListBuffer(attrListSize);
    auto pAttrList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrListBuffer.data());
    BOOL attrInitOk = InitializeProcThreadAttributeList(pAttrList, 1, 0, &attrListSize);

    HANDLE inheritHandles[] = {hStdOutWrite.get(), hStdErrWrite.get()};
    STARTUPINFOEXW siex{};
    siex.StartupInfo.cb = sizeof(STARTUPINFOEXW);
    siex.StartupInfo.hStdOutput = hStdOutWrite.get();
    siex.StartupInfo.hStdError = hStdErrWrite.get();
    siex.StartupInfo.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    siex.StartupInfo.wShowWindow = SW_HIDE;

    DWORD creationFlags = CREATE_NO_WINDOW;
    if (attrInitOk && UpdateProcThreadAttribute(pAttrList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inheritHandles,
                                                sizeof(inheritHandles), nullptr, nullptr)) {
        siex.lpAttributeList = pAttrList;
        creationFlags |= EXTENDED_STARTUPINFO_PRESENT;
    }

    PROCESS_INFORMATION pi{};
    std::wstring cmdLineW = buildCommandLineW(args);

    BOOL created = CreateProcessW(nullptr, cmdLineW.data(), nullptr, nullptr, TRUE, creationFlags, nullptr, nullptr,
                                  &siex.StartupInfo, &pi);

    if (attrInitOk) {
        DeleteProcThreadAttributeList(pAttrList);
    }

    if (!created) {
        LOG_WARN("[Process] Failed to spawn process '{}' (Error: {})", program, GetLastError());
        return res;
    }

    ScopedHandle hProcess(pi.hProcess);
    ScopedHandle hThread(pi.hThread);

    // Create a dedicated sub-job object for this invocation to guarantee PyInstaller child termination on
    // timeout/cancellation
    ScopedHandle hChildJob(CreateJobObjectW(nullptr, nullptr));
    if (hChildJob.get() != nullptr) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(hChildJob.get(), JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
        AssignProcessToJobObject(hChildJob.get(), hProcess.get());
    }

    if (g_hJobObject != nullptr) {
        AssignProcessToJobObject(g_hJobObject, hProcess.get());
    }

    // Close pipe write ends in parent so ReadFile hits EOF when process terminates
    hStdOutWrite.reset();
    hStdErrWrite.reset();

    std::string stdOutData;
    std::string stdErrData;

    std::thread outThread([hPipe = hStdOutRead.get(), &stdOutData]() {
        std::array<char, 65536> buf{};
        DWORD bytesRead = 0;
        while (ReadFile(hPipe, buf.data(), static_cast<DWORD>(buf.size()), &bytesRead, nullptr) && bytesRead > 0) {
            stdOutData.append(buf.data(), bytesRead);
        }
    });

    std::thread errThread([hPipe = hStdErrRead.get(), &stdErrData]() {
        std::array<char, 65536> buf{};
        DWORD bytesRead = 0;
        while (ReadFile(hPipe, buf.data(), static_cast<DWORD>(buf.size()), &bytesRead, nullptr) && bytesRead > 0) {
            stdErrData.append(buf.data(), bytesRead);
        }
    });

    auto startTime = std::chrono::steady_clock::now();

    while (true) {
        if (cancelToken != nullptr && cancelToken->load()) {
            res.cancelled = true;
            if (hChildJob.get() != nullptr) {
                TerminateJobObject(hChildJob.get(), 1);
            } else {
                TerminateProcess(hProcess.get(), 1);
            }
            break;
        }

        if (std::chrono::steady_clock::now() - startTime > timeout) {
            res.timedOut = true;
            LOG_WARN("[Process] Command '{}' timed out after {}ms", program, timeout.count());
            if (hChildJob.get() != nullptr) {
                TerminateJobObject(hChildJob.get(), 1);
            } else {
                TerminateProcess(hProcess.get(), 1);
            }
            break;
        }

        DWORD waitRes = WaitForSingleObject(hProcess.get(), 50);
        if (waitRes == WAIT_OBJECT_0) {
            break;
        }
    }

    if (res.timedOut || res.cancelled) {
        CancelIoEx(hStdOutRead.get(), nullptr);
        CancelIoEx(hStdErrRead.get(), nullptr);
    } else if (hChildJob.get() != nullptr) {
        // Child exited normally. Terminate any lingering grandchildren in the job object
        // so all inherited pipe write handles are closed by the OS, allowing ReadFile
        // in outThread/errThread to naturally drain all buffered pipe output until EOF.
        TerminateJobObject(hChildJob.get(), 0);
    }

    if (outThread.joinable()) {
        outThread.join();
    }
    if (errThread.joinable()) {
        errThread.join();
    }

    res.stdOut = std::move(stdOutData);
    res.stdErr = std::move(stdErrData);

    DWORD exitCode = 0;
    GetExitCodeProcess(hProcess.get(), &exitCode);
    res.exitCode = (res.timedOut || res.cancelled) ? -1 : static_cast<int>(exitCode);
    return res;
}

}  // namespace lightyt

#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/prctl.h>
#endif

namespace lightyt {

void Process::initProcessGroup() {}

void Process::assignToJobObject(void *processHandle) {
    (void)processHandle;
}

ProcessResult Process::run(const std::vector<std::string> &args, std::chrono::milliseconds timeout,
                           const std::atomic<bool> *cancelToken) {
    if (args.empty()) {
        return {};
    }
    return run(args[0], args, timeout, cancelToken);
}

ProcessResult Process::run(std::string_view program, const std::vector<std::string> &args,
                           std::chrono::milliseconds timeout, const std::atomic<bool> *cancelToken) {
    ProcessResult res;
    if (args.empty()) {
        return res;
    }

    // Pre-construct char* pointers in parent before fork() to eliminate malloc() deadlock in multithreaded runtime
    std::vector<char *> cArgs;
    cArgs.reserve(args.size() + 1);
    for (const auto &a : args) {
        cArgs.push_back(const_cast<char *>(a.c_str()));
    }
    cArgs.push_back(nullptr);

    int outPipe[2] = {-1, -1};
    int errPipe[2] = {-1, -1};

#if defined(__linux__)
    if (pipe2(outPipe, O_CLOEXEC) != 0)
        return res;
    if (pipe2(errPipe, O_CLOEXEC) != 0) {
        close(outPipe[0]);
        close(outPipe[1]);
        return res;
    }
#else
    if (pipe(outPipe) != 0)
        return res;
    if (pipe(errPipe) != 0) {
        close(outPipe[0]);
        close(outPipe[1]);
        return res;
    }
    fcntl(outPipe[0], F_SETFD, FD_CLOEXEC);
    fcntl(outPipe[1], F_SETFD, FD_CLOEXEC);
    fcntl(errPipe[0], F_SETFD, FD_CLOEXEC);
    fcntl(errPipe[1], F_SETFD, FD_CLOEXEC);
#endif

    pid_t pid = fork();
    if (pid < 0) {
        close(outPipe[0]);
        close(outPipe[1]);
        close(errPipe[0]);
        close(errPipe[1]);
        return res;
    }

    if (pid == 0) {
        // In child process: NO malloc or std::string/vector allocations!
#if defined(__linux__)
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        if (getppid() == 1) {
            _exit(1);
        }
#endif
        setpgid(0, 0);

        dup2(outPipe[1], STDOUT_FILENO);
        dup2(errPipe[1], STDERR_FILENO);

        close(outPipe[0]);
        close(outPipe[1]);
        close(errPipe[0]);
        close(errPipe[1]);

        execvp(cArgs[0], cArgs.data());
        _exit(127);
    }

    // In parent: ensure process group is set to avoid signal race
    setpgid(pid, pid);

    // Close write ends in parent
    close(outPipe[1]);
    close(errPipe[1]);

    // Make read ends non-blocking
    fcntl(outPipe[0], F_SETFL, O_NONBLOCK);
    fcntl(errPipe[0], F_SETFL, O_NONBLOCK);

    std::array<pollfd, 2> fds{};
    fds[0].fd = outPipe[0];
    fds[0].events = POLLIN;
    fds[1].fd = errPipe[0];
    fds[1].events = POLLIN;

    auto drainPipe = [](int fd, std::string &dest, bool &openFlag) {
        std::array<char, 65536> buf{};
        while (true) {
            ssize_t n = read(fd, buf.data(), buf.size());
            if (n > 0) {
                dest.append(buf.data(), static_cast<size_t>(n));
            } else if (n == 0) {
                openFlag = false;
                break;
            } else {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                openFlag = false;
                break;
            }
        }
    };

    auto startTime = std::chrono::steady_clock::now();
    bool outOpen = true;
    bool errOpen = true;
    bool childExited = false;
    int childStatus = 0;
    auto childExitTime = std::chrono::steady_clock::time_point{};

    while (outOpen || errOpen) {
        if (cancelToken != nullptr && cancelToken->load()) {
            res.cancelled = true;
            kill(-pid, SIGTERM);
            break;
        }

        if (!childExited) {
            pid_t wp = waitpid(pid, &childStatus, WNOHANG);
            if (wp == pid) {
                childExited = true;
                childExitTime = std::chrono::steady_clock::now();
            }
        } else {
            // Direct child already exited! If pipes remain open because grandchildren inherited them,
            // allow a short 50ms drain window for pending data, then terminate remaining grandchildren.
            if (std::chrono::steady_clock::now() - childExitTime > std::chrono::milliseconds(50)) {
                kill(-pid, SIGKILL);
                break;
            }
        }

        if (std::chrono::steady_clock::now() - startTime > timeout) {
            res.timedOut = true;
            LOG_WARN("[Process] Command '{}' timed out after {}ms", program, timeout.count());
            kill(-pid, SIGTERM);
            break;
        }

        int pollRes = poll(fds.data(), static_cast<nfds_t>(fds.size()), 20);
        if (pollRes > 0) {
            if (outOpen && (fds[0].revents & (POLLIN | POLLHUP | POLLERR))) {
                drainPipe(outPipe[0], res.stdOut, outOpen);
                if (!outOpen) {
                    fds[0].fd = -1;
                }
            }

            if (errOpen && (fds[1].revents & (POLLIN | POLLHUP | POLLERR))) {
                drainPipe(errPipe[0], res.stdErr, errOpen);
                if (!errOpen) {
                    fds[1].fd = -1;
                }
            }
        } else if (pollRes < 0 && errno != EINTR) {
            break;
        }
    }

    close(outPipe[0]);
    close(errPipe[0]);

    int status = childStatus;
    if (res.timedOut || res.cancelled) {
        auto killStart = std::chrono::steady_clock::now();
        bool exited = childExited;
        while (!exited && std::chrono::steady_clock::now() - killStart < std::chrono::milliseconds(200)) {
            pid_t wp = waitpid(pid, &status, WNOHANG);
            if (wp == pid) {
                exited = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (!exited) {
            kill(-pid, SIGKILL);
            waitpid(pid, &status, 0);
        }
        res.exitCode = -1;
    } else {
        if (!childExited) {
            waitpid(pid, &status, 0);
        }
        res.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }

    return res;
}

}  // namespace lightyt
#endif
