#include "StreamResolver.h"

#include "Process.h"
#include "utils/AppPaths.h"
#include "utils/Logger.h"
#include "utils/StringUtils.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
struct CachedStream {
    StreamInfo info;
    std::chrono::steady_clock::time_point expiresAt;
};

std::mutex g_cacheMutex;
std::unordered_map<std::string, CachedStream> g_streamCache;

struct InFlight {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    bool success = false;
    StreamInfo info;
    std::string error;
};

std::mutex g_inFlightMutex;
std::unordered_map<std::string, std::shared_ptr<InFlight>> g_inFlight;
std::atomic<bool> g_shutdownRequested{false};

class PrefetchQueue {
public:
    PrefetchQueue() {
        for (int i = 0; i < 2; ++i) {
            m_workers.emplace_back([this]() { run(); });
        }
    }

    ~PrefetchQueue() { shutdown(); }

    void enqueue(std::string cleanId, std::string quality, std::string cookies, bool allowAv1) {
        {
            std::lock_guard lock(m_mutex);
            if (m_stop) {
                return;
            }
            auto it = std::ranges::find_if(
                m_tasks, [&](const auto &task) { return task.cleanId == cleanId && task.quality == quality; });
            if (it != m_tasks.end()) {
                m_tasks.erase(it);
            }
            if (m_tasks.size() >= 4) {
                m_tasks.pop_front();
            }
            m_tasks.push_back({std::move(cleanId), std::move(quality), std::move(cookies), allowAv1});
        }
        m_cv.notify_one();
    }

    void shutdown() {
        {
            std::lock_guard lock(m_mutex);
            m_stop = true;
            m_tasks.clear();
        }
        m_cv.notify_all();
        for (auto &w : m_workers) {
            if (w.joinable()) {
                w.join();
            }
        }
        m_workers.clear();
    }

private:
    struct Task {
        std::string cleanId;
        std::string quality;
        std::string cookies;
        bool allowAv1 = false;
    };

    void run() {
        while (true) {
            Task task;
            {
                std::unique_lock lock(m_mutex);
                m_cv.wait(lock, [this]() { return m_stop || !m_tasks.empty(); });
                if (m_stop && m_tasks.empty()) {
                    break;
                }
                if (!m_tasks.empty()) {
                    task = std::move(m_tasks.back());
                    m_tasks.pop_back();
                }
            }

            if (!task.cleanId.empty() && !m_stop && !g_shutdownRequested.load()) {
                StreamInfo info;
                std::string err;
                LOG_DEBUG("[StreamResolver] Background prefetching stream for {}...", task.cleanId);
                bool ok = StreamResolver::resolve(task.cleanId, task.quality, info, err, task.cookies, task.allowAv1,
                                                  &g_shutdownRequested);
                if (ok) {
                    LOG_DEBUG("[StreamResolver] Background prefetch completed for {}", task.cleanId);
                }
            }
        }
    }

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<Task> m_tasks;
    bool m_stop = false;
    std::vector<std::thread> m_workers;
};

PrefetchQueue &getPrefetchQueue() {
    static PrefetchQueue s_queue;
    return s_queue;
}

bool fileExists(const std::filesystem::path &p) {
    std::error_code ec;
#ifdef _WIN32
    if (!std::filesystem::is_regular_file(p, ec)) {
        return false;
    }
    auto sz = std::filesystem::file_size(p, ec);
    return !ec && sz >= 1048576;  // Reject WinGet Link shims under 1MB
#else
    return std::filesystem::is_regular_file(p, ec);
#endif
}

bool copyFileSecure(const std::filesystem::path &src, const std::filesystem::path &dst) {
    std::ifstream in(src, std::ios::binary);
    if (!in.is_open()) {
        return false;
    }
    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out << in.rdbuf();
    out.flush();
    if (!out.good()) {
        return false;
    }
    out.close();

    std::error_code ec;
    std::filesystem::permissions(dst, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace, ec);
#ifndef _WIN32
    chmod(dst.c_str(), S_IRUSR | S_IWUSR);
#endif
    return true;
}
}  // namespace

std::chrono::steady_clock::time_point StreamResolver::parseUrlExpiry(std::string_view url) {
    auto pos = url.find("expire=");
    if (pos != std::string_view::npos) {
        auto end = url.find('&', pos);
        auto valStr = url.substr(pos + 7, (end == std::string_view::npos) ? end : end - (pos + 7));
        int64_t expSec = 0;
        auto [ptr, ec] = std::from_chars(valStr.data(), valStr.data() + valStr.size(), expSec);
        if (ec == std::errc{}) {
            int64_t nowSec =
                std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                    .count();
            int64_t remaining = expSec - nowSec - 600;  // 10 min safety margin
            if (remaining > 60 && remaining < 28800) {
                return std::chrono::steady_clock::now() + std::chrono::seconds(remaining);
            }
        }
    }
    return std::chrono::steady_clock::now() + std::chrono::hours(2);
}

