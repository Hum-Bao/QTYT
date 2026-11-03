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
    std::string commentsContinuationToken;
    std::string commentCountText;
};

struct CommentItem {
    std::string commentId;
    std::string author;
    std::string authorChannelId;
    std::string authorAvatarUrl;
    std::string publishedTime;
    std::string text;
    std::string likeCountText;
    int64_t likeCount = 0;
    bool isLiked = false;
    bool isDisliked = false;
    bool isPinned = false;
    bool isHearted = false;
    std::string heartTooltip;
    bool isAuthorChannelOwner = false;
    bool isReply = false;

    // Reply thread metadata
    int replyCount = 0;
    std::string replyCountText;
    std::string repliesContinuationToken;

    // Action tokens for /youtubei/v1/comment/perform_comment_action
    std::string likeActionToken;
    std::string unlikeActionToken;
    std::string dislikeActionToken;
    std::string undislikeActionToken;
};

struct CommentsResult {
    std::vector<CommentItem> comments;
    std::string nextContinuationToken;
    std::string commentCountText;
    std::string topCommentsToken;
    std::string newestCommentsToken;
};
