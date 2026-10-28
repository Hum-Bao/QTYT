#include "MpvApi.h"

#include "utils/AppPaths.h"
#include "utils/Logger.h"
#include "utils/StringUtils.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#endif

namespace lightyt {

#ifdef _WIN32
std::string findFileInPaths(std::string_view filename) {
    // 1. Check bundled local/relative lib/ directories
    for (const auto &cand : paths::getLocalCandidatePaths(filename)) {
        if (paths::isValidBinary(cand, 1024 * 1024)) {
            return utils::pathToUtf8(cand);
        }
    }

    // 2. Search Windows system PATH with dynamically grown buffer
    std::filesystem::path wFilename = utils::u8ToPath(filename);
    std::vector<wchar_t> searchBuf(MAX_PATH);
    DWORD len = SearchPathW(nullptr, wFilename.c_str(), nullptr, static_cast<DWORD>(searchBuf.size()), searchBuf.data(),
                            nullptr);
    if (len >= searchBuf.size()) {
        searchBuf.resize(len + 1);
        len = SearchPathW(nullptr, wFilename.c_str(), nullptr, static_cast<DWORD>(searchBuf.size()), searchBuf.data(),
                          nullptr);
    }
    if (len > 0 && len < searchBuf.size()) {
        std::filesystem::path p(searchBuf.data());
        if (paths::isValidBinary(p, 1024 * 1024)) {
            return utils::pathToUtf8(p);
        }
    }

    // 3. Search WinGet installed packages (shinchiro.mpv)
    auto wingetBinary = paths::findWinGetPackageBinary("shinchiro.mpv", filename, 1024 * 1024);
    if (!wingetBinary.empty()) {
        return utils::pathToUtf8(wingetBinary);
    }

    // 4. Standard Program Files and LocalAppData installation paths
    const wchar_t *wProgFiles = _wgetenv(L"ProgramFiles");
    if (wProgFiles != nullptr && wProgFiles[0] != L'\0') {
        std::filesystem::path progFiles = std::filesystem::path(wProgFiles) / "mpv" / utils::u8ToPath(filename);
        if (paths::isValidBinary(progFiles, 1024 * 1024)) {
            return utils::pathToUtf8(progFiles);
        }
    } else {
        std::filesystem::path progFiles = std::filesystem::path(L"C:\\Program Files\\mpv") / utils::u8ToPath(filename);
        if (paths::isValidBinary(progFiles, 1024 * 1024)) {
            return utils::pathToUtf8(progFiles);
        }
    }

    const wchar_t *wProgFilesX86 = _wgetenv(L"ProgramFiles(x86)");
    if (wProgFilesX86 != nullptr && wProgFilesX86[0] != L'\0') {
        std::filesystem::path progFilesX86 = std::filesystem::path(wProgFilesX86) / "mpv" / utils::u8ToPath(filename);
        if (paths::isValidBinary(progFilesX86, 1024 * 1024)) {
            return utils::pathToUtf8(progFilesX86);
        }
    } else {
        std::filesystem::path progFilesX86 = std::filesystem::path(L"C:\\Program Files (x86)\\mpv") / utils::u8ToPath(filename);
        if (paths::isValidBinary(progFilesX86, 1024 * 1024)) {
            return utils::pathToUtf8(progFilesX86);
        }
    }

    const wchar_t *wLocalApp = _wgetenv(L"LOCALAPPDATA");
    if (wLocalApp != nullptr && wLocalApp[0] != L'\0') {
        std::filesystem::path progUser = std::filesystem::path(wLocalApp) / "Programs" / "mpv" / utils::u8ToPath(filename);
        if (paths::isValidBinary(progUser, 1024 * 1024)) {
            return utils::pathToUtf8(progUser);
        }
    }

    return {};
}
#endif

bool MpvApi::load() {
    if (lib.isLoaded())
        return true;

#ifdef _WIN32
    // 1. Check local/SearchPath/WinGet/ProgramFiles candidate paths
    std::string foundMpvDll = findFileInPaths("mpv-2.dll");
    if (foundMpvDll.empty())
        foundMpvDll = findFileInPaths("libmpv-2.dll");
    if (foundMpvDll.empty())
        foundMpvDll = findFileInPaths("mpv-1.dll");

    // 2. Fall back to dedicated WinGet package binary search for libmpv-2.dll
    if (foundMpvDll.empty()) {
        auto wingetDll = paths::findWinGetPackageBinary("shinchiro.mpv", "libmpv-2.dll", 1024 * 1024);
        if (!wingetDll.empty()) {
            foundMpvDll = utils::pathToUtf8(wingetDll);
        }
    }

    if (!foundMpvDll.empty()) {
        if (lib.load(foundMpvDll)) {
            LOG_INFO("[MpvApi] Loaded libmpv from candidate path: {}", foundMpvDll);
        }
    }

    // 3. Fall back to system dynamic library loader search paths by candidate name
    if (!lib.isLoaded()) {
        constexpr std::array<const char *, 4> candidates = {"mpv-2.dll", "libmpv-2.dll", "mpv-1.dll", "libmpv.dll"};
        for (const char *cand : candidates) {
            if (lib.load(cand)) {
                LOG_INFO("[MpvApi] Loaded libmpv by candidate name: {}", cand);
                break;
            }
        }
    }
#else
    constexpr std::array<const char *, 3> candidates = {"libmpv.so.2", "libmpv.so.1", "libmpv.so"};
    // 1. Check bundled lib/ directory candidates first
    for (const char *cand : candidates) {
        auto p = paths::findCandidateFile(cand);
        if (!p.empty() && lib.load(utils::pathToUtf8(p))) {
            LOG_INFO("[MpvApi] Loaded bundled libmpv from {}", utils::pathToUtf8(p));
            break;
        }
    }

    // 2. Check system dynamic linker search paths
    if (!lib.isLoaded()) {
        for (const char *cand : candidates) {
            if (lib.load(cand)) {
                LOG_INFO("[MpvApi] Loaded system libmpv: {}", cand);
                break;
            }
        }
    }
#endif

    if (!lib.isLoaded()) {
        LOG_WARN("[MpvApi] Failed to locate or load libmpv from lib/ or system PATH.");
        return false;
    }

    create = lib.getSymbol<mpv_handle *(*)()>("mpv_create");
    initialize = lib.getSymbol<int (*)(mpv_handle *)>("mpv_initialize");
    destroy = lib.getSymbol<void (*)(mpv_handle *)>("mpv_destroy");
    terminate_destroy = lib.getSymbol<void (*)(mpv_handle *)>("mpv_terminate_destroy");
    command = lib.getSymbol<int (*)(mpv_handle *, const char **)>("mpv_command");
    command_async = lib.getSymbol<int (*)(mpv_handle *, uint64_t, const char **)>("mpv_command_async");
    set_option = lib.getSymbol<int (*)(mpv_handle *, const char *, mpv_format, void *)>("mpv_set_option");
    set_option_string = lib.getSymbol<int (*)(mpv_handle *, const char *, const char *)>("mpv_set_option_string");
    get_property = lib.getSymbol<int (*)(mpv_handle *, const char *, mpv_format, void *)>("mpv_get_property");
    set_property = lib.getSymbol<int (*)(mpv_handle *, const char *, mpv_format, void *)>("mpv_set_property");
    set_property_string = lib.getSymbol<int (*)(mpv_handle *, const char *, const char *)>("mpv_set_property_string");
    observe_property = lib.getSymbol<int (*)(mpv_handle *, uint64_t, const char *, mpv_format)>("mpv_observe_property");
    wait_event = lib.getSymbol<mpv_event *(*)(mpv_handle *, double)>("mpv_wait_event");
    wakeup = lib.getSymbol<void (*)(mpv_handle *)>("mpv_wakeup");
    set_wakeup_callback = lib.getSymbol<void (*)(mpv_handle *, void (*)(void *), void *)>("mpv_set_wakeup_callback");
    error_string = lib.getSymbol<const char *(*)(int)>("mpv_error_string");
    request_log_messages = lib.getSymbol<int (*)(mpv_handle *, const char *)>("mpv_request_log_messages");
    mpv_free = lib.getSymbol<void (*)(void *)>("mpv_free");

    // Optional mpv_render_context functions
    render_context_create =
        lib.getSymbol<int (*)(mpv_render_context **, mpv_handle *, mpv_render_param *)>("mpv_render_context_create");
    render_context_set_update_callback = lib.getSymbol<void (*)(mpv_render_context *, mpv_render_update_fn, void *)>(
        "mpv_render_context_set_update_callback");
    render_context_update = lib.getSymbol<uint64_t (*)(mpv_render_context *)>("mpv_render_context_update");
    render_context_render =
        lib.getSymbol<int (*)(mpv_render_context *, mpv_render_param *)>("mpv_render_context_render");
    render_context_report_swap = lib.getSymbol<void (*)(mpv_render_context *)>("mpv_render_context_report_swap");
    render_context_free = lib.getSymbol<void (*)(mpv_render_context *)>("mpv_render_context_free");

    return create != nullptr && initialize != nullptr && command != nullptr && set_option != nullptr &&
           set_property != nullptr;
}

MpvApi &getMpvApi() {
    static auto *s_api = new MpvApi();
    return *s_api;
}

}  // namespace lightyt