std::string StreamResolver::qualityToFormatString(std::string_view quality, bool allowAv1) {
    std::string vcodecFilter = allowAv1 ? "" : "[vcodec!^=av01]";

    if (quality == "1080p") {
        return "bestvideo[height<=1080]" + vcodecFilter +
               "+bestaudio/bestvideo[height<=1080]+bestaudio/best[height<=1080]/best";
    }
    if (quality == "720p") {
        return "bestvideo[height<=720]" + vcodecFilter +
               "+bestaudio/bestvideo[height<=720]+bestaudio/best[height<=720]/best";
    }
    if (quality == "480p") {
        return "bestvideo[height<=480]" + vcodecFilter +
               "+bestaudio/bestvideo[height<=480]+bestaudio/best[height<=480]/best";
    }
    if (quality == "360p") {
        return "bestvideo[height<=360]" + vcodecFilter +
               "+bestaudio/bestvideo[height<=360]+bestaudio/best[height<=360]/best";
    }
    if (quality == "Audio Only") {
        return "bestaudio/best";
    }
    // "Auto" / "Best"
    return "bestvideo" + vcodecFilter + "+bestaudio/best";
}

std::string StreamResolver::qualityToFormatSort(std::string_view quality, bool allowAv1) {
    if (quality == "1080p") {
        return allowAv1 ? "res:1080,vcodec:av01,vp09,avc1" : "res:1080";
    }
    if (quality == "720p") {
        return allowAv1 ? "res:720,vcodec:av01,vp09,avc1" : "res:720";
    }
    if (quality == "480p") {
        return allowAv1 ? "res:480,vcodec:av01,vp09,avc1" : "res:480";
    }
    if (quality == "360p") {
        return allowAv1 ? "res:360,vcodec:av01,vp09,avc1" : "res:360";
    }
    return "";
}

void StreamResolver::clearCache() {
    std::lock_guard lock(g_cacheMutex);
    g_streamCache.clear();
}

void StreamResolver::pruneExpired() {
    std::lock_guard lock(g_cacheMutex);
    auto now = std::chrono::steady_clock::now();
    std::erase_if(g_streamCache, [now](const auto &item) { return now >= item.second.expiresAt; });
}

void StreamResolver::shutdown() {
    g_shutdownRequested.store(true);
    getPrefetchQueue().shutdown();

    std::lock_guard lock(g_inFlightMutex);
    for (auto &[k, flight] : g_inFlight) {
        if (flight) {
            std::lock_guard flightLock(flight->mutex);
            flight->done = true;
            flight->error = "Stream resolution cancelled (shutting down)";
            flight->cv.notify_all();
        }
    }
}

std::string StreamResolver::findYtDlp() {
    static std::string s_cachedYtDlp;
    static std::mutex s_mutex;
    {
        std::lock_guard lock(s_mutex);
        if (!s_cachedYtDlp.empty()) {
            return s_cachedYtDlp;
        }
    }

    std::string found;
#ifdef _WIN32
    for (const char *name : {"yt-dlp.exe", "yt-dlp"}) {
        for (const auto &cand : lightyt::paths::getLocalCandidatePaths(name)) {
            if (fileExists(cand)) {
                found = cand.string();
                break;
            }
        }
        if (!found.empty())
            break;
    }

    if (found.empty()) {
        const char *localApp = std::getenv("LOCALAPPDATA");
        if (localApp != nullptr && localApp[0] != '\0') {
            std::error_code ec;
            std::filesystem::path pkgDir = std::filesystem::path(localApp) / "Microsoft" / "WinGet" / "Packages";
            if (std::filesystem::is_directory(pkgDir, ec)) {
                for (const auto &entry : std::filesystem::recursive_directory_iterator(
                         pkgDir, std::filesystem::directory_options::skip_permission_denied, ec)) {
                    if (ec)
                        break;
                    if (entry.is_regular_file(ec) && entry.path().filename() == "yt-dlp.exe") {
                        if (fileExists(entry.path())) {
                            found = entry.path().string();
                            break;
                        }
                    }
                }
            }
        }
        if (found.empty()) {
            std::array<char, MAX_PATH> searchBuf{};
            if (SearchPathA(nullptr, "yt-dlp.exe", nullptr, static_cast<DWORD>(searchBuf.size()), searchBuf.data(),
                            nullptr) > 0) {
                if (fileExists(searchBuf.data())) {
                    found = searchBuf.data();
                }
            }
        }
    }
#else
    for (const char *name : {"yt-dlp", "yt-dlp.exe"}) {
        for (const auto &cand : lightyt::paths::getLocalCandidatePaths(name)) {
            if (fileExists(cand) && access(cand.c_str(), X_OK) == 0) {
                found = cand.string();
                break;
            }
        }
        if (!found.empty())
            break;
    }

    if (found.empty()) {
        if (access("/usr/local/bin/yt-dlp", X_OK) == 0) {
            found = "/usr/local/bin/yt-dlp";
        } else if (access("/usr/bin/yt-dlp", X_OK) == 0) {
            found = "/usr/bin/yt-dlp";
        } else {
            const char *pathEnv = std::getenv("PATH");
            if (pathEnv != nullptr) {
                std::stringstream ss(pathEnv);
                std::string dir;
                while (std::getline(ss, dir, ':')) {
                    if (!dir.empty()) {
                        std::filesystem::path cand = std::filesystem::path(dir) / "yt-dlp";
                        if (access(cand.c_str(), X_OK) == 0) {
                            found = cand.string();
                            break;
                        }
                    }
                }
            }
        }
    }
#endif

    if (!found.empty()) {
        std::lock_guard lock(s_mutex);
        s_cachedYtDlp = found;
    }
    return found;
}

