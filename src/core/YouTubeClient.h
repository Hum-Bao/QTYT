#pragma once

#include "CookieManager.h"
#include "HttpClient.h"
#include "VideoItem.h"

#include <functional>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace lightyt {
class WatchHistoryTracker;
}

enum class ContinuationKind : uint8_t { Browse, Search, WatchNext };

struct FeedResult {
    std::vector<VideoItem> videos;
    std::string continuationToken;
};

class YouTubeClient {
public:
    YouTubeClient();
    explicit YouTubeClient(const std::shared_ptr<CookieManager> &cookieManager);
    ~YouTubeClient();

    [[nodiscard]] std::shared_ptr<CookieManager> getCookieManager() const noexcept { return m_cookieManager; }

    // Fetch the home feed (personalized recommendations if cookies are loaded, otherwise popular/trending)
    [[nodiscard]] std::vector<VideoItem> fetchHomeFeed(std::string &errorMessage);

    // Fetch the subscriptions feed (videos from channels the account is subscribed to)
    [[nodiscard]] std::vector<VideoItem> fetchSubscriptionsFeed(std::string &errorMessage);

    // Generic browse feed fetch by InnerTube browseId
    [[nodiscard]] std::vector<VideoItem> fetchFeed(std::string_view browseId, std::string &errorMessage);

    // Feed fetch with continuation token support (for infinite scrolling)
    [[nodiscard]] FeedResult fetchFeedWithContinuation(std::string_view browseId, std::string &errorMessage);

    // Fetch next batch of videos using continuation token
    [[nodiscard]] FeedResult fetchContinuation(std::string_view continuationToken, std::string &errorMessage,
                                               ContinuationKind kind = ContinuationKind::Browse);

    [[nodiscard]] FeedResult fetchContinuation(std::string_view continuationToken, ContinuationKind kind,
                                               std::string &errorMessage) {
        return fetchContinuation(continuationToken, errorMessage, kind);
    }

    // Search YouTube for query (returns first page)
    [[nodiscard]] std::vector<VideoItem> search(std::string_view query, std::string &errorMessage);

    // Search with continuation token support
    [[nodiscard]] FeedResult searchWithContinuation(std::string_view query, std::string &errorMessage);

    // Fetch watch page metadata (channel avatar, subscriber count, full description, views, date)
    [[nodiscard]] WatchMetadata fetchWatchMetadata(std::string_view videoId,
                                                   const std::atomic<bool> *cancelToken = nullptr) const;
    [[nodiscard]] static WatchMetadata fetchWatchMetadataDirect(std::string_view videoId,
                                                                const std::atomic<bool> *cancelToken = nullptr);

    // Fetch available video qualities for a video via InnerTube /player
    [[nodiscard]] std::vector<std::string>
    fetchAvailableQualities(std::string_view videoId, const std::atomic<bool> *cancelToken = nullptr) const;
    [[nodiscard]] static std::vector<std::string>
    fetchAvailableQualitiesDirect(std::string_view videoId, const std::atomic<bool> *cancelToken = nullptr);

    // Fetch channels/profiles associated with the authenticated account (cached unless forceRefresh is true)
    [[nodiscard]] std::vector<AccountChannel> fetchAccountChannels(std::string &errorMessage, bool forceRefresh = false,
                                                                   const std::atomic<bool> *cancelToken = nullptr);
    [[nodiscard]] std::vector<AccountChannel> getCachedAccountChannels() const;
    void clearCachedAccountChannels();

    // Set selected channel / account parameters for subsequent requests
    void setSelectedChannel(std::string_view pageId, int authUserIndex = -1, std::string_view channelName = "");
    [[nodiscard]] std::string getSelectedChannelPageId() const;
    [[nodiscard]] int getSelectedAuthUserIndex() const;
    [[nodiscard]] std::string getSelectedChannelName() const;

    // Helper to extract videos from any InnerTube JSON tree
    [[nodiscard]] static std::vector<VideoItem> extractVideosFromJson(const nlohmann::json &root);

    // Helper to extract channels from InnerTube account responses
    [[nodiscard]] static std::vector<AccountChannel> extractChannelsFromJson(const nlohmann::json &root);

    // Helper to extract continuation token from InnerTube JSON tree
    [[nodiscard]] static std::string extractContinuationToken(const nlohmann::json &root);

    // Helper to parse an individual videoRenderer JSON object
    [[nodiscard]] static bool parseVideoRenderer(const nlohmann::json &vr, VideoItem &outItem);

    // Send asynchronous InnerTube player beacon to register video in account Watch History
    void recordWatchHistory(std::string_view videoId);

    // Send asynchronous InnerTube watch time beacon to report watch progress
    void recordWatchTime(std::string_view videoId, double startSec, double endSec, double durationSec,
                         bool isFinished = false);
    void recordWatchTime(std::string_view videoId, double positionSec, double durationSec, bool isFinished = false);

    // Like or dislike a video via InnerTube endpoint
    [[nodiscard]] bool likeVideo(std::string_view videoId, bool like, std::string &errorMessage,
                                 const std::atomic<bool> *cancelToken = nullptr);
    [[nodiscard]] bool removeLike(std::string_view videoId, std::string &errorMessage,
                                  const std::atomic<bool> *cancelToken = nullptr);

    // Subscribe or unsubscribe to a channel
    [[nodiscard]] bool subscribeChannel(std::string_view channelId, bool subscribe, std::string &errorMessage,
                                        const std::atomic<bool> *cancelToken = nullptr);

    // Helpers to build headers and base context
    [[nodiscard]] std::vector<std::string> buildHeaders(bool authenticated,
                                                        std::string_view referer = "https://www.youtube.com/",
                                                        bool rootAccountOnly = false) const;
    [[nodiscard]] nlohmann::json buildBaseContext(bool anonymous = false, bool rootAccountOnly = false) const;

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
    std::unique_ptr<lightyt::WatchHistoryTracker> m_watchHistoryTracker;

    mutable std::mutex m_channelMutex;
    std::string m_selectedChannelPageId;
    int m_selectedAuthUserIndex = -1;
    std::string m_selectedChannelName;
    std::vector<AccountChannel> m_cachedAccountChannels;

    [[nodiscard]] HttpResponse postInnerTube(const std::string &url, const std::string &body,
                                             const std::vector<std::string> &headers, int timeoutSec = 10,
                                             const std::atomic<bool> *cancelToken = nullptr) const;
    [[nodiscard]] HttpResponse getInnerTube(const std::string &url, const std::vector<std::string> &headers,
                                            int timeoutSec = 10, const std::atomic<bool> *cancelToken = nullptr) const;
};
