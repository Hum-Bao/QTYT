#pragma once

#include "CookieManager.h"
#include "HttpClient.h"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <queue>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace lightyt {

class WatchHistoryTracker {
public:
    using HeaderBuilder = std::function<std::vector<std::string>(bool authenticated, std::string_view referer)>;
    using ContextBuilder = std::function<nlohmann::json(bool anonymous)>;

    WatchHistoryTracker(std::shared_ptr<CookieManager> cookieManager, HeaderBuilder headerBuilder,
                        ContextBuilder contextBuilder);
    ~WatchHistoryTracker();

    WatchHistoryTracker(const WatchHistoryTracker &) = delete;
    WatchHistoryTracker &operator=(const WatchHistoryTracker &) = delete;

    void recordWatchHistory(std::string_view videoId);
    void recordWatchTime(std::string_view videoId, double startSec, double endSec, double durationSec,
                         bool isFinished = false);
    void recordWatchTime(std::string_view videoId, double positionSec, double durationSec, bool isFinished = false);

    // Static beacon URL and nonce helpers
    [[nodiscard]] static std::string generatePlaybackNonce();
    [[nodiscard]] static std::string buildPlaybackBeaconUrl(std::string_view baseUrl, std::string_view cpn);
    [[nodiscard]] static std::string buildWatchtimeBeaconUrl(std::string_view baseUrl, std::string_view cpn,
                                                             double startSec, double endSec, double durationSec,
                                                             bool isFinished = false);
    [[nodiscard]] static std::string buildWatchtimeBeaconUrl(std::string_view baseUrl, std::string_view cpn,
                                                             double positionSec, double durationSec,
                                                             bool isFinished = false);

private:
    std::shared_ptr<CookieManager> m_cookieManager;
    HeaderBuilder m_headerBuilder;
    ContextBuilder m_contextBuilder;

    struct PlaybackTrackingSession {
        std::string videoId;
        std::string cpn;
        std::string playbackUrl;
        std::string watchtimeUrl;
        std::chrono::steady_clock::time_point startTime;
        double lastReportedSec = 0.0;
        std::chrono::steady_clock::time_point lastReportTime;
    };

    mutable std::mutex m_sessionMutex;
    PlaybackTrackingSession m_activeSession;

    mutable std::mutex m_trackingMutex;
    std::condition_variable m_trackingCv;
    std::queue<std::function<void()>> m_trackingTasks;
    bool m_trackingStop = false;
    std::thread m_trackingWorker;

    void trackingWorkerLoop();
    void enqueueTrackingTask(std::function<void()> task);

    [[nodiscard]] HttpResponse postInnerTube(const std::string &url, const std::string &body,
                                             const std::vector<std::string> &headers, int timeoutSec = 10) const;
    [[nodiscard]] HttpResponse getInnerTube(const std::string &url, const std::vector<std::string> &headers,
                                            int timeoutSec = 10) const;
};

}  // namespace lightyt
