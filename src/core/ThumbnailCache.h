#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

class ThumbnailCache {
public:
    [[nodiscard]] static ThumbnailCache &instance();

    // Check if thumbnail exists in local disk cache
    [[nodiscard]] bool hasDisk(std::string_view url) const;

    // Read raw image bytes from local disk cache (lock-free)
    [[nodiscard]] bool readDisk(std::string_view url, std::vector<uint8_t> &outData);

    // Write raw image bytes to local disk cache atomically (writes to .tmp and renames)
    [[nodiscard]] bool writeDisk(std::string_view url, const std::vector<uint8_t> &data);

    // Remove thumbnail from local disk cache
    bool removeDisk(std::string_view url);

    // Prune cache to stay within size and age limits
    void prune(size_t maxBytes = 150ULL * 1024 * 1024, int maxAgeDays = 30);

    // Get disk cache directory path
    [[nodiscard]] const std::filesystem::path &getCacheDir() const noexcept { return m_cacheDir; }

    // Public URL hasher for diagnostics and tests
    [[nodiscard]] static std::string hashUrl(std::string_view url);

    ThumbnailCache(const ThumbnailCache &) = delete;
    ThumbnailCache &operator=(const ThumbnailCache &) = delete;

private:
    ThumbnailCache();
    ~ThumbnailCache();

    std::filesystem::path m_cacheDir;
    std::atomic<int> m_writesSincePrune{0};
    std::atomic<bool> m_isPruning{false};
    std::atomic<bool> m_shuttingDown{false};
};
