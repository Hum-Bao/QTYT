#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct VideoItem {
    std::string id;
    std::string title;
    std::string author;
    std::string authorId;
    std::string channelId;
    std::string duration;
    std::string viewCount;
    std::string publishedTime;
    std::string thumbnailUrl;
    std::string channelAvatarUrl;
    std::string description;
    bool isShort = false;

    [[nodiscard]] std::string getWatchUrl() const { return "https://www.youtube.com/watch?v=" + id; }
};

struct AccountChannel {
    std::string name;
    std::string handle;
    std::string channelId;
    std::string pageId;
    std::string photoUrl;
    int authUserIndex = -1;  // -1 = unset (don't force X-Goog-AuthUser), 0, 1, 2... = explicit account index
    bool isSelected = false;
};

struct WatchMetadata {
    std::string videoId;
    std::string title;
    std::string description;
    std::string author;
    std::string channelId;
    std::string channelAvatarUrl;
    std::string subscriberCount;
    std::string viewCount;
    std::string publishedDate;
    int64_t likeCount = 0;
    bool isLiked = false;
    bool isDisliked = false;
    bool isSubscribed = false;

    // R1 Extensions
    std::string autoplayVideoId;
    std::vector<VideoItem> relatedVideos;
    std::string continuationToken;
};
