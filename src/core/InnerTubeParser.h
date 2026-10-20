#pragma once

#include "VideoItem.h"

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace lightyt {

class InnerTubeParser {
public:
    // Extract video items from any InnerTube JSON response tree (pruned traversal)
    [[nodiscard]] static std::vector<VideoItem> extractVideos(const nlohmann::json &root);

    // Extract continuation token from any InnerTube JSON response tree
    [[nodiscard]] static std::string extractContinuationToken(const nlohmann::json &root);

    // Extract channels/identities from account_menu or accounts_list responses
    [[nodiscard]] static std::vector<AccountChannel> extractAccountChannels(const nlohmann::json &root);

    // Parse an individual video renderer / view model object
    [[nodiscard]] static bool parseVideoRenderer(const nlohmann::json &vr, VideoItem &outItem);

    // Parse classic videoRenderer
    [[nodiscard]] static bool parseClassicVideoRenderer(const nlohmann::json &vr, VideoItem &outItem);

    // Parse modern lockupViewModel / shortsLockupViewModel
    [[nodiscard]] static bool parseModernLockupViewModel(const nlohmann::json &vr, VideoItem &outItem);

    // Extract plain text string from runs, simpleText, or content object
    [[nodiscard]] static std::string extractText(const nlohmann::json &obj);

    // Parse like count from raw InnerTube string (e.g. "123K", "1.2M", "123,456 likes", "along with 1,234 other
    // people")
    [[nodiscard]] static int64_t parseLikeCountString(std::string_view text);

    // Parse InnerTube /next response tree for watch page metadata (avatar, full description, subscriber count, like
    // status)
    [[nodiscard]] static bool parseWatchNextMetadata(const nlohmann::json &root, WatchMetadata &outMeta,
                                                     bool preferWebp = false);

    // Extract highest appropriate resolution thumbnail URL
    [[nodiscard]] static std::string extractBestThumbnail(const nlohmann::json &thumbObj, std::string_view videoId,
                                                          bool preferWebp = true);

    // Extract available video qualities from InnerTube /player response (e.g. "Auto", "1080p", "720p", "Audio Only")
    [[nodiscard]] static std::vector<std::string> extractAvailableQualities(const nlohmann::json &root);

    // Channel identity matching and merging helpers
    [[nodiscard]] static bool channelsMatch(const AccountChannel &a, const AccountChannel &b) noexcept;
    static void mergeAccountChannel(AccountChannel &target, const AccountChannel &src);
    static void mergeOrAddChannel(std::vector<AccountChannel> &list, AccountChannel ch);
};

}  // namespace lightyt
