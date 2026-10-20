#pragma once

#include <atomic>
#include <chrono>
#include <string>
#include <string_view>
#include <vector>

namespace lightyt {

struct ProcessResult {
    int exitCode = -1;
    std::string stdOut;
    std::string stdErr;
    bool timedOut = false;
    bool cancelled = false;

    [[nodiscard]] bool success() const noexcept { return exitCode == 0 && !timedOut && !cancelled; }
};

class Process {
public:
    // Builds an MSVCRT-compatible escaped command line string from arguments.
    [[nodiscard]] static std::string buildCommandLine(const std::vector<std::string> &args);

    // Execute a process with arguments directly (NO shell invocation)
    // Supports timeout and cooperative cancellation.
    [[nodiscard]] static ProcessResult run(const std::vector<std::string> &args,
                                           std::chrono::milliseconds timeout = std::chrono::milliseconds(30000),
                                           const std::atomic<bool> *cancelToken = nullptr);

    // Convenience overload to execute command string on Windows if needed
    [[nodiscard]] static ProcessResult run(std::string_view program, const std::vector<std::string> &args,
                                           std::chrono::milliseconds timeout = std::chrono::milliseconds(30000),
                                           const std::atomic<bool> *cancelToken = nullptr);

    // Initialize OS-level process management (e.g. Windows Job Object for automatic child cleanup)
    static void initProcessGroup();

    // Assign an external child process handle (e.g. mpv.exe) to the cleanup group
    static void assignToJobObject(void *processHandle);
};

}  // namespace lightyt
