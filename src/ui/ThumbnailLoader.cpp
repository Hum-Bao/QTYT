#include "ThumbnailLoader.h"

#include "../core/HttpClient.h"
#include "../core/ThumbnailCache.h"
#include "../utils/Logger.h"
#include "../utils/StringUtils.h"

#include <QCoreApplication>
#include <QImage>
#include <QMetaObject>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QRunnable>

namespace {

[[nodiscard]] std::string extractYtImgVideoId(std::string_view url) {
    for (std::string_view prefix : {"i.ytimg.com/vi/", "i.ytimg.com/vi_webp/"}) {
        auto pos = url.find(prefix);
        if (pos != std::string_view::npos) {
            auto start = pos + prefix.size();
            auto end = url.find('/', start);
            if (end != std::string_view::npos && end > start) {
                return std::string(url.substr(start, end - start));
            }
        }
    }
    return {};
}

class ThumbnailTask : public QRunnable {
public:
    ThumbnailTask(std::string url, ThumbnailLoader *loader, std::shared_ptr<std::atomic<bool>> cancelToken)
        : m_url(std::move(url)), m_loader(loader), m_cancelToken(std::move(cancelToken)) {
        setAutoDelete(true);
    }

    void run() override {
        if (m_cancelToken && m_cancelToken->load()) {
            return;
        }

        std::vector<uint8_t> buffer;
        bool haveData = ThumbnailCache::instance().readDisk(m_url, buffer);

        auto tryDownloadAndDecode = [&](const std::string &targetUrl, bool cacheUnderOriginal) -> QImage {
            std::vector<uint8_t> dlBuf;
            if (!HttpClient::downloadBytes(targetUrl, dlBuf, 8, 5 * 1024 * 1024, m_cancelToken.get()) ||
                dlBuf.empty()) {
                return QImage();
            }
            QImage img = ThumbnailLoader::decodeImageFromBytes(dlBuf);
            if (!img.isNull()) {
                (void)ThumbnailCache::instance().writeDisk(cacheUnderOriginal ? m_url : targetUrl, dlBuf);
            }
            return img;
        };

        QImage image;
        if (haveData && !buffer.empty()) {
            image = ThumbnailLoader::decodeImageFromBytes(buffer);
            if (image.isNull()) {
                ThumbnailCache::instance().removeDisk(m_url);
            }
        }

        if (image.isNull()) {
            if (m_cancelToken && m_cancelToken->load())
                return;
            image = tryDownloadAndDecode(m_url, true);
        }

        // Automatic fallback to JPEG if WebP or custom format fails to download or decode
        if (image.isNull()) {
            std::string jpegUrl = lightyt::utils::convertThumbnailUrlToJpeg(m_url);
            if (jpegUrl != m_url) {
                if (m_cancelToken && m_cancelToken->load())
                    return;
                image = tryDownloadAndDecode(jpegUrl, true);
            }
            if (image.isNull()) {
                std::string vid = extractYtImgVideoId(m_url);
                if (!vid.empty()) {
                    for (const char *fallbackName : {"hqdefault.jpg", "mqdefault.jpg"}) {
                        if (m_cancelToken && m_cancelToken->load())
                            return;
                        std::string fallbackUrl = "https://i.ytimg.com/vi/" + vid + "/" + fallbackName;
                        if (fallbackUrl != m_url && fallbackUrl != jpegUrl) {
                            image = tryDownloadAndDecode(fallbackUrl, true);
                            if (!image.isNull())
                                break;
                        }
                    }
                }
            }
        }

        if (m_cancelToken && m_cancelToken->load()) {
            return;
        }

        if (image.isNull()) {
            LOG_DEBUG("[ThumbnailLoader] Failed to download/decode thumbnail: {}", m_url);
            QMetaObject::invokeMethod(
                qApp,
                [loader = m_loader, cancel = m_cancelToken, url = m_url]() {
                    if (loader && (!cancel || !cancel->load())) {
                        loader->onTaskFailed(url);
                    }
                },
                Qt::QueuedConnection);
            return;
        }

        // Render high-resolution 16:9 card surface (640x360) off-thread
        constexpr int TARGET_W = 640;
        constexpr int TARGET_H = 360;
        QImage cardImg(TARGET_W, TARGET_H, QImage::Format_ARGB32_Premultiplied);
        cardImg.fill(QColor(0x18, 0x18, 0x18));
        {
            QPainter painter(&cardImg);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

            if (image.height() > image.width()) {
                // Vertical 9:16 Shorts thumbnail centered over dark pillarbox
                QImage fg = image.scaled(TARGET_W, TARGET_H, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                int fgX = (TARGET_W - fg.width()) / 2;
                int fgY = (TARGET_H - fg.height()) / 2;
                painter.drawImage(fgX, fgY, fg);
            } else {
                // Standard landscape 16:9 thumbnail (center-crops 4:3 letterbox bars if hqdefault)
                QImage scaled =
                    image.scaled(TARGET_W, TARGET_H, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                if (scaled.width() > TARGET_W || scaled.height() > TARGET_H) {
                    int x = std::max(0, (scaled.width() - TARGET_W) / 2);
                    int y = std::max(0, (scaled.height() - TARGET_H) / 2);
                    scaled = scaled.copy(x, y, TARGET_W, TARGET_H);
                }
                painter.drawImage(0, 0, scaled);
            }
        }

        QMetaObject::invokeMethod(
            qApp,
            [loader = m_loader, cancel = m_cancelToken, url = m_url, img = std::move(cardImg)]() {
                if (loader && (!cancel || !cancel->load())) {
                    QPixmap pixmap = QPixmap::fromImage(img);
                    loader->onTaskSucceeded(url, pixmap);
                }
            },
            Qt::QueuedConnection);
    }

private:
    std::string m_url;
    QPointer<ThumbnailLoader> m_loader;
    std::shared_ptr<std::atomic<bool>> m_cancelToken;
};

class AvatarTask : public QRunnable {
public:
    AvatarTask(std::string url, int size, ThumbnailLoader *loader, std::shared_ptr<std::atomic<bool>> cancelToken)
        : m_url(std::move(url)), m_size(size), m_loader(loader), m_cancelToken(std::move(cancelToken)) {
        setAutoDelete(true);
    }

    void run() override {
        if (m_cancelToken && m_cancelToken->load())
            return;

        std::vector<uint8_t> buffer;
        bool haveData = ThumbnailCache::instance().readDisk(m_url, buffer);

        auto tryDownloadAndDecodeAvatar = [&](const std::string &targetUrl) -> QImage {
            std::vector<uint8_t> dlBuf;
            if (!HttpClient::downloadBytes(targetUrl, dlBuf, 8, 5 * 1024 * 1024, m_cancelToken.get()) ||
                dlBuf.empty()) {
                return QImage();
            }
            QImage img = ThumbnailLoader::decodeImageFromBytes(dlBuf);
            if (!img.isNull()) {
                (void)ThumbnailCache::instance().writeDisk(m_url, dlBuf);
            }
            return img;
        };

        QImage image;
        if (haveData && !buffer.empty()) {
            image = ThumbnailLoader::decodeImageFromBytes(buffer);
            if (image.isNull()) {
                ThumbnailCache::instance().removeDisk(m_url);
            }
        }

        if (image.isNull()) {
            if (m_cancelToken && m_cancelToken->load())
                return;
            image = tryDownloadAndDecodeAvatar(m_url);
        }

        // Seamless fallback to JPEG if WebP avatar fails to download or decode (e.g. -rw -> -rj)
        if (image.isNull()) {
            std::string jpegUrl = lightyt::utils::convertAvatarUrlToJpeg(m_url);
            if (jpegUrl != m_url) {
                if (m_cancelToken && m_cancelToken->load())
                    return;
                image = tryDownloadAndDecodeAvatar(jpegUrl);
            }
        }

        if (m_cancelToken && m_cancelToken->load())
            return;

        if (image.isNull()) {
            QMetaObject::invokeMethod(
                qApp,
                [loader = m_loader, cancel = m_cancelToken, url = m_url, size = m_size]() {
                    if (loader && (!cancel || !cancel->load())) {
                        loader->onAvatarTaskFailed(url, size);
                    }
                },
                Qt::QueuedConnection);
            return;
        }

        if (m_cancelToken && m_cancelToken->load())
            return;

        // Render circular clipped avatar off-thread at 2x resolution for crisp High-DPI rendering
        const int renderSize = m_size * 2;
        QImage circularImg(renderSize, renderSize, QImage::Format_ARGB32_Premultiplied);
        circularImg.fill(Qt::transparent);
        {
            QPainter painter(&circularImg);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

            QPainterPath clip;
            clip.addEllipse(0, 0, renderSize, renderSize);
            painter.setClipPath(clip);

            QImage scaled =
                image.scaled(renderSize, renderSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            int x = std::max(0, (scaled.width() - renderSize) / 2);
            int y = std::max(0, (scaled.height() - renderSize) / 2);
            painter.drawImage(0, 0, scaled.copy(x, y, renderSize, renderSize));
        }

        QMetaObject::invokeMethod(
            qApp,
            [loader = m_loader, cancel = m_cancelToken, url = m_url, size = m_size, img = std::move(circularImg)]() {
                if (loader && (!cancel || !cancel->load())) {
                    QPixmap pixmap = QPixmap::fromImage(img);
                    pixmap.setDevicePixelRatio(2.0);
                    loader->onAvatarTaskSucceeded(url, size, pixmap);
                }
            },
            Qt::QueuedConnection);
    }

private:
    std::string m_url;
    int m_size = 36;
    QPointer<ThumbnailLoader> m_loader;
    std::shared_ptr<std::atomic<bool>> m_cancelToken;
};

}  // namespace

QImage ThumbnailLoader::decodeImageFromBytes(const uint8_t *data, size_t size) {
    if (!data || size == 0) {
        return QImage();
    }

    QImage image;
    if (image.loadFromData(data, static_cast<int>(size))) {
        return image;
    }

    // Check for WebP magic bytes (RIFF....WEBP) or JPEG and retry with explicit format hints
    if (size >= 12 && data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F' &&
        data[8] == 'W' && data[9] == 'E' && data[10] == 'B' && data[11] == 'P') {
        if (image.loadFromData(data, static_cast<int>(size), "WEBP")) {
            return image;
        }
    } else if (size >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
        if (image.loadFromData(data, static_cast<int>(size), "JPG")) {
            return image;
        }
    } else if (size >= 8 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') {
        if (image.loadFromData(data, static_cast<int>(size), "PNG")) {
            return image;
        }
    }

    return QImage();
}

QImage ThumbnailLoader::decodeImageFromBytes(const std::vector<uint8_t> &bytes) {
    return decodeImageFromBytes(bytes.data(), bytes.size());
}

QImage ThumbnailLoader::decodeImageFromBytes(const std::string &rawData) {
    return decodeImageFromBytes(reinterpret_cast<const uint8_t *>(rawData.data()), rawData.size());
}

ThumbnailLoader::ThumbnailLoader(QObject *parent)
    : QObject(parent), m_cancelToken(std::make_shared<std::atomic<bool>>(false)) {
    m_threadPool.setMaxThreadCount(3);
    m_memoryCache.setMaxCost(64 * 1024 * 1024);  // 64 MB memory cap for HD 640x360 thumbnails
}

ThumbnailLoader &ThumbnailLoader::instance() {
    static ThumbnailLoader s_instance;
    return s_instance;
}

void ThumbnailLoader::shutdown() {
    cancelPending();
    m_threadPool.waitForDone();
}

ThumbnailLoader::~ThumbnailLoader() {
    cancelPending();
    m_threadPool.waitForDone();
}

void ThumbnailLoader::onTaskSucceeded(const std::string &url, const QPixmap &pixmap) {
    if (!url.empty()) {
        {
            std::lock_guard lock(m_cacheMutex);
            m_inFlight.erase(url);
            m_failedUrls.erase(url);
            int cost = pixmap.width() * pixmap.height() * 4;
            m_memoryCache.insert(QString::fromUtf8(url.c_str()), new QPixmap(pixmap), cost);
        }
        emit thumbnailLoaded(url, pixmap);
    }
}

void ThumbnailLoader::onTaskFailed(const std::string &url) {
    std::lock_guard lock(m_cacheMutex);
    m_inFlight.erase(url);
    m_failedUrls[url] = std::chrono::steady_clock::now();
}

void ThumbnailLoader::requestThumbnail(const std::string &url) {
    if (url.empty())
        return;

    std::shared_ptr<std::atomic<bool>> token;
    {
        std::lock_guard lock(m_cacheMutex);
        QString qUrl = QString::fromUtf8(url.c_str());
        if (m_memoryCache.contains(qUrl) || m_inFlight.contains(url)) {
            return;
        }

        // Failure cooldown: avoid spamming network for failed thumbnails within 60s
        auto it = m_failedUrls.find(url);
        if (it != m_failedUrls.end()) {
            auto elapsed =
                std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - it->second).count();
            if (elapsed < 60) {
                return;
            }
        }

        m_inFlight.insert(url);
        token = m_cancelToken;
    }

    m_threadPool.start(new ThumbnailTask(url, this, std::move(token)));
}

void ThumbnailLoader::cancelPending() {
    m_threadPool.clear();
    std::lock_guard lock(m_cacheMutex);
    if (m_cancelToken) {
        m_cancelToken->store(true);
    }
    m_cancelToken = std::make_shared<std::atomic<bool>>(false);
    m_inFlight.clear();
}

bool ThumbnailLoader::hasCached(const std::string &url) const {
    std::lock_guard lock(m_cacheMutex);
    return m_memoryCache.contains(QString::fromUtf8(url.c_str()));
}

QPixmap ThumbnailLoader::getCached(const std::string &url) const {
    std::lock_guard lock(m_cacheMutex);
    QPixmap *p = m_memoryCache.object(QString::fromUtf8(url.c_str()));
    return p ? *p : QPixmap();
}

void ThumbnailLoader::onAvatarTaskSucceeded(const std::string &url, int size, const QPixmap &pixmap) {
    if (!url.empty()) {
        std::string flightKey = "avatar:" + std::to_string(size) + ":" + url;
        {
            std::lock_guard lock(m_cacheMutex);
            m_inFlight.erase(flightKey);
            m_failedUrls.erase(flightKey);
            m_avatarCache.insert(AvatarKey{url, size}, new QPixmap(pixmap), 1);
        }
        emit avatarLoaded(url, pixmap);
        emit avatarReady(url, size, pixmap);
    }
}

void ThumbnailLoader::onAvatarTaskFailed(const std::string &url, int size) {
    std::string flightKey = "avatar:" + std::to_string(size) + ":" + url;
    std::lock_guard lock(m_cacheMutex);
    m_inFlight.erase(flightKey);
    m_failedUrls[flightKey] = std::chrono::steady_clock::now();
}

QPixmap ThumbnailLoader::requestAvatar(const std::string &url, int size) {
    if (url.empty())
        return {};

    std::shared_ptr<std::atomic<bool>> token;
    {
        std::lock_guard lock(m_cacheMutex);
        AvatarKey key{url, size};
        if (QPixmap *cached = m_avatarCache.object(key)) {
            return *cached;
        }
        std::string flightKey = "avatar:" + std::to_string(size) + ":" + url;
        if (m_inFlight.contains(flightKey)) {
            return {};
        }

        auto it = m_failedUrls.find(flightKey);
        if (it != m_failedUrls.end()) {
            auto elapsed =
                std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - it->second).count();
            if (elapsed < 60)
                return {};
        }

        m_inFlight.insert(flightKey);
        token = m_cancelToken;
    }

    m_threadPool.start(new AvatarTask(url, size, this, std::move(token)));
    return {};
}

bool ThumbnailLoader::hasCachedAvatar(const std::string &url, int size) const {
    std::lock_guard lock(m_cacheMutex);
    return m_avatarCache.contains(AvatarKey{url, size});
}

QPixmap ThumbnailLoader::getCachedAvatar(const std::string &url, int size) const {
    std::lock_guard lock(m_cacheMutex);
    QPixmap *p = m_avatarCache.object(AvatarKey{url, size});
    return p ? *p : QPixmap();
}
