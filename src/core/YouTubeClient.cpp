#include "YouTubeClient.h"

#include "InnerTubeNetwork.h"
#include "InnerTubeParser.h"
#include "WatchHistoryTracker.h"
#include "utils/Logger.h"

#include <chrono>
#include <ctime>
#include <format>
#include <mutex>

namespace {

const std::string &getCurrentClientVersion() {
    static std::string s_version;
    static std::chrono::steady_clock::time_point s_lastUpdate{};
    static std::mutex s_mutex;

    auto now = std::chrono::steady_clock::now();
    std::lock_guard lock(s_mutex);
    if (s_version.empty() || now - s_lastUpdate > std::chrono::hours(12)) {
        auto sysNow = std::chrono::system_clock::now();
        std::time_t tt = std::chrono::system_clock::to_time_t(sysNow);
        std::tm tm{};
#ifdef _WIN32
        gmtime_s(&tm, &tt);
#else
        gmtime_r(&tt, &tm);
#endif
        s_version = std::format("2.{:04d}{:02d}{:02d}.01.00", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
        s_lastUpdate = now;
    }
    return s_version;
}
}  // namespace

YouTubeClient::YouTubeClient() : YouTubeClient(std::make_shared<CookieManager>()) {}

YouTubeClient::YouTubeClient(const std::shared_ptr<CookieManager> &cookieManager)
    : m_cookieManager(cookieManager ? cookieManager : std::make_shared<CookieManager>()),
      m_watchHistoryTracker(std::make_unique<lightyt::WatchHistoryTracker>(
          m_cookieManager,
          [this](bool authenticated, std::string_view referer) { return buildHeaders(authenticated, referer); },
          [this](bool anonymous) { return buildBaseContext(anonymous); })) {}

YouTubeClient::~YouTubeClient() = default;

HttpResponse YouTubeClient::postInnerTube(const std::string &url, const std::string &body,
                                          const std::vector<std::string> &headers, int timeoutSec,
                                          const std::atomic<bool> *cancelToken) const {
    return lightyt::innertube::post(url, body, headers, m_cookieManager.get(), timeoutSec, cancelToken);
}

HttpResponse YouTubeClient::getInnerTube(const std::string &url, const std::vector<std::string> &headers,
                                         int timeoutSec, const std::atomic<bool> *cancelToken) const {
    return lightyt::innertube::get(url, headers, m_cookieManager.get(), timeoutSec, cancelToken);
}

void YouTubeClient::recordWatchHistory(std::string_view videoId) {
    if (m_watchHistoryTracker) {
        m_watchHistoryTracker->recordWatchHistory(videoId);
    }
}

void YouTubeClient::recordWatchTime(std::string_view videoId, double startSec, double endSec, double durationSec,
                                    bool isFinished) {
    if (m_watchHistoryTracker) {
        m_watchHistoryTracker->recordWatchTime(videoId, startSec, endSec, durationSec, isFinished);
    }
}

void YouTubeClient::recordWatchTime(std::string_view videoId, double positionSec, double durationSec, bool isFinished) {
    if (m_watchHistoryTracker) {
        m_watchHistoryTracker->recordWatchTime(videoId, positionSec, durationSec, isFinished);
    }
}

std::string YouTubeClient::generatePlaybackNonce() {
    return lightyt::WatchHistoryTracker::generatePlaybackNonce();
}

std::string YouTubeClient::buildPlaybackBeaconUrl(std::string_view baseUrl, std::string_view cpn) {
    return lightyt::WatchHistoryTracker::buildPlaybackBeaconUrl(baseUrl, cpn);
}

std::string YouTubeClient::buildWatchtimeBeaconUrl(std::string_view baseUrl, std::string_view cpn, double startSec,
                                                   double endSec, double durationSec, bool isFinished) {
    return lightyt::WatchHistoryTracker::buildWatchtimeBeaconUrl(baseUrl, cpn, startSec, endSec, durationSec,
                                                                 isFinished);
}

std::string YouTubeClient::buildWatchtimeBeaconUrl(std::string_view baseUrl, std::string_view cpn, double positionSec,
                                                   double durationSec, bool isFinished) {
    return lightyt::WatchHistoryTracker::buildWatchtimeBeaconUrl(baseUrl, cpn, positionSec, durationSec, isFinished);
}

void YouTubeClient::setSelectedChannel(std::string_view pageId, int authUserIndex, std::string_view channelName) {
    std::lock_guard lock(m_channelMutex);
    m_selectedChannelPageId = std::string(pageId);
    m_selectedAuthUserIndex = (!m_selectedChannelPageId.empty() && authUserIndex < 0) ? 0 : authUserIndex;
    m_selectedChannelName = std::string(channelName);
    LOG_INFO("[YouTubeClient] Active channel set to '{}' (pageId: '{}', authUserIndex: {})", m_selectedChannelName,
             m_selectedChannelPageId, m_selectedAuthUserIndex);
}

std::string YouTubeClient::getSelectedChannelPageId() const {
    std::lock_guard lock(m_channelMutex);
    return m_selectedChannelPageId;
}

int YouTubeClient::getSelectedAuthUserIndex() const {
    std::lock_guard lock(m_channelMutex);
    return m_selectedAuthUserIndex;
}

std::string YouTubeClient::getSelectedChannelName() const {
    std::lock_guard lock(m_channelMutex);
    return m_selectedChannelName;
}

std::vector<std::string> YouTubeClient::buildHeaders(bool authenticated, std::string_view referer,
                                                     bool rootAccountOnly) const {
    const std::string &clientVer = getCurrentClientVersion();
    std::vector<std::string> headers;
    headers.emplace_back("X-YouTube-Client-Name: 1");
    headers.emplace_back("X-YouTube-Client-Version: " + clientVer);
    headers.emplace_back("Origin: https://www.youtube.com");
    headers.emplace_back("X-Origin: https://www.youtube.com");
    headers.emplace_back(std::format("Referer: {}", referer));
    headers.emplace_back(std::format("User-Agent: {}", DEFAULT_USER_AGENT));
    headers.emplace_back(R"(Sec-Ch-Ua: "Google Chrome";v="131", "Chromium";v="131", "Not_A Brand";v="24")");
    headers.emplace_back("Sec-Ch-Ua-Mobile: ?0");
    headers.emplace_back(R"(Sec-Ch-Ua-Platform: "Windows")");

    if (authenticated) {
        if (!rootAccountOnly) {
            int authIdx = -1;
            std::string pageId;
            {
                std::lock_guard lock(m_channelMutex);
                authIdx = m_selectedAuthUserIndex;
                pageId = m_selectedChannelPageId;
            }
            if (authIdx < 0 && !pageId.empty()) {
                authIdx = 0;
            }
            if (authIdx >= 0) {
                headers.emplace_back("X-Goog-AuthUser: " + std::to_string(authIdx));
            }
            if (!pageId.empty()) {
                headers.emplace_back("X-Goog-PageId: " + pageId);
            }
        }
        if (m_cookieManager && m_cookieManager->hasAuth()) {
            std::string auth = m_cookieManager->getSapisidHash("https://www.youtube.com");
            if (!auth.empty()) {
                headers.push_back("Authorization: " + auth);
            }
            std::string cookies = m_cookieManager->getCookieHeaderValue("www.youtube.com");
            if (!cookies.empty()) {
                headers.push_back("Cookie: " + cookies);
            }
        }
    }
    return headers;
}

nlohmann::json YouTubeClient::buildBaseContext(bool anonymous, bool rootAccountOnly) const {
    nlohmann::json ctx;
    ctx["client"]["clientName"] = "WEB";
    ctx["client"]["clientVersion"] = getCurrentClientVersion();
    ctx["client"]["hl"] = "en";
    ctx["client"]["gl"] = "US";
    ctx["client"]["utcOffsetMinutes"] = 0;
    if (!anonymous && !rootAccountOnly) {
        std::lock_guard lock(m_channelMutex);
        int authIdx = m_selectedAuthUserIndex;
        if (authIdx < 0 && !m_selectedChannelPageId.empty()) {
            authIdx = 0;
        }
        if (authIdx >= 0) {
            ctx["user"]["authuser"] = authIdx;
        }
        if (!m_selectedChannelPageId.empty()) {
            ctx["client"]["onBehalfOfUser"] = m_selectedChannelPageId;
        }
    }
    return ctx;
}

std::vector<AccountChannel> YouTubeClient::extractChannelsFromJson(const nlohmann::json &root) {
    return lightyt::InnerTubeParser::extractAccountChannels(root);
}

std::vector<AccountChannel> YouTubeClient::getCachedAccountChannels() const {
    std::lock_guard lock(m_channelMutex);
    return m_cachedAccountChannels;
}

void YouTubeClient::clearCachedAccountChannels() {
    std::lock_guard lock(m_channelMutex);
    m_cachedAccountChannels.clear();
}

std::vector<AccountChannel> YouTubeClient::fetchAccountChannels(std::string &errorMessage, bool forceRefresh,
                                                                const std::atomic<bool> *cancelToken) {
    if (cancelToken && cancelToken->load()) {
        return {};
    }
    if (!forceRefresh) {
        std::lock_guard lock(m_channelMutex);
        if (!m_cachedAccountChannels.empty()) {
            return m_cachedAccountChannels;
        }
    }

    if (!m_cookieManager || !m_cookieManager->hasAuth()) {
        errorMessage = "Not authenticated. Please load your cookies.txt first.";
        LOG_WARN("[YouTubeClient] fetchAccountChannels: {}", errorMessage);
        return {};
    }

    std::vector<AccountChannel> allChannels;

    auto mergeChannels = [&](const std::vector<AccountChannel> &newChannels) {
        for (const auto &ch : newChannels) {
            lightyt::InnerTubeParser::mergeOrAddChannel(allChannels, ch);
        }
    };

    // 1. Query account_menu
    {
        nlohmann::json req;
        req["context"] = buildBaseContext(false, /*rootAccountOnly=*/true);
        std::vector<std::string> headers = buildHeaders(true, "https://www.youtube.com/", /*rootAccountOnly=*/true);
        std::string menuUrl = lightyt::innertube::makeApiUrl("account/account_menu");

        HttpResponse res = postInnerTube(menuUrl, req.dump(), headers, 10, cancelToken);
        LOG_DEBUG("[YouTubeClient] account_menu HTTP {} ({} bytes)", res.statusCode, res.body.size());
        if (res.isOk()) {
            try {
                nlohmann::json parsed = nlohmann::json::parse(res.body);
                mergeChannels(extractChannelsFromJson(parsed));
            } catch (const std::exception &e) {
                LOG_WARN("[YouTubeClient] Failed to parse account_menu JSON: {}", e.what());
            }
        }
    }

    if (cancelToken && cancelToken->load()) {
        return allChannels;
    }

    // 2. Query accounts_list
    {
        nlohmann::json req;
        req["context"] = buildBaseContext(false, /*rootAccountOnly=*/true);
        std::vector<std::string> headers = buildHeaders(true, "https://www.youtube.com/", /*rootAccountOnly=*/true);
        std::string listUrl = lightyt::innertube::makeApiUrl("account/accounts_list");

        HttpResponse res = postInnerTube(listUrl, req.dump(), headers, 10, cancelToken);
        LOG_DEBUG("[YouTubeClient] accounts_list HTTP {} ({} bytes)", res.statusCode, res.body.size());
        if (res.isOk()) {
            try {
                nlohmann::json parsed = nlohmann::json::parse(res.body);
                mergeChannels(extractChannelsFromJson(parsed));
            } catch (const std::exception &e) {
                LOG_WARN("[YouTubeClient] Failed to parse accounts_list JSON: {}", e.what());
            }
        }
    }

    if (allChannels.empty()) {
        errorMessage = "No channels could be automatically detected from the account API.";
        LOG_WARN("[YouTubeClient] {}", errorMessage);
    } else {
        {
            std::lock_guard lock(m_channelMutex);
            m_cachedAccountChannels = allChannels;
        }
        LOG_INFO("[YouTubeClient] Detected {} channels/profiles associated with account", allChannels.size());
        for (const auto &c : allChannels) {
            LOG_INFO("[YouTubeClient]   - Channel: '{}' ({}) [pageId: '{}', channelId: '{}', selected: {}]", c.name,
                     c.handle, c.pageId, c.channelId, c.isSelected);
        }
    }

    return allChannels;
}

std::string YouTubeClient::extractContinuationToken(const nlohmann::json &root) {
    return lightyt::InnerTubeParser::extractContinuationToken(root);
}

std::vector<VideoItem> YouTubeClient::fetchHomeFeed(std::string &errorMessage) {
    return fetchFeed("FEwhat_to_watch", errorMessage);
}

std::vector<VideoItem> YouTubeClient::fetchSubscriptionsFeed(std::string &errorMessage) {
    return fetchFeed("FEsubscriptions", errorMessage);
}

std::vector<VideoItem> YouTubeClient::fetchFeed(std::string_view browseId, std::string &errorMessage) {
    return fetchFeedWithContinuation(browseId, errorMessage).videos;
}

FeedResult YouTubeClient::fetchFeedWithContinuation(std::string_view browseId, std::string &errorMessage) {
    bool useAuth = m_cookieManager && m_cookieManager->hasAuth();
    nlohmann::json req;
    req["context"] = buildBaseContext();
    req["browseId"] = std::string(browseId);

    LOG_DEBUG("[YouTubeClient] Requesting feed (auth: {}, browseId: {})", useAuth, browseId);

    std::vector<std::string> headers = buildHeaders(useAuth);
    std::string browseUrl = lightyt::innertube::makeApiUrl("browse");
    HttpResponse res = postInnerTube(browseUrl, req.dump(), headers);

    if (!res.isOk() && useAuth) {
        LOG_INFO("[YouTubeClient] Authenticated feed request failed (HTTP {}). Checking fallback options...",
                 res.statusCode);
        // Step 1: If using a specific channel profile, retry with root account session
        bool hasChannelSelection = false;
        {
            std::lock_guard lock(m_channelMutex);
            hasChannelSelection = (!m_selectedChannelPageId.empty() || m_selectedAuthUserIndex >= 0);
        }
        if (hasChannelSelection) {
            LOG_INFO("[YouTubeClient] Retrying authenticated feed with root account session...");
            nlohmann::json baseReq;
            baseReq["context"] = buildBaseContext(false, /*rootAccountOnly=*/true);
            baseReq["browseId"] = std::string(browseId);
            std::vector<std::string> baseHeaders =
                buildHeaders(true, "https://www.youtube.com/", /*rootAccountOnly=*/true);
            res = postInnerTube(browseUrl, baseReq.dump(), baseHeaders);
        }
    }

    LOG_DEBUG("[YouTubeClient] HTTP Status: {}, Body bytes: {}", res.statusCode, res.body.size());

    if (!res.isOk()) {
        errorMessage = res.error.empty() ? ("HTTP error " + std::to_string(res.statusCode)) : res.error;
        LOG_WARN("[YouTubeClient] Failed response: {}", errorMessage);
        return {};
    }

    try {
        nlohmann::json parsed = nlohmann::json::parse(res.body);
        FeedResult result;
        result.videos = extractVideosFromJson(parsed);
        result.continuationToken = extractContinuationToken(parsed);

        if (result.videos.empty()) {
            if (parsed.contains("alerts") && parsed["alerts"].is_array()) {
                for (const auto &alert : parsed["alerts"]) {
                    if (alert.contains("alertRenderer")) {
                        const auto &ar = alert["alertRenderer"];
                        std::string alertText =
                            lightyt::InnerTubeParser::extractText(ar.value("text", nlohmann::json{}));
                        if (!alertText.empty()) {
                            LOG_WARN("[YouTubeClient] InnerTube alert: {}", alertText);
                        }
                    }
                }
            }
            if (parsed.contains("error") && parsed["error"].is_object()) {
                std::string errDetail = parsed["error"].value("message", "");
                if (!errDetail.empty()) {
                    LOG_WARN("[YouTubeClient] InnerTube error: {}", errDetail);
                }
            }
            if (useAuth && parsed.contains("responseContext") && parsed["responseContext"].is_object()) {
                const auto &rc = parsed["responseContext"];
                if (rc.contains("mainAppWebResponseContext") && rc["mainAppWebResponseContext"].is_object() &&
                    rc["mainAppWebResponseContext"].value("loggedOut", false)) {
                    errorMessage =
                        "Session cookies expired or logged out on YouTube. Please re-import fresh cookies.txt.";
                    LOG_WARN("[YouTubeClient] {}", errorMessage);
                }
            }
            if (false) {
                if (false) {
                }
            }
        }

        // Fallback pipeline: for authenticated accounts when watch history is paused/empty
        if (result.videos.empty() && browseId == "FEwhat_to_watch") {
            if (useAuth) {
                LOG_INFO("[YouTubeClient] FEwhat_to_watch returned 0 videos. Falling back to FEsubscriptions...");
                auto subResult = fetchFeedWithContinuation("FEsubscriptions", errorMessage);
                if (!subResult.videos.empty()) {
                    return subResult;
                }
            } else {
                LOG_INFO(
                    "[YouTubeClient] FEwhat_to_watch returned 0 videos in guest mode. Falling back to FEtrending...");
                auto trendingResult = fetchFeedWithContinuation("FEtrending", errorMessage);
                if (!trendingResult.videos.empty()) {
                    return trendingResult;
                }
            }
        }

        LOG_INFO("[YouTubeClient] Extracted {} videos (continuation: {})", result.videos.size(),
                 !result.continuationToken.empty());
        return result;
    } catch (const std::exception &e) {
        errorMessage = std::string("JSON parse error: ") + e.what();
        LOG_ERROR("[YouTubeClient] {}", errorMessage);
        return {};
    }
}

FeedResult YouTubeClient::fetchContinuation(std::string_view continuationToken, std::string &errorMessage,
                                            ContinuationKind kind) {
    if (continuationToken.empty()) {
        errorMessage = "Empty continuation token";
        return {};
    }

    bool useAuth = m_cookieManager && m_cookieManager->hasAuth();
    nlohmann::json req;
    req["context"] = buildBaseContext();
    req["continuation"] = std::string(continuationToken);

    std::string_view kindStr = "Browse";
    std::string endpointName = "browse";
    if (kind == ContinuationKind::Search) {
        kindStr = "Search";
        endpointName = "search";
    } else if (kind == ContinuationKind::WatchNext) {
        kindStr = "WatchNext";
        endpointName = "next";
    }

    LOG_DEBUG("[YouTubeClient] Fetching next batch with continuation token (kind: {})...", kindStr);

    std::vector<std::string> headers = buildHeaders(useAuth);
    std::string endpointUrl = lightyt::innertube::makeApiUrl(endpointName);
    HttpResponse res = postInnerTube(endpointUrl, req.dump(), headers);

    if (!res.isOk() && useAuth) {
        LOG_INFO("[YouTubeClient] Authenticated continuation request failed (HTTP {}). Retrying with clean context...",
                 res.statusCode);
        nlohmann::json retryReq;
        retryReq["context"] = buildBaseContext();
        if (retryReq["context"].contains("client")) {
            retryReq["context"]["client"].erase("onBehalfOfUser");
        }
        if (retryReq["context"].contains("user")) {
            retryReq["context"]["user"].erase("onBehalfOfUser");
        }
        retryReq["continuation"] = std::string(continuationToken);
        res = postInnerTube(endpointUrl, retryReq.dump(), headers);

        if (!res.isOk()) {
            LOG_INFO("[YouTubeClient] Retrying continuation anonymously (HTTP {})...", res.statusCode);
            std::vector<std::string> anonHeaders = buildHeaders(false);
            nlohmann::json anonReq;
            anonReq["context"] = buildBaseContext(true);
            anonReq["continuation"] = std::string(continuationToken);
            res = postInnerTube(endpointUrl, anonReq.dump(), anonHeaders);
        }
    }

    if (!res.isOk()) {
        errorMessage = res.error.empty() ? ("HTTP error " + std::to_string(res.statusCode)) : res.error;
        LOG_WARN("[YouTubeClient] Continuation request failed: {}", errorMessage);
        return {};
    }

    try {
        nlohmann::json parsed = nlohmann::json::parse(res.body);
        FeedResult result;
        result.videos = extractVideosFromJson(parsed);
        result.continuationToken = extractContinuationToken(parsed);
        LOG_INFO("[YouTubeClient] Extracted {} continuation videos", result.videos.size());
        return result;
    } catch (const std::exception &e) {
        errorMessage = std::string("JSON parse error: ") + e.what();
        LOG_ERROR("[YouTubeClient] {}", errorMessage);
        return {};
    }
}

std::vector<VideoItem> YouTubeClient::search(std::string_view query, std::string &errorMessage) {
    return searchWithContinuation(query, errorMessage).videos;
}

FeedResult YouTubeClient::searchWithContinuation(std::string_view query, std::string &errorMessage) {
    nlohmann::json req;
    req["context"] = buildBaseContext();
    req["query"] = std::string(query);

    bool useAuth = m_cookieManager && m_cookieManager->hasAuth();
    std::vector<std::string> headers = buildHeaders(useAuth);
    std::string searchUrl = lightyt::innertube::makeApiUrl("search");
    HttpResponse res = postInnerTube(searchUrl, req.dump(), headers);

    if (!res.isOk() && useAuth) {
        LOG_INFO("[YouTubeClient] Authenticated search request failed (HTTP {}). Retrying anonymously...",
                 res.statusCode);
        req["context"] = buildBaseContext(true);
        std::vector<std::string> anonHeaders = buildHeaders(false);
        res = postInnerTube(searchUrl, req.dump(), anonHeaders);
    }

    if (!res.isOk()) {
        errorMessage = res.error.empty() ? ("HTTP error " + std::to_string(res.statusCode)) : res.error;
        return {};
    }

    try {
        nlohmann::json parsed = nlohmann::json::parse(res.body);
        FeedResult result;
        result.videos = extractVideosFromJson(parsed);
        result.continuationToken = extractContinuationToken(parsed);
        return result;
    } catch (const std::exception &e) {
        errorMessage = std::string("JSON parse error: ") + e.what();
        return {};
    }
}

std::vector<VideoItem> YouTubeClient::extractVideosFromJson(const nlohmann::json &root) {
    return lightyt::InnerTubeParser::extractVideos(root);
}

bool YouTubeClient::parseVideoRenderer(const nlohmann::json &vr, VideoItem &outItem) {
    return lightyt::InnerTubeParser::parseVideoRenderer(vr, outItem);
}

WatchMetadata YouTubeClient::fetchWatchMetadata(std::string_view videoId, const std::atomic<bool> *cancelToken) const {
    WatchMetadata meta;
    if (videoId.empty())
        return meta;

    bool useAuth = m_cookieManager && m_cookieManager->hasAuth();
    nlohmann::json req;
    req["context"] = buildBaseContext(!useAuth);
    req["videoId"] = std::string(videoId);

    std::string ref = std::format("https://www.youtube.com/watch?v={}", videoId);
    std::vector<std::string> headers = buildHeaders(useAuth, ref);
    std::string url = lightyt::innertube::makeApiUrl("next");

    HttpResponse res = postInnerTube(url, req.dump(), headers, 6, cancelToken);
    if (!res.isOk() && useAuth) {
        req["context"] = buildBaseContext(true);
        std::vector<std::string> anonHeaders = buildHeaders(false, ref);
        res = postInnerTube(url, req.dump(), anonHeaders, 6, cancelToken);
    }

    if (res.isOk() && !res.body.empty()) {
        try {
            auto json = nlohmann::json::parse(res.body);
            meta.videoId = std::string(videoId);
            (void)lightyt::InnerTubeParser::parseWatchNextMetadata(json, meta);
        } catch (const std::exception &ex) {
            LOG_DEBUG("[YouTubeClient] Failed to parse /next JSON: {}", ex.what());
        }
    }
    return meta;
}

WatchMetadata YouTubeClient::fetchWatchMetadataDirect(std::string_view videoId, const std::atomic<bool> *cancelToken) {
    return YouTubeClient().fetchWatchMetadata(videoId, cancelToken);
}

std::vector<std::string> YouTubeClient::fetchAvailableQualities(std::string_view videoId,
                                                                const std::atomic<bool> *cancelToken) const {
    std::vector<std::string> defaultQualities = {"Auto", "1080p", "720p", "480p", "360p", "Audio Only"};
    if (videoId.empty())
        return defaultQualities;

    bool useAuth = m_cookieManager && m_cookieManager->hasAuth();
    nlohmann::json req;
    req["context"] = buildBaseContext(!useAuth);
    req["videoId"] = std::string(videoId);
    req["playbackContext"]["contentPlaybackContext"]["html5Preference"] = "HTML5_PREF_WANTS";

    std::string ref = std::format("https://www.youtube.com/watch?v={}", videoId);
    std::vector<std::string> headers = buildHeaders(useAuth, ref);
    std::string url = lightyt::innertube::makeApiUrl("player");

    HttpResponse res = postInnerTube(url, req.dump(), headers, 6, cancelToken);
    if (!res.isOk() && useAuth) {
        req["context"] = buildBaseContext(true);
        std::vector<std::string> anonHeaders = buildHeaders(false, ref);
        res = postInnerTube(url, req.dump(), anonHeaders, 6, cancelToken);
    }

    if (res.isOk() && !res.body.empty()) {
        try {
            auto json = nlohmann::json::parse(res.body);
            auto qualities = lightyt::InnerTubeParser::extractAvailableQualities(json);
            if (!qualities.empty()) {
                return qualities;
            }
        } catch (const std::exception &ex) {
            LOG_DEBUG("[YouTubeClient] Failed to parse /player JSON for qualities: {}", ex.what());
        }
    }
    return defaultQualities;
}

std::vector<std::string> YouTubeClient::fetchAvailableQualitiesDirect(std::string_view videoId,
                                                                      const std::atomic<bool> *cancelToken) {
    return YouTubeClient().fetchAvailableQualities(videoId, cancelToken);
}

bool YouTubeClient::likeVideo(std::string_view videoId, bool like, std::string &errorMessage,
                              const std::atomic<bool> *cancelToken) {
    if (videoId.empty()) {
        errorMessage = "Empty video ID";
        return false;
    }
    if (!m_cookieManager || !m_cookieManager->hasAuth()) {
        errorMessage = "Sign-in required to like/dislike videos";
        LOG_WARN("[YouTubeClient] Cannot {} video without authentication", (like ? "like" : "dislike"));
        return false;
    }

    nlohmann::json req;
    req["context"] = buildBaseContext();
    req["target"] = {{"videoId", std::string(videoId)}};

    std::string action = like ? "like" : "dislike";
    std::string endpointUrl = lightyt::innertube::makeApiUrl(std::format("like/{}", action));
    std::string watchUrl = std::format("https://www.youtube.com/watch?v={}", videoId);
    std::vector<std::string> headers = buildHeaders(true, watchUrl);

    HttpResponse res = postInnerTube(endpointUrl, req.dump(), headers, 10, cancelToken);
    if (!res.isOk()) {
        errorMessage = res.error.empty() ? ("HTTP error " + std::to_string(res.statusCode)) : res.error;
        LOG_WARN("[YouTubeClient] Failed to {} video {}: {}", action, videoId, errorMessage);
        return false;
    }

    LOG_INFO("[YouTubeClient] Successfully sent {} for video {}", action, videoId);
    return true;
}

bool YouTubeClient::removeLike(std::string_view videoId, std::string &errorMessage,
                               const std::atomic<bool> *cancelToken) {
    if (videoId.empty()) {
        errorMessage = "Empty video ID";
        return false;
    }
    if (!m_cookieManager || !m_cookieManager->hasAuth()) {
        errorMessage = "Sign-in required to update video rating";
        return false;
    }

    nlohmann::json req;
    req["context"] = buildBaseContext();
    req["target"] = {{"videoId", std::string(videoId)}};

    std::string endpointUrl = lightyt::innertube::makeApiUrl("like/removelike");
    std::string watchUrl = std::format("https://www.youtube.com/watch?v={}", videoId);
    std::vector<std::string> headers = buildHeaders(true, watchUrl);

    HttpResponse res = postInnerTube(endpointUrl, req.dump(), headers, 10, cancelToken);
    if (!res.isOk()) {
        errorMessage = res.error.empty() ? ("HTTP error " + std::to_string(res.statusCode)) : res.error;
        LOG_WARN("[YouTubeClient] Failed to remove like/dislike for video {}: {}", videoId, errorMessage);
        return false;
    }

    LOG_INFO("[YouTubeClient] Successfully removed like/dislike for video {}", videoId);
    return true;
}

bool YouTubeClient::subscribeChannel(std::string_view channelId, bool subscribe, std::string &errorMessage,
                                     const std::atomic<bool> *cancelToken) {
    if (channelId.empty()) {
        errorMessage = "Empty channel ID";
        return false;
    }
    if (!m_cookieManager || !m_cookieManager->hasAuth()) {
        errorMessage = "Sign-in required to subscribe";
        return false;
    }

    nlohmann::json req;
    req["context"] = buildBaseContext();
    req["channelIds"] = nlohmann::json::array({std::string(channelId)});

    std::string action = subscribe ? "subscribe" : "unsubscribe";
    std::string endpointUrl = lightyt::innertube::makeApiUrl(std::format("subscription/{}", action));
    std::vector<std::string> headers = buildHeaders(true, "https://www.youtube.com");

    HttpResponse res = postInnerTube(endpointUrl, req.dump(), headers, 10, cancelToken);
    if (!res.isOk()) {
        errorMessage = res.error.empty() ? ("HTTP error " + std::to_string(res.statusCode)) : res.error;
        LOG_WARN("[YouTubeClient] Failed to {} channel {}: {}", action, channelId, errorMessage);
        return false;
    }

    LOG_INFO("[YouTubeClient] Successfully {}d channel {}", action, channelId);
    return true;
}
