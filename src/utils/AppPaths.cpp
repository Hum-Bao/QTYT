#include "AppPaths.h"
#include "StringUtils.h"

#include <algorithm>
#include <cstdlib>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <shlobj.h>
#include <windows.h>
#include <winsock2.h>
#else
#include <linux/limits.h>
#include <unistd.h>
#endif

namespace lightyt::paths {

namespace fs = std::filesystem;

std::filesystem::path getAppDataDir() {
#ifdef _WIN32
    PWSTR pathWide = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &pathWide))) {
        fs::path res(pathWide);
        CoTaskMemFree(pathWide);
        return res / "LightYT";
    }
    const char *localApp = std::getenv("LOCALAPPDATA");
    if (localApp != nullptr && localApp[0] != '\0') {
        return fs::path(localApp) / "LightYT";
    }
    return fs::path("data");
#else
    const char *xdgData = std::getenv("XDG_DATA_HOME");
    if (xdgData != nullptr && xdgData[0] != '\0') {
        return fs::path(xdgData) / "LightYT";
    }
    const char *home = std::getenv("HOME");
    if (home != nullptr && home[0] != '\0') {
        return fs::path(home) / ".local" / "share" / "LightYT";
    }
    return fs::path("/tmp") / "lightyt_data";
#endif
}

std::filesystem::path getCacheDir() {
    const char *custom = std::getenv("LIGHTYT_CACHE_DIR");
    if (custom != nullptr && custom[0] != '\0') {
        return custom;
    }

#ifdef _WIN32
    return getAppDataDir() / "cache";
#else
    const char *xdgCache = std::getenv("XDG_CACHE_HOME");
    if (xdgCache != nullptr && xdgCache[0] != '\0') {
        return fs::path(xdgCache) / "LightYT";
    }
    const char *home = std::getenv("HOME");
    if (home != nullptr && home[0] != '\0') {
        return fs::path(home) / ".cache" / "LightYT";
    }
    return fs::path("/tmp") / "lightyt_cache";
#endif
}

std::filesystem::path getThumbnailCacheDir() {
    return getCacheDir() / "thumbnails";
}

std::filesystem::path getExecutableDir() {
#ifdef _WIN32
    std::vector<wchar_t> buf(MAX_PATH);
    while (buf.size() <= 32768) {
        DWORD len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (len == 0) {
            break;
        }
        if (len < buf.size()) {
            return std::filesystem::path(buf.data(), buf.data() + len).parent_path();
        }
        buf.resize(buf.size() * 2);
    }
    return fs::current_path();
#else
    char buf[PATH_MAX]{};
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        return fs::path(buf).parent_path();
    }
    return fs::current_path();
#endif
}

bool ensureDirExists(const std::filesystem::path &dir) {
    std::error_code ec;
    return fs::create_directories(dir, ec) || fs::exists(dir, ec);
}

std::filesystem::path getSettingsFilePath() {
    return getAppDataDir() / "settings.ini";
}

std::vector<std::filesystem::path> getLocalCandidatePaths(std::string_view filename) {
    auto exeDir = getExecutableDir();
    std::filesystem::path fn(filename);
    return {exeDir / "lib" / fn, exeDir.parent_path() / "lib" / fn, fs::path("lib") / fn, exeDir / fn};
}

bool isValidBinary(const std::filesystem::path &p, std::uintmax_t minWinSizeBytes) {
    std::error_code ec;
    if (!fs::exists(p, ec) || !fs::is_regular_file(p, ec)) {
        return false;
    }
#ifdef _WIN32
    if (minWinSizeBytes > 0) {
        auto sz = fs::file_size(p, ec);
        if (ec || sz < minWinSizeBytes) {
            return false;
        }
    }
#else
    (void)minWinSizeBytes;
#endif
    return true;
}

std::filesystem::path findWinGetPackageBinary(std::string_view packageSubstring,
                                              std::string_view fileName,
                                              std::uintmax_t minWinSizeBytes) {
#ifdef _WIN32
    const wchar_t *localAppData = _wgetenv(L"LOCALAPPDATA");
    if (localAppData == nullptr || localAppData[0] == L'\0') {
        return {};
    }
    std::filesystem::path wingetDir = std::filesystem::path(localAppData) / "Microsoft" / "WinGet" / "Packages";
    std::error_code ec;
    if (!fs::exists(wingetDir, ec) || !fs::is_directory(wingetDir, ec)) {
        return {};
    }
    std::filesystem::path targetName = lightyt::utils::u8ToPath(fileName);
    for (const auto &entry : fs::directory_iterator(wingetDir, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_directory(ec)) {
            continue;
        }
        std::string dirName = lightyt::utils::pathToUtf8(entry.path().filename());
        if (packageSubstring.empty() || dirName.find(packageSubstring) != std::string::npos) {
            for (const auto &sub : fs::recursive_directory_iterator(
                     entry.path(), fs::directory_options::skip_permission_denied, ec)) {
                if (ec) {
                    break;
                }
                if (sub.is_regular_file(ec) && sub.path().filename() == targetName &&
                    isValidBinary(sub.path(), minWinSizeBytes)) {
                    return sub.path();
                }
            }
        }
    }
#else
    (void)packageSubstring;
    (void)fileName;
    (void)minWinSizeBytes;
#endif
    return {};
}

std::filesystem::path findCandidateFile(std::string_view filename,
                                        const std::vector<std::filesystem::path> &fallbackPaths) {
    std::error_code ec;
    auto localCands = getLocalCandidatePaths(filename);
    auto itLocal = std::ranges::find_if(localCands, [&](const auto &cand) { return fs::is_regular_file(cand, ec); });
    if (itLocal != localCands.end()) {
        return *itLocal;
    }
    auto itFallback =
        std::ranges::find_if(fallbackPaths, [&](const auto &cand) { return fs::is_regular_file(cand, ec); });
    if (itFallback != fallbackPaths.end()) {
        return *itFallback;
    }
    return {};
}

}  // namespace lightyt::paths
