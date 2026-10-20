#include "WatchHistoryTracker.h"

#include "CookieManager.h"
#include "HttpClient.h"
#include "InnerTubeNetwork.h"
#include "utils/Logger.h"
#include "utils/StringUtils.h"

#include <chrono>
#include <format>
#include <random>

namespace lightyt {

WatchHistoryTracker::WatchHistoryTracker(std::shared_ptr<CookieManager> cookieManager, HeaderBuilder headerBuilder,
                                         ContextBuilder contextBuilder)
    : m_cookieManager(std::move(cookieManager)), m_headerBuilder(std::move(headerBuilder)),
      m_contextBuilder(std::move(contextBuilder)) {
    m_trackingWorker = std::thread([this]() { trackingWorkerLoop(); });
}

WatchHistoryTracker::~WatchHistoryTracker() {
    {
        std::unique_lock lock(m_trackingMutex);
        m_trackingStop = true;
    }
    m_trackingCv.notify_all();
    if (m_trackingWorker.joinable()) {
        m_trackingWorker.join();
    }
}

std::string WatchHistoryTracker::generatePlaybackNonce() {
    static constexpr std::string_view CHARS = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_";
    thread_local std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(0, CHARS.size() - 1);
    std::string nonce;
    nonce.reserve(16);
    for (size_t i = 0; i < 16; ++i) {
        nonce.push_back(CHARS[dist(rng)]);
    }
    return nonce;
}

std::string WatchHistoryTracker::buildPlaybackBeaconUrl(std::string_view baseUrl, std::string_view cpn) {
    std::string url(baseUrl);
    utils::appendOrReplaceUrlParam(url, "ns", "yt");
    utils::appendOrReplaceUrlParam(url, "ver", "2");
    if (!cpn.empty()) {
        utils::appendOrReplaceUrlParam(url, "cpn", cpn);
    }
    return url;
}

std::string WatchHistoryTracker::buildWatchtimeBeaconUrl(std::string_view baseUrl, std::string_view cpn,
                                                         double startSec, double endSec, double durationSec,
                                                         bool isFinished) {
    std::string url(baseUrl);
    utils::appendOrReplaceUrlParam(url, "ns", "yt");
    utils::appendOrReplaceUrlParam(url, "ver", "2");
    if (!cpn.empty()) {
        utils::appendOrReplaceUrlParam(url, "cpn", cpn);
    }
    utils::appendOrReplaceUrlParam(url, "state", isFinished ? "completed" : "playing");
    double st = std::max(0.0, startSec);
    double et = std::max(st, endSec);
    std::string posStr = std::format("{:.3f}", et);
    std::string startStr = std::format("{:.3f}", st);
    utils::appendOrReplaceUrlParam(url, "cmt", posStr);
    utils::appendOrReplaceUrlParam(url, "rt", posStr);
    utils::appendOrReplaceUrlParam(url, "st", startStr);
    utils::appendOrReplaceUrlParam(url, "et", posStr);
    if (durationSec > 0.0) {
        utils::appendOrReplaceUrlParam(url, "len", std::format("{:.3f}", durationSec));
    }
    return url;
}

std::string WatchHistoryTracker::buildWatchtimeBeaconUrl(std::string_view baseUrl, std::string_view cpn,
                                                         double positionSec, double durationSec, bool isFinished) {
    return buildWatchtimeBeaconUrl(baseUrl, cpn, 0.0, positionSec, durationSec, isFinished);
}

HttpResponse WatchHistoryTracker::postInnerTube(const std::string &url, const std::string &body,
                                                const std::vector<std::string> &headers, int timeoutSec) const {
    return innertube::post(url, body, headers, m_cookieManager.get(), timeoutSec);
}

HttpResponse WatchHistoryTracker::getInnerTube(const std::string &url, const std::vector<std::string> &headers,
                                               int timeoutSec) const {
    return innertube::get(url, headers, m_cookieManager.get(), timeoutSec);
}

void WatchHistoryTracker::enqueueTrackingTask(std::function<void()> task) {
    {
        std::unique_lock lock(m_trackingMutex);
        if (m_trackingStop) {
            return;
        }
        m_trackingTasks.push(std::move(task));
    }
    m_trackingCv.notify_one();
}

void WatchHistoryTracker::trackingWorkerLoop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock lock(m_trackingMutex);
            m_trackingCv.wait(lock, [this]() { return m_trackingStop || !m_trackingTasks.empty(); });
            if (m_trackingStop && m_trackingTasks.empty()) {
                break;
            }
            if (!m_trackingTasks.empty()) {
                task = std::move(m_trackingTasks.front());
                m_trackingTasks.pop();
            }
        }
        if (task) {
            try {
                task();
            } catch (const std::exception &e) {
                LOG_WARN("[WatchHistoryTracker] Tracking task error: {}", e.what());
            } catch (...) {
                LOG_WARN("[WatchHistoryTracker] Tracking task unknown error");
            }
        }
    }
}