namespace {
std::vector<std::string> findAllJsRuntimes() {
    static std::vector<std::string> s_cachedRuntimes;
    static bool s_initialized = false;
    static std::mutex s_mutex;
    {
        std::lock_guard lock(s_mutex);
        if (s_initialized) {
            return s_cachedRuntimes;
        }
    }

    std::vector<std::string> foundList;
    auto addUnique = [&](std::string_view stem, const std::filesystem::path &p) {
        std::string entry = std::string(stem) + ":" + p.string();
        if (std::ranges::find(foundList, entry) == foundList.end()) {
            foundList.push_back(std::move(entry));
        }
    };

#ifdef _WIN32
    auto exeDir = lightyt::paths::getExecutableDir();
    constexpr std::array<std::pair<const char *, const char *>, 3> kRuntimes = {
        {{"deno", "deno.exe"}, {"node", "node.exe"}, {"bun", "bun.exe"}}};

    const char *pathEnv = std::getenv("PATH");
    const char *userProfile = std::getenv("USERPROFILE");
    const char *localApp = std::getenv("LOCALAPPDATA");
    const char *nvmSymlink = std::getenv("NVM_SYMLINK");

    // Pre-scan WinGet Packages once for deno.exe, node.exe, bun.exe so Deno in WinGet
    // is never masked by an older system Node.js when WinGet Links has a <1MB shim.
    std::unordered_map<std::string, std::filesystem::path> wingetBins;
    if (localApp != nullptr && localApp[0] != '\0') {
        std::error_code ec;
        std::filesystem::path pkgDir = std::filesystem::path(localApp) / "Microsoft" / "WinGet" / "Packages";
        if (std::filesystem::is_directory(pkgDir, ec)) {
            for (const auto &entry : std::filesystem::recursive_directory_iterator(
                     pkgDir, std::filesystem::directory_options::skip_permission_denied, ec)) {
                if (ec)
                    break;
                if (entry.is_regular_file(ec)) {
                    auto fn = entry.path().filename().string();
                    if ((fn == "deno.exe" || fn == "node.exe" || fn == "bun.exe") && fileExists(entry.path())) {
                        wingetBins.try_emplace(fn, entry.path());
                    }
                }
            }
        }
    }

    // Search each runtime across all locations in strict priority order: deno -> node -> bun
    for (const auto &[stem, binName] : kRuntimes) {
        for (const auto &cand : lightyt::paths::getLocalCandidatePaths(binName)) {
            if (fileExists(cand)) {
                addUnique(stem, cand);
                break;
            }
        }
        std::array<char, MAX_PATH> searchBuf{};
        if (SearchPathA(nullptr, binName, nullptr, static_cast<DWORD>(searchBuf.size()), searchBuf.data(), nullptr) >
            0) {
            if (fileExists(searchBuf.data())) {
                addUnique(stem, searchBuf.data());
            }
        }
        if (pathEnv && pathEnv[0] != '\0') {
            std::stringstream ss(pathEnv);
            std::string dir;
            while (std::getline(ss, dir, ';')) {
                if (dir.empty())
                    continue;
                std::filesystem::path cand = std::filesystem::path(dir) / binName;
                if (fileExists(cand)) {
                    addUnique(stem, cand);
                    break;
                }
            }
        }
        if (std::string_view(stem) == "deno") {
            if (userProfile && userProfile[0] != '\0') {
                std::filesystem::path up(userProfile);
                if (fileExists(up / ".deno" / "bin" / "deno.exe"))
                    addUnique("deno", up / ".deno" / "bin" / "deno.exe");
                if (fileExists(up / "scoop" / "apps" / "deno" / "current" / "deno.exe"))
                    addUnique("deno", up / "scoop" / "apps" / "deno" / "current" / "deno.exe");
            }
        } else if (std::string_view(stem) == "node") {
            if (nvmSymlink && nvmSymlink[0] != '\0') {
                std::filesystem::path nvmPath = std::filesystem::path(nvmSymlink) / "node.exe";
                if (fileExists(nvmPath))
                    addUnique("node", nvmPath);
            }
            if (fileExists("C:/Program Files/nodejs/node.exe"))
                addUnique("node", "C:/Program Files/nodejs/node.exe");
            if (fileExists("C:/Program Files (x86)/nodejs/node.exe"))
                addUnique("node", "C:/Program Files (x86)/nodejs/node.exe");
            if (localApp && localApp[0] != '\0') {
                std::filesystem::path laNode = std::filesystem::path(localApp) / "Programs" / "nodejs" / "node.exe";
                if (fileExists(laNode))
                    addUnique("node", laNode);
            }
            if (userProfile && userProfile[0] != '\0') {
                std::filesystem::path scoopNode =
                    std::filesystem::path(userProfile) / "scoop" / "apps" / "nodejs" / "current" / "node.exe";
                if (fileExists(scoopNode))
                    addUnique("node", scoopNode);
            }
        } else if (std::string_view(stem) == "bun") {
            if (userProfile && userProfile[0] != '\0') {
                std::filesystem::path upBun = std::filesystem::path(userProfile) / ".bun" / "bin" / "bun.exe";
                if (fileExists(upBun))
                    addUnique("bun", upBun);
            }
        }
        if (auto it = wingetBins.find(binName); it != wingetBins.end()) {
            addUnique(stem, it->second);
        }
    }
#else
    for (const char *name : {"deno", "node", "bun"}) {
        for (const auto &cand : lightyt::paths::getLocalCandidatePaths(name)) {
            if (fileExists(cand) && access(cand.c_str(), X_OK) == 0) {
                addUnique(name, cand);
                break;
            }
        }
        const char *pathEnv = std::getenv("PATH");
        if (pathEnv != nullptr) {
            std::stringstream ss(pathEnv);
            std::string dir;
            while (std::getline(ss, dir, ':')) {
                if (!dir.empty()) {
                    std::filesystem::path cand = std::filesystem::path(dir) / name;
                    if (access(cand.c_str(), X_OK) == 0) {
                        addUnique(name, cand);
                        break;
                    }
                }
            }
        }

        const char *home = std::getenv("HOME");
        if (home != nullptr) {
            std::filesystem::path homePath(home);
            if (std::string_view(name) == "deno") {
                auto denoBin = homePath / ".deno" / "bin" / "deno";
                if (access(denoBin.c_str(), X_OK) == 0)
                    addUnique("deno", denoBin);
            } else if (std::string_view(name) == "bun") {
                auto bunBin = homePath / ".bun" / "bin" / "bun";
                if (access(bunBin.c_str(), X_OK) == 0)
                    addUnique("bun", bunBin);
            } else if (std::string_view(name) == "node") {
                std::filesystem::path nvmDir = homePath / ".nvm" / "versions" / "node";
                std::error_code ec;
                if (std::filesystem::is_directory(nvmDir, ec)) {
                    for (const auto &entry : std::filesystem::directory_iterator(nvmDir, ec)) {
                        if (entry.is_directory(ec)) {
                            auto nodeBin = entry.path() / "bin" / "node";
                            if (access(nodeBin.c_str(), X_OK) == 0) {
                                addUnique("node", nodeBin);
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
#endif

    {
        std::lock_guard lock(s_mutex);
        if (!s_initialized) {
            if (!foundList.empty()) {
                LOG_INFO("[StreamResolver] Using JS runtime: {}", foundList.front());
                auto colonPos = foundList.front().find(':');
                if (colonPos != std::string::npos) {
                    std::string binPath = foundList.front().substr(colonPos + 1);
                    std::error_code ec;
                    std::filesystem::path binDir = std::filesystem::path(binPath).parent_path();
                    if (!binDir.empty() && std::filesystem::is_directory(binDir, ec)) {
                        const char *currPath = std::getenv("PATH");
                        std::string newPath = binDir.string() + (currPath ? (":" + std::string(currPath)) : "");
#ifdef _WIN32
                        _putenv_s("PATH", newPath.c_str());
#else
                        setenv("PATH", newPath.c_str(), 1);
#endif
                    }
                }
            }
            s_cachedRuntimes = foundList;
            s_initialized = true;
        }
        return s_cachedRuntimes;
    }
}

bool isLowResFallback(std::string_view videoUrl, std::string_view quality) {
    if (quality == "360p" || quality == "Audio Only") {
        return false;
    }
    std::string itag = StreamResolver::extractItag(videoUrl);
    if (itag.empty()) {
        return false;
    }
    // Known <=360p SD itags (such as itag 18 muxed 360p30)
    static const std::unordered_set<std::string_view> kSd360Itags = {
        "17", "18", "36", "43", "91", "92", "93", "133", "134", "160", "242", "243", "278", "394", "395", "396"};
    if (kSd360Itags.contains(itag)) {
        return true;
    }
    if (quality == "1080p" || quality == "720p" || quality == "Auto") {
        // Also reject 480p SD itags when >=720p HD was requested
        static const std::unordered_set<std::string_view> kSd480Itags = {"59", "78", "94", "135", "244", "397"};
        if (kSd480Itags.contains(itag)) {
            return true;
        }
    }
    return false;
}
}  // namespace

std::string StreamResolver::extractItag(std::string_view url) {
    for (std::string_view prefix : {"?itag=", "&itag=", "/itag/"}) {
        auto pos = url.find(prefix);
        if (pos != std::string_view::npos) {
            auto start = pos + prefix.size();
            auto end = start;
            while (end < url.size() && url[end] >= '0' && url[end] <= '9') {
                ++end;
            }
            if (end > start) {
                return std::string(url.substr(start, end - start));
            }
        }
    }
    return {};
}

std::string StreamResolver::findJsRuntime() {
    auto runtimes = findAllJsRuntimes();
    return runtimes.empty() ? std::string{} : runtimes.front();
}

bool StreamResolver::hasJsRuntime() noexcept {
    return !findAllJsRuntimes().empty();
}

std::string StreamResolver::findPotProvider() {
    static std::string s_cachedPot;
    static std::mutex s_mutex;
    {
        std::lock_guard lock(s_mutex);
        if (!s_cachedPot.empty()) {
            return s_cachedPot;
        }
    }

    std::string found;
#ifdef _WIN32
    for (const char *name : {"bgutil-pot.exe", "bgutil-pot"}) {
#else
    for (const char *name : {"bgutil-pot", "bgutil-pot.exe"}) {
#endif
        for (const auto &cand : lightyt::paths::getLocalCandidatePaths(name)) {
            if (fileExists(cand)) {
#ifndef _WIN32
                if (access(cand.c_str(), X_OK) == 0)
#endif
                {
                    found = cand.string();
                    break;
                }
            }
        }
        if (!found.empty()) {
            break;
        }
    }

    if (found.empty()) {
        const char *pathEnv = std::getenv("PATH");
        if (pathEnv != nullptr) {
            std::stringstream ss(pathEnv);
            std::string dir;
#ifdef _WIN32
            constexpr char sep = ';';
            const std::array<const char *, 2> names = {"bgutil-pot.exe", "bgutil-pot"};
#else
            constexpr char sep = ':';
            const std::array<const char *, 2> names = {"bgutil-pot", "bgutil-pot.exe"};
#endif
            while (std::getline(ss, dir, sep)) {
                if (dir.empty())
                    continue;
                for (const char *binName : names) {
                    std::filesystem::path cand = std::filesystem::path(dir) / binName;
#ifndef _WIN32
                    if (access(cand.c_str(), X_OK) == 0)
#else
                    if (fileExists(cand))
#endif
                    {
                        found = cand.string();
                        break;
                    }
                }
                if (!found.empty()) {
                    break;
                }
            }
        }
    }

    if (!found.empty()) {
        std::lock_guard lock(s_mutex);
        s_cachedPot = found;
        LOG_INFO("[StreamResolver] Found POT provider: {}", found);
    }
    return found;
}

bool StreamResolver::hasPotProvider() noexcept {
    return !findPotProvider().empty();
}

bool StreamResolver::isAuthRequiredError(std::string_view err) noexcept {
    return err.find("Sign in to confirm your age") != std::string_view::npos ||
           err.find("Private video") != std::string_view::npos ||
           err.find("This video is private") != std::string_view::npos ||
           err.find("This video requires login") != std::string_view::npos ||
           err.find("Sign in to confirm") != std::string_view::npos ||
           err.find("confirm you're not a bot") != std::string_view::npos ||
           err.find("confirm you’re not a bot") != std::string_view::npos ||
           err.find("not a bot") != std::string_view::npos || err.find("Use --cookies") != std::string_view::npos ||
           err.find("Sign in") != std::string_view::npos;
}

std::vector<std::string> StreamResolver::buildYtDlpArgs(const std::string &ytdlpPath, const std::string &url,
                                                        const std::string &format, bool useCookies,
                                                        std::string_view cookieFilePath, std::string_view extractorArgs,
                                                        std::string_view formatSort) {
    std::vector<std::string> args = {ytdlpPath, "--no-warnings", "--no-playlist", "-g", "-f", format};

    if (!formatSort.empty()) {
        args.emplace_back("-S");
        args.emplace_back(formatSort);
    }

    auto jsRuntimes = findAllJsRuntimes();
    for (const auto &rt : jsRuntimes) {
        args.emplace_back("--js-runtimes");
        args.emplace_back(rt);
    }

    if (!extractorArgs.empty()) {
        args.emplace_back("--extractor-args");
        args.emplace_back(extractorArgs);
    }

    if (useCookies && !cookieFilePath.empty()) {
        args.emplace_back("--cookies");
        args.emplace_back(cookieFilePath);
    }

    args.emplace_back("--");
    args.emplace_back(url);
    return args;
}

std::string StreamResolver::extractVideoId(std::string_view videoIdOrUrl) {
    std::string_view s = lightyt::utils::stripShortsPrefix(videoIdOrUrl);

    // 1. Check ?v= or &v=
    auto vPos = s.find("v=");
    if (vPos != std::string_view::npos && (vPos == 0 || s[vPos - 1] == '?' || s[vPos - 1] == '&')) {
        auto start = vPos + 2;
        auto end = s.find_first_of("&?#", start);
        return std::string((end == std::string_view::npos) ? s.substr(start) : s.substr(start, end - start));
    }

    // 2. Check youtu.be/<id>
    auto ytbPos = s.find("youtu.be/");
    if (ytbPos != std::string_view::npos) {
        auto start = ytbPos + 9;
        auto end = s.find_first_of("&?#/", start);
        return std::string((end == std::string_view::npos) ? s.substr(start) : s.substr(start, end - start));
    }

    // 3. Check /shorts/<id>
    auto shortsPos = s.find("/shorts/");
    if (shortsPos != std::string_view::npos) {
        auto start = shortsPos + 8;
        auto end = s.find_first_of("&?#/", start);
        return std::string((end == std::string_view::npos) ? s.substr(start) : s.substr(start, end - start));
    }

    // 4. Check /embed/<id> or /v/<id>
    for (std::string_view prefix : {"/embed/", "/v/"}) {
        auto pos = s.find(prefix);
        if (pos != std::string_view::npos) {
            auto start = pos + prefix.size();
            auto end = s.find_first_of("&?#/", start);
            return std::string((end == std::string_view::npos) ? s.substr(start) : s.substr(start, end - start));
        }
    }

    // 5. If not a recognized URL format, strip any query parameters or hash
    auto queryPos = s.find_first_of("?&#");
    if (queryPos != std::string_view::npos) {
        s = s.substr(0, queryPos);
    }

    return std::string(s);
}

bool StreamResolver::resolve(std::string_view videoIdOrUrl, std::string_view quality, StreamInfo &outInfo,
                             std::string &outError, std::string_view cookieFilePath, bool allowAv1,
                             const std::atomic<bool> *cancelToken) {
    if (g_shutdownRequested.load()) {
        outError = "Stream resolution cancelled (shutting down)";
        return false;
    }

    std::string cleanId = extractVideoId(videoIdOrUrl);

    std::string key = cleanId + "|" + std::string(quality) + "|" + (allowAv1 ? "1" : "0");

    // 1. Check in-memory stream URL cache
    {
        std::lock_guard lock(g_cacheMutex);
        auto it = g_streamCache.find(key);
        if (it != g_streamCache.end()) {
            if (std::chrono::steady_clock::now() < it->second.expiresAt) {
                LOG_DEBUG("[StreamResolver] Cache hit for {}", cleanId);
                outInfo = it->second.info;
                return true;
            }
            g_streamCache.erase(it);
        }
    }

    // 2. Deduplicate concurrent in-flight resolutions for the same video
    std::shared_ptr<InFlight> inFlight;
    std::shared_ptr<InFlight> myInFlight;
    {
        std::lock_guard lock(g_inFlightMutex);
        if (g_shutdownRequested.load()) {
            outError = "Stream resolution cancelled (shutting down)";
            return false;
        }
        auto it = g_inFlight.find(key);
        if (it != g_inFlight.end()) {
            inFlight = it->second;
        } else {
            myInFlight = std::make_shared<InFlight>();
            g_inFlight[key] = myInFlight;
        }
    }

    if (inFlight) {
        LOG_DEBUG("[StreamResolver] Waiting for concurrent in-flight resolve of {}", cleanId);
        std::unique_lock waitLock(inFlight->mutex);
        while (!inFlight->done) {
            if ((cancelToken && cancelToken->load()) || g_shutdownRequested.load()) {
                outError = "Stream resolution cancelled";
                return false;
            }
            inFlight->cv.wait_for(waitLock, std::chrono::milliseconds(100));
        }
        if (inFlight->success) {
            outInfo = inFlight->info;
            return true;
        }
        outError = inFlight->error;
        return false;
    }

    struct InFlightGuard {
        std::shared_ptr<InFlight> inFlight;
        std::string key;
        bool resolved = false;
        StreamInfo info{};
        std::string err;

        InFlightGuard(std::shared_ptr<InFlight> flight, std::string k)
            : inFlight(std::move(flight)), key(std::move(k)) {}

        ~InFlightGuard() {
            if (!inFlight)
                return;
            {
                std::lock_guard lock(inFlight->mutex);
                inFlight->info = info;
                inFlight->error = err;
                inFlight->success = resolved;
                inFlight->done = true;
            }
            inFlight->cv.notify_all();

            std::lock_guard lock(g_inFlightMutex);
            g_inFlight.erase(key);
        }
    };
    InFlightGuard flightGuard(myInFlight, key);

    std::string ytdlp = findYtDlp();
    if (ytdlp.empty()) {
        outError = "yt-dlp executable not found. Please install yt-dlp or place it in the app directory.";
        LOG_WARN("[StreamResolver] {}", outError);
        flightGuard.err = outError;
        return false;
    }

    std::string url = cleanId;
    if (!url.starts_with("http://") && !url.starts_with("https://")) {
        url = "https://www.youtube.com/watch?v=" + cleanId;
    }

    std::string format = qualityToFormatString(quality, allowAv1);
    std::string formatSort = qualityToFormatSort(quality, allowAv1);

    // Isolate cookies into a temporary file so yt-dlp's cookie jar dumper never mutates the master file
    std::filesystem::path tempCookiePath;
    std::string effectiveCookiePath;
    if (!cookieFilePath.empty()) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(cookieFilePath, ec)) {
            std::filesystem::path parentDir = std::filesystem::path(cookieFilePath).parent_path();
            if (parentDir.empty() || !std::filesystem::exists(parentDir, ec)) {
                parentDir = lightyt::paths::getAppDataDir();
            }
            if (!std::filesystem::exists(parentDir, ec)) {
                std::filesystem::create_directories(parentDir, ec);
            }
            static std::atomic<uint64_t> s_cookieCounter{0};
            auto id = s_cookieCounter.fetch_add(1);
            tempCookiePath = parentDir / std::format("lightyt_ytdlp_cookies_{}_{}.txt",
                                                     std::chrono::steady_clock::now().time_since_epoch().count(), id);

            if (copyFileSecure(cookieFilePath, tempCookiePath)) {
                effectiveCookiePath = tempCookiePath.string();
            } else {
                std::filesystem::remove(tempCookiePath, ec);
                tempCookiePath.clear();
                effectiveCookiePath = std::string(cookieFilePath);
            }
        }
    }

    struct TempCookieGuard {
        std::filesystem::path path;
        ~TempCookieGuard() {
            if (!path.empty()) {
                std::error_code ec;
                std::filesystem::remove(path, ec);
            }
        }
    } cookieGuard{.path = tempCookiePath};

    auto tryYtDlp = [&](bool useCookies, std::string_view extractorArgs,
                        int timeoutMs = 12000) -> lightyt::ProcessResult {
        std::vector<std::string> args =
            buildYtDlpArgs(ytdlp, url, format, useCookies, effectiveCookiePath, extractorArgs, formatSort);
        return lightyt::Process::run(args, std::chrono::milliseconds(timeoutMs), cancelToken);
    };

    auto parseResult = [](const lightyt::ProcessResult &res, StreamInfo &info) -> bool {
        if (res.exitCode != 0 || res.stdOut.empty()) {
            return false;
        }
        std::istringstream stream(res.stdOut);
        std::string line;
        std::vector<std::string> urls;
        while (std::getline(stream, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) {
                line.pop_back();
            }
            if (!line.empty() && (line.starts_with("http://") || line.starts_with("https://"))) {
                urls.push_back(line);
            }
        }
        if (!urls.empty()) {
            info.videoUrl = urls[0];
            info.audioUrl = (urls.size() > 1) ? urls[1] : "";
            info.isDirectStream = true;
            info.videoItag = StreamResolver::extractItag(info.videoUrl);
            info.audioItag = StreamResolver::extractItag(info.audioUrl);
            return true;
        }
        return false;
    };

    LOG_INFO("[StreamResolver] Resolving stream for {} (quality: {})...", url, quality);

    const bool hasCookies = !effectiveCookiePath.empty();

    constexpr std::string_view FAST_DEFAULT_ARGS = "youtube:player_skip=configs;player_client=default";
    constexpr std::string_view ANON_HD_ARGS = "youtube:player_client=tv_downgraded,web_embedded,default";
    constexpr std::string_view AUTH_HD_ARGS = "youtube:player_client=web_embedded,tv_downgraded,web_safari,web";

    // 1. Primary fast un-throttled extraction (anonymous VisionOS + Web default clients)
    auto result = tryYtDlp(false, FAST_DEFAULT_ARGS, 12000);

    if (result.cancelled || g_shutdownRequested.load()) {
        outError = "Stream resolution cancelled";
        flightGuard.err = outError;
        return false;
    }

    bool ok = parseResult(result, outInfo);

    // 2. If fast extraction failed or only yielded an SD fallback (e.g. "made for kids" video where
    // VisionOS is unplayable and Web fell back to itag 18), try tv_downgraded and web_embedded
    if (!ok || outInfo.videoUrl.empty() || isLowResFallback(outInfo.videoUrl, quality)) {
        StreamInfo hdInfo;
        auto hdRes = tryYtDlp(false, ANON_HD_ARGS, 12000);
        if (hdRes.cancelled || g_shutdownRequested.load()) {
            outError = "Stream resolution cancelled";
            flightGuard.err = outError;
            return false;
        }
        if (parseResult(hdRes, hdInfo) && !hdInfo.videoUrl.empty()) {
            if (!ok || outInfo.videoUrl.empty() || !isLowResFallback(hdInfo.videoUrl, quality)) {
                outInfo = std::move(hdInfo);
                ok = true;
            }
        } else if (!ok) {
            result = std::move(hdRes);
        }
    }

    // 3. If anonymous extraction failed (e.g. age-restricted, private, or members-only) or yielded
    // only an SD fallback, and cookies are available, retry with authenticated HD clients
    if (hasCookies && (!ok || outInfo.videoUrl.empty() || isLowResFallback(outInfo.videoUrl, quality))) {
        LOG_INFO("[StreamResolver] Anonymous extraction incomplete (exit: {}, low-res: {}). Retrying with cookies...",
                 result.exitCode, isLowResFallback(outInfo.videoUrl, quality));
        StreamInfo authInfo;
        auto authRes = tryYtDlp(true, AUTH_HD_ARGS, 15000);
        if (authRes.cancelled || g_shutdownRequested.load()) {
            outError = "Stream resolution cancelled";
            flightGuard.err = outError;
            return false;
        }
        if (parseResult(authRes, authInfo) && !authInfo.videoUrl.empty()) {
            if (!ok || outInfo.videoUrl.empty() || !isLowResFallback(authInfo.videoUrl, quality)) {
                outInfo = std::move(authInfo);
                ok = true;
            }
        } else if (!ok) {
            result = std::move(authRes);
        }
    }

    if (!ok || outInfo.videoUrl.empty()) {
        outError = !result.stdErr.empty()
                       ? result.stdErr
                       : "yt-dlp returned no stream URLs (Exit code: " + std::to_string(result.exitCode) + ")";
        LOG_WARN("[StreamResolver] Resolution failed: {}", outError);
        flightGuard.err = outError;
        return false;
    }

    // Store in cache with dynamic expiry parsed from stream URL (typically ~6 hours)
    {
        std::lock_guard lock(g_cacheMutex);
        if (g_streamCache.size() >= 64) {
            auto now = std::chrono::steady_clock::now();
            std::erase_if(g_streamCache, [now](const auto &item) { return now >= item.second.expiresAt; });
        }
        g_streamCache[key] = {outInfo, parseUrlExpiry(outInfo.videoUrl)};
    }

    std::string_view resolvedItag = extractItag(outInfo.videoUrl);
    LOG_INFO("[StreamResolver] Resolved streams successfully (itag: {}, video + {}audio)",
             resolvedItag.empty() ? "auto" : resolvedItag, outInfo.audioUrl.empty() ? "no " : "");
    flightGuard.resolved = true;
    flightGuard.info = outInfo;
    return true;
}

void StreamResolver::prefetch(std::string_view videoIdOrUrl, std::string_view quality, std::string_view cookieFilePath,
                              bool allowAv1) {
    if (g_shutdownRequested.load())
        return;

    std::string cleanId = extractVideoId(videoIdOrUrl);
    if (cleanId.empty())
        return;

    std::string key = cleanId + "|" + std::string(quality) + "|" + (allowAv1 ? "1" : "0");

    // 1. Check in-memory stream URL cache
    {
        std::lock_guard lock(g_cacheMutex);
        auto it = g_streamCache.find(key);
        if (it != g_streamCache.end()) {
            if (std::chrono::steady_clock::now() < it->second.expiresAt) {
                return;  // Already cached
            }
        }
    }

    // 2. Check if already in flight
    {
        std::lock_guard lock(g_inFlightMutex);
        if (g_inFlight.find(key) != g_inFlight.end()) {
            return;  // Already resolving
        }
    }

    getPrefetchQueue().enqueue(cleanId, std::string(quality), std::string(cookieFilePath), allowAv1);
}

