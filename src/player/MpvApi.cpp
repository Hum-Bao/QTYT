#include "MpvApi.h"

#include "utils/AppPaths.h"

#include <algorithm>
#include <array>
#include <filesystem>

#ifdef _WIN32
#include "utils/StringUtils.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#endif

namespace lightyt {

#ifdef _WIN32
namespace {

bool isValidBinaryWin(const std::filesystem::path &p) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(p, ec))
        return false;
    // Reject shims and stubs under 1MB (e.g. WinGet Links, Scoop stubs) that loop-spawn
    auto sz = std::filesystem::file_size(p, ec);
    if (ec || sz < 1048576)
        return false;
    return true;
}

}  // namespace

std::string findFileInPaths(std::string_view filename) {
    std::string filenameStr(filename);
    for (const auto &cand : paths::getLocalCandidatePaths(filename)) {
        if (isValidBinaryWin(cand))
            return cand.string();
    }

    std::wstring wFilename = utils::toWString(filename);
    std::array<wchar_t, MAX_PATH> searchBuf{};
    if (SearchPathW(nullptr, wFilename.c_str(), nullptr, static_cast<DWORD>(searchBuf.size()), searchBuf.data(),
                    nullptr) > 0) {
        std::filesystem::path p(searchBuf.data());
        if (isValidBinaryWin(p)) {
            return p.string();
        }
    }

    const char *localAppData = std::getenv("LOCALAPPDATA");
    if (localAppData != nullptr && localAppData[0] != '\0') {
        std::error_code ec;
        std::filesystem::path pkgDir = std::filesystem::path(localAppData) / "Microsoft" / "WinGet" / "Packages";
        if (std::filesystem::is_directory(pkgDir, ec)) {
            for (const auto &entry : std::filesystem::recursive_directory_iterator(
                     pkgDir, std::filesystem::directory_options::skip_permission_denied, ec)) {
                if (ec)
                    break;
                if (entry.is_regular_file(ec) && entry.path().filename().string() == filenameStr) {
                    if (isValidBinaryWin(entry.path())) {
                        return entry.path().string();
                    }
                }
            }
        }
    }

    std::string progFiles = "C:\\Program Files\\mpv\\" + filenameStr;
    if (isValidBinaryWin(progFiles))
        return progFiles;
    std::string progFilesX86 = "C:\\Program Files (x86)\\mpv\\" + filenameStr;
    if (isValidBinaryWin(progFilesX86))
        return progFilesX86;
    if (localAppData != nullptr && localAppData[0] != '\0') {
        std::string progUser = std::string(localAppData) + "\\Programs\\mpv\\" + filenameStr;
        if (isValidBinaryWin(progUser))
            return progUser;
    }
    return {};
}
#endif

bool MpvApi::load() {
    if (lib.isLoaded())
        return true;

#ifdef _WIN32
    std::string foundMpvDll = findFileInPaths("mpv-2.dll");
    if (foundMpvDll.empty())
        foundMpvDll = findFileInPaths("libmpv-2.dll");
    if (foundMpvDll.empty())
        foundMpvDll = findFileInPaths("mpv-1.dll");

    if (!foundMpvDll.empty()) {
        lib.load(foundMpvDll);
    }

    if (!lib.isLoaded()) {
        constexpr std::array<const char *, 4> candidates = {"mpv-2.dll", "libmpv-2.dll", "mpv-1.dll", "libmpv.dll"};
        for (const char *cand : candidates) {
            if (lib.load(cand)) {
                break;
            }
        }
    }
#else
    constexpr std::array<const char *, 3> candidates = {"libmpv.so.2", "libmpv.so.1", "libmpv.so"};
    // 1. Check bundled lib/ directory candidates first
    for (const char *cand : candidates) {
        auto p = paths::findCandidateFile(cand);
        if (!p.empty() && lib.load(p.string())) {
            break;
        }
    }

    // 2. Check system dynamic linker search paths
    if (!lib.isLoaded()) {
        for (const char *cand : candidates) {
            if (lib.load(cand)) {
                break;
            }
        }
    }
#endif

    if (!lib.isLoaded())
        return false;

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
