#pragma once

#include <QCache>
#include <QImage>
#include <QObject>
#include <QPixmap>
#include <QThreadPool>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct AvatarKey {
    std::string url;
    int size = 36;
    bool operator==(const AvatarKey &other) const noexcept { return size == other.size && url == other.url; }
};

inline size_t qHash(const AvatarKey &key, size_t seed = 0) noexcept {
    size_t h = std::hash<std::string>{}(key.url);
    return seed ^ (h + 0x9e3779b9 + (static_cast<size_t>(key.size) << 6) + (static_cast<size_t>(key.size) >> 2));
}

template<>
struct std::hash<AvatarKey> {
    size_t operator()(const AvatarKey &key) const noexcept {
        size_t h = std::hash<std::string>{}(key.url);
        return h ^ (static_cast<size_t>(key.size) + 0x9e3779b9 + (h << 6) + (h >> 2));
    }
};

class ThumbnailLoader : public QObject {
    Q_OBJECT
public:
    explicit ThumbnailLoader(QObject *parent = nullptr);
    ~ThumbnailLoader() override;

    // Decode image from raw bytes with format sniffing (WebP, JPEG, PNG fallback)
    [[nodiscard]] static QImage decodeImageFromBytes(const uint8_t *data, size_t size);
    [[nodiscard]] static QImage decodeImageFromBytes(const std::vector<uint8_t> &bytes);
    [[nodiscard]] static QImage decodeImageFromBytes(const std::string &rawData);

    // Request thumbnail download/decode (emits thumbnailLoaded when ready)
    void requestThumbnail(const std::string &url);

    // Request channel avatar download/decode (emits avatarLoaded when ready)
    void requestAvatar(const std::string &url, int size = 36);

    // Cancel queued downloads when switching feeds
    void cancelPending();

    // Check if thumbnail is already in memory cache
    [[nodiscard]] bool hasCached(const std::string &url) const;
    [[nodiscard]] QPixmap getCached(const std::string &url) const;

    // Check if avatar is already in memory cache
    [[nodiscard]] bool hasCachedAvatar(const std::string &url, int size = 36) const;
    [[nodiscard]] QPixmap getCachedAvatar(const std::string &url, int size = 36) const;

    // Called on GUI thread when a task completes
    void onTaskSucceeded(const std::string &url, const QPixmap &pixmap);
    void onTaskFailed(const std::string &url);

    void onAvatarTaskSucceeded(const std::string &url, int size, const QPixmap &pixmap);
    void onAvatarTaskFailed(const std::string &url);

signals:
    void thumbnailLoaded(const std::string &url, const QPixmap &pixmap);
    void avatarLoaded(const std::string &url, const QPixmap &pixmap);

private:
    QThreadPool m_threadPool;
    mutable std::mutex m_cacheMutex;
    QCache<QString, QPixmap> m_memoryCache;         // 24 MB memory limit
    QCache<AvatarKey, QPixmap> m_avatarCache{256};  // Circular avatar cache
    std::unordered_set<std::string> m_inFlight;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> m_failedUrls;
    std::shared_ptr<std::atomic<bool>> m_cancelToken;
};
