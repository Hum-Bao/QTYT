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
    bool isPlaylist = false;
    std::string playlistBrowseId;

    [[nodiscard]] std::string getWatchUrl() const { return "https://www.youtube.com/watch?v=" + id; }
};

struct PlaylistItem {
    std::string id;              // Raw playlist ID (e.g. "PL...", "WL", "LL")
    std::string browseId;        // InnerTube browseId (e.g. "VLPL...")
    std::string title;
    std::string author;
    std::string videoCountText;  // e.g. "24 videos"
    std::string updatedTime;     // e.g. "Updated today" / "Public"
    std::string thumbnailUrl;
    std::string description;
    std::string viewCountText;   // e.g. "296 views"

    [[nodiscard]] std::string getPlaylistUrl() const {
        return "https://www.youtube.com/playlist?list=" + id;
    }

    [[nodiscard]] VideoItem toVideoItem() const {
        VideoItem v;
        v.id = id;
        v.title = title;
        v.author = author;
        v.duration = videoCountText;
        v.publishedTime = updatedTime;
        v.thumbnailUrl = thumbnailUrl;
        v.description = description;
        v.viewCount = viewCountText;
        v.isPlaylist = true;
        v.playlistBrowseId = browseId.empty() ? ("VL" + id) : browseId;
        return v;
    }
};

struct PlaylistsResult {
    std::vector<PlaylistItem> playlists;
    std::string continuationToken;
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