void WatchHistoryTracker::recordWatchHistory(std::string_view videoId) {
    if (!m_cookieManager || !m_cookieManager->hasAuth() || videoId.empty()) {
        return;
    }

    enqueueTrackingTask([this, id = std::string(videoId)]() {
        LOG_INFO("[WatchHistoryTracker] Recording watch history for video {}...", id);
        std::string cpn = generatePlaybackNonce();

        nlohmann::json req;
        if (m_contextBuilder) {
            req["context"] = m_contextBuilder(false);
        }
        req["videoId"] = id;
        req["cpn"] = cpn;
        req["playbackContext"]["contentPlaybackContext"]["html5Preference"] = "HTML5_PREF_WANTS";

        std::string referer = std::format("https://www.youtube.com/watch?v={}", id);
        std::vector<std::string> headers;
        if (m_headerBuilder) {
            headers = m_headerBuilder(true, referer);
        }
        std::string playerUrl = innertube::makeApiUrl("player");

        HttpResponse res = postInnerTube(playerUrl, req.dump(), headers, 5);
        if (!res.isOk()) {
            LOG_WARN("[WatchHistoryTracker] Player request for history failed (HTTP {}): {}", res.statusCode,
                     res.error);
            return;
        }

        try {
            nlohmann::json parsed = nlohmann::json::parse(res.body);
            if (parsed.contains("playbackTracking") && parsed["playbackTracking"].is_object()) {
                const auto &pt = parsed["playbackTracking"];
                std::string playbackUrl;
                std::string watchtimeUrl;

                if (pt.contains("videostatsPlaybackUrl") && pt["videostatsPlaybackUrl"].contains("baseUrl") &&
                    pt["videostatsPlaybackUrl"]["baseUrl"].is_string()) {
                    playbackUrl = pt["videostatsPlaybackUrl"]["baseUrl"].get<std::string>();
                }

                if (pt.contains("videostatsWatchtimeUrl") && pt["videostatsWatchtimeUrl"].contains("baseUrl") &&
                    pt["videostatsWatchtimeUrl"]["baseUrl"].is_string()) {
                    watchtimeUrl = pt["videostatsWatchtimeUrl"]["baseUrl"].get<std::string>();
                }

                if (!playbackUrl.empty()) {
                    std::string fullPlaybackUrl = buildPlaybackBeaconUrl(playbackUrl, cpn);
                    LOG_DEBUG("[WatchHistoryTracker] Pinging playback tracking beacon: {}", fullPlaybackUrl);
                    HttpResponse statsRes = getInnerTube(fullPlaybackUrl, headers, 5);
                    LOG_INFO("[WatchHistoryTracker] Playback tracking beacon pinged (HTTP {})", statsRes.statusCode);
                }

                {
                    std::lock_guard lock(m_sessionMutex);
                    m_activeSession.videoId = id;
                    m_activeSession.cpn = cpn;
                    m_activeSession.playbackUrl = playbackUrl;
                    m_activeSession.watchtimeUrl = watchtimeUrl;
                    m_activeSession.startTime = std::chrono::steady_clock::now();
                    m_activeSession.lastReportTime = m_activeSession.startTime;
                    m_activeSession.lastReportedSec = 0.0;
                }
            }
        } catch (const std::exception &e) {
            LOG_WARN("[WatchHistoryTracker] Failed to parse player response for history tracking: {}", e.what());
        }
    });
}

void WatchHistoryTracker::recordWatchTime(std::string_view videoId, double startSec, double endSec, double durationSec,
                                          bool isFinished) {
    if (!m_cookieManager || !m_cookieManager->hasAuth() || videoId.empty()) {
        return;
    }

    enqueueTrackingTask([this, id = std::string(videoId), startSec, endSec, durationSec, isFinished]() {
        std::string watchtimeUrl;
        std::string cpn;
        {
            std::lock_guard lock(m_sessionMutex);
            if (m_activeSession.videoId != id || m_activeSession.watchtimeUrl.empty()) {
                return;
            }
            watchtimeUrl = m_activeSession.watchtimeUrl;
            cpn = m_activeSession.cpn;
        }

        std::string fullWatchtimeUrl =
            buildWatchtimeBeaconUrl(watchtimeUrl, cpn, startSec, endSec, durationSec, isFinished);
        std::string referer = std::format("https://www.youtube.com/watch?v={}", id);
        std::vector<std::string> headers;
        if (m_headerBuilder) {
            headers = m_headerBuilder(true, referer);
        }

        HttpResponse statsRes = getInnerTube(fullWatchtimeUrl, headers, 5);
        LOG_INFO("[WatchHistoryTracker] Watch time beacon pinged for {} (st={:.1f}s, et={:.1f}s, HTTP {})", id,
                 startSec, endSec, statsRes.statusCode);

        {
            std::lock_guard lock(m_sessionMutex);
            if (m_activeSession.videoId == id) {
                m_activeSession.lastReportedSec = endSec;
                m_activeSession.lastReportTime = std::chrono::steady_clock::now();
            }
        }
    });
}

void WatchHistoryTracker::recordWatchTime(std::string_view videoId, double positionSec, double durationSec,
                                          bool isFinished) {
    double start = 0.0;
    {
        std::lock_guard lock(m_sessionMutex);
        if (m_activeSession.videoId == videoId) {
            start = m_activeSession.lastReportedSec;
        }
    }
    recordWatchTime(videoId, start, positionSec, durationSec, isFinished);
}

}  // namespace lightyt
