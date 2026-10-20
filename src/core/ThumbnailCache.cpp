#include "ThumbnailCache.h"

#include "utils/AppPaths.h"
#include "utils/Logger.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

ThumbnailCache &ThumbnailCache::instance() {
    static ThumbnailCache s_instance;
    return s_instance;
}

ThumbnailCache::ThumbnailCache() {
    m_cacheDir = lightyt::paths::getThumbnailCacheDir();
    lightyt::paths::ensureDirExists(m_cacheDir);
    LOG_INFO("[ThumbnailCache] Initialized with disk directory: {}", m_cacheDir.string());
}

ThumbnailCache::~ThumbnailCache() {
    m_shuttingDown.store(true);
    while (m_isPruning.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

std::string ThumbnailCache::hashUrl(std::string_view url) {
    // 64-bit FNV-1a hash
    uint64_t hash = 14695981039346656037ULL;
    for (char c : url) {
        hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
        hash *= 1099511628211ULL;
    }
    return std::format("{:016x}", hash);
}

bool ThumbnailCache::hasDisk(std::string_view url) const {
    auto path = m_cacheDir / (hashUrl(url) + ".jpg");
    std::error_code ec;
    return fs::exists(path, ec);
}

bool ThumbnailCache::readDisk(std::string_view url, std::vector<uint8_t> &outData) {
    auto path = m_cacheDir / (hashUrl(url) + ".jpg");

    std::error_code ec;
    auto sz = fs::file_size(path, ec);
    if (ec || sz == 0) {
        return false;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    outData.resize(static_cast<size_t>(sz));
    bool readSuccess =
        static_cast<bool>(file.read(reinterpret_cast<char *>(outData.data()), static_cast<std::streamsize>(sz)));
    if (readSuccess) {
        auto now = fs::file_time_type::clock::now();
        auto mtime = fs::last_write_time(path, ec);
        if (!ec && (now > mtime) && (now - mtime > std::chrono::hours(1))) {
            fs::last_write_time(path, now, ec);
        }
    }
    return readSuccess;
}

bool ThumbnailCache::writeDisk(std::string_view url, const std::vector<uint8_t> &data) {
    if (data.empty()) {
        return false;
    }

    lightyt::paths::ensureDirExists(m_cacheDir);
    static std::atomic<uint64_t> s_tempCounter{0};
    std::string hash = hashUrl(url);
    auto finalPath = m_cacheDir / (hash + ".jpg");
    auto tempPath = m_cacheDir / std::format("{}_{}.tmp", hash, s_tempCounter.fetch_add(1));

    {
        std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            return false;
        }

        file.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
        file.flush();
        if (!file.good()) {
            std::error_code ec;
            fs::remove(tempPath, ec);
            return false;
        }
    }

    std::error_code ec;
    fs::rename(tempPath, finalPath, ec);
    if (ec) {
        // Fallback for Windows if destination exists
        fs::remove(finalPath, ec);
        fs::rename(tempPath, finalPath, ec);
    }
    if (!ec) {
        if (m_writesSincePrune.fetch_add(1) >= 50) {
            m_writesSincePrune.store(0);
            if (!m_shuttingDown.load()) {
                bool expected = false;
                if (m_isPruning.compare_exchange_strong(expected, true)) {
                    std::thread([this]() {
                        prune();
                        m_isPruning.store(false);
                    }).detach();
                }
            }
        }
        return true;
    }
    return false;
}

bool ThumbnailCache::removeDisk(std::string_view url) {
    auto path = m_cacheDir / (hashUrl(url) + ".jpg");
    std::error_code ec;
    return fs::remove(path, ec);
}

void ThumbnailCache::prune(size_t maxBytes, int maxAgeDays) {
    std::error_code ec;
    if (!fs::exists(m_cacheDir, ec)) {
        return;
    }

    struct FileEntry {
        fs::path path;
        size_t size = 0;
        fs::file_time_type writeTime;
    };

    std::vector<FileEntry> files;
    size_t totalBytes = 0;
    auto now = fs::file_time_type::clock::now();
    auto maxAgeDuration = std::chrono::hours(maxAgeDays * 24);

    for (const auto &entry : fs::directory_iterator(m_cacheDir, ec)) {
        if (m_shuttingDown.load()) {
            return;
        }
        if (entry.is_regular_file(ec)) {
            const auto &path = entry.path();
            if (path.extension() == ".tmp") {
                fs::remove(path, ec);
                continue;
            }
            if (path.extension() == ".jpg") {
                auto sz = entry.file_size(ec);
                auto wt = entry.last_write_time(ec);
                if (now - wt > maxAgeDuration) {
                    fs::remove(path, ec);
                    continue;
                }
                files.push_back({path, sz, wt});
                totalBytes += sz;
            }
        }
    }

    if (totalBytes > maxBytes) {
        // Sort oldest first
        std::ranges::sort(files, [](const FileEntry &a, const FileEntry &b) { return a.writeTime < b.writeTime; });

        for (const auto &f : files) {
            if (m_shuttingDown.load()) {
                return;
            }
            if (totalBytes <= maxBytes) {
                break;
            }
            if (fs::remove(f.path, ec)) {
                totalBytes -= f.size;
            }
        }
    }
}
