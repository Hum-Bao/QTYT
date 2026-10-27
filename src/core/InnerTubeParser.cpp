#include "InnerTubeParser.h"

#include "utils/StringUtils.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_set>

namespace lightyt {

namespace {

using utils::equalsIgnoreCase;
using utils::trim;

bool isIgnoredSubtree(std::string_view key) noexcept {
    return key == "trackingParams" || key == "responseContext" || key == "frameworkUpdates" ||
           key == "serviceTrackingParams" || key == "topbar" || key == "microformat" || key == "adSlotRenderer" ||
           key == "adPlacementRenderer";
}

bool isIgnoredWatchSubtree(std::string_view key) noexcept {
    return key == "trackingParams" || key == "serviceTrackingParams" || key == "topbar" || key == "microformat" ||
           key == "adSlotRenderer" || key == "adPlacementRenderer";
}

inline const nlohmann::json *findObject(const nlohmann::json &node, std::string_view key) {
    if (!node.is_object()) {
        return nullptr;
    }
    auto it = node.find(key);
    if (it != node.end() && it->is_object()) {
        return &(*it);
    }
    return nullptr;
}

inline const nlohmann::json *findArray(const nlohmann::json &node, std::string_view key) {
    if (!node.is_object()) {
        return nullptr;
    }
    auto it = node.find(key);
    if (it != node.end() && it->is_array()) {
        return &(*it);
    }
    return nullptr;
}

inline std::string_view findString(const nlohmann::json &node, std::string_view key) {
    if (!node.is_object()) {
        return {};
    }
    auto it = node.find(key);
    if (it != node.end() && it->is_string()) {
        return it->get_ref<const std::string &>();
    }
    return {};
}

void recursivelyFindVideoRenderers(const nlohmann::json &node, std::vector<const nlohmann::json *> &outList,
                                   int depth = 0) {
    if (depth > 64) {
        return;
    }
    if (node.is_object()) {
        if (const auto *obj = findObject(node, "videoRenderer")) {
            outList.push_back(obj);
            return;
        }
        if (const auto *obj = findObject(node, "gridVideoRenderer")) {
            outList.push_back(obj);
            return;
        }
        if (const auto *obj = findObject(node, "compactVideoRenderer")) {
            outList.push_back(obj);
            return;
        }
        if (const auto *obj = findObject(node, "lockupViewModel")) {
            outList.push_back(obj);
            return;
        }
        if (const auto *obj = findObject(node, "reelItemRenderer")) {
            outList.push_back(obj);
            return;
        }
        if (const auto *obj = findObject(node, "shortsLockupViewModel")) {
            outList.push_back(obj);
            return;
        }
        if (const auto *obj = findObject(node, "playlistVideoRenderer")) {
            outList.push_back(obj);
            return;
        }

        for (const auto &[key, val] : node.items()) {
            if (!isIgnoredSubtree(key)) {
                recursivelyFindVideoRenderers(val, outList, depth + 1);
            }
        }
    } else if (node.is_array()) {
        for (const auto &item : node) {
            recursivelyFindVideoRenderers(item, outList, depth + 1);
        }
    }
}

void recursivelyFindContinuationToken(const nlohmann::json &node, std::string &outToken, int depth = 0) {
    if (depth > 64 || !outToken.empty()) {
        return;
    }
    if (node.is_object()) {
        if (const auto *cc = findObject(node, "continuationCommand")) {
            if (auto tok = findString(*cc, "token"); !tok.empty()) {
                outToken = std::string(tok);
                return;
            }
        }
        if (const auto *cir = findObject(node, "continuationItemRenderer")) {
            if (const auto *ce = findObject(*cir, "continuationEndpoint")) {
                if (const auto *cc = findObject(*ce, "continuationCommand")) {
                    if (auto tok = findString(*cc, "token"); !tok.empty()) {
                        outToken = std::string(tok);
                        return;
                    }
                }
            }
        }
        if (const auto *ncd = findObject(node, "nextContinuationData")) {
            if (auto tok = findString(*ncd, "continuation"); !tok.empty()) {
                outToken = std::string(tok);
                return;
            }
        }
        for (const auto &[key, val] : node.items()) {
            if (!isIgnoredSubtree(key)) {
                recursivelyFindContinuationToken(val, outToken, depth + 1);
                if (!outToken.empty()) {
                    return;
                }
            }
        }
    } else if (node.is_array()) {
        for (const auto &item : node) {
            recursivelyFindContinuationToken(item, outToken, depth + 1);
            if (!outToken.empty()) {
                return;
            }
        }
    }
}

const nlohmann::json *findTwoColumnWatchNext(const nlohmann::json &root) {
    if (!root.is_object()) {
        return nullptr;
    }
    if (const auto *tc = findObject(root, "twoColumnWatchNextResults")) {
        return tc;
    }
    if (const auto *cnt = findObject(root, "contents")) {
        if (const auto *tc = findObject(*cnt, "twoColumnWatchNextResults")) {
            return tc;
        }
    }
    if (const auto *resp = findObject(root, "response")) {
        if (const auto *cnt = findObject(*resp, "contents")) {
            if (const auto *tc = findObject(*cnt, "twoColumnWatchNextResults")) {
                return tc;
            }
        }
        if (const auto *tc = findObject(*resp, "twoColumnWatchNextResults")) {
            return tc;
        }
    }
    return nullptr;
}

std::string extractAutoplayVideoId(const nlohmann::json &apNode, bool allowDirectCompact = false) {
    std::function<std::string(const nlohmann::json &, int, bool)> searchAutoplay =
        [&](const nlohmann::json &node, int depth, bool insideAutoplayContext) -> std::string {
        if (depth > 24 || (!node.is_object() && !node.is_array())) {
            return {};
        }
        if (node.is_object()) {
            if (const auto *avr = findObject(node, "autoplayVideoRenderer")) {
                if (const auto *ne = findObject(*avr, "navigationEndpoint")) {
                    if (const auto *we = findObject(*ne, "watchEndpoint")) {
                        if (auto vid = findString(*we, "videoId"); !vid.empty()) {
                            return std::string(vid);
                        }
                    }
                }
                if (const auto *we = findObject(*avr, "watchEndpoint")) {
                    if (auto vid = findString(*we, "videoId"); !vid.empty()) {
                        return std::string(vid);
                    }
                }
                if (auto vid = findString(*avr, "videoId"); !vid.empty()) {
                    return std::string(vid);
                }
            }
            if (insideAutoplayContext) {
                if (const auto *cvr = findObject(node, "compactVideoRenderer")) {
                    if (auto vid = findString(*cvr, "videoId"); !vid.empty()) {
                        return std::string(vid);
                    }
                    if (const auto *ne = findObject(*cvr, "navigationEndpoint")) {
                        if (const auto *we = findObject(*ne, "watchEndpoint")) {
                            if (auto vid = findString(*we, "videoId"); !vid.empty()) {
                                return std::string(vid);
                            }
                        }
                    }
                }
            }
            if (const auto *nvr = findObject(node, "nextVideoRenderer")) {
                std::string res = searchAutoplay(*nvr, depth + 1, true);
                if (!res.empty()) {
                    return res;
                }
            }
            if (const auto *ap = findObject(node, "autoplay")) {
                std::string res = searchAutoplay(*ap, depth + 1, true);
                if (!res.empty()) {
                    return res;
                }
            }
            if (const auto *sets = findArray(node, "sets")) {
                for (const auto &s : *sets) {
                    std::string res = searchAutoplay(s, depth + 1, insideAutoplayContext);
                    if (!res.empty()) {
                        return res;
                    }
                }
            }
            for (const auto &[k, v] : node.items()) {
                if (!isIgnoredWatchSubtree(k)) {
                    bool nextContext = insideAutoplayContext || (k == "autoplay") || (k == "nextVideoRenderer");
                    std::string res = searchAutoplay(v, depth + 1, nextContext);
                    if (!res.empty()) {
                        return res;
                    }
                }
            }
        } else if (node.is_array()) {
            for (const auto &item : node) {
                std::string res = searchAutoplay(item, depth + 1, insideAutoplayContext);
                if (!res.empty()) {
                    return res;
                }
            }
        }
        return {};
    };
    return searchAutoplay(apNode, 0, allowDirectCompact);
}

void recursivelyFindAccountRenderers(const nlohmann::json &node,
                                     std::vector<std::pair<std::string, const nlohmann::json *>> &outList,
                                     int depth = 0) {
    if (depth > 64) {
        return;
    }
    if (node.is_object()) {
        if (const auto *obj = findObject(node, "activeAccountHeaderRenderer")) {
            outList.emplace_back("activeAccountHeaderRenderer", obj);
        }
        if (const auto *obj = findObject(node, "accountItemRenderer")) {
            outList.emplace_back("accountItemRenderer", obj);
        }
        if (const auto *obj = findObject(node, "accountItem")) {
            outList.emplace_back("accountItem", obj);
        }
        if (const auto *obj = findObject(node, "channelListItemRenderer")) {
            outList.emplace_back("channelListItemRenderer", obj);
        }

        for (const auto &[key, val] : node.items()) {
            if (!isIgnoredSubtree(key)) {
                recursivelyFindAccountRenderers(val, outList, depth + 1);
            }
        }
    } else if (node.is_array()) {
        for (const auto &item : node) {
            recursivelyFindAccountRenderers(item, outList, depth + 1);
        }
    }
}

AccountChannel parseAccountRenderer(std::string_view type, const nlohmann::json &obj) {
    AccountChannel ch;
    if (type == "activeAccountHeaderRenderer") {
        ch.isSelected = true;
    } else if (obj.contains("isSelected") && obj["isSelected"].is_boolean()) {
        ch.isSelected = obj["isSelected"].get<bool>();
    }

    if (obj.contains("title")) {
        ch.name = InnerTubeParser::extractText(obj["title"]);
    } else if (obj.contains("accountName")) {
        ch.name = InnerTubeParser::extractText(obj["accountName"]);
    } else if (obj.contains("name")) {
        ch.name = InnerTubeParser::extractText(obj["name"]);
    } else if (obj.contains("headerText")) {
        ch.name = InnerTubeParser::extractText(obj["headerText"]);
    }

    if (obj.contains("channelHandle")) {
        ch.handle = InnerTubeParser::extractText(obj["channelHandle"]);
    } else if (obj.contains("handle")) {
        ch.handle = InnerTubeParser::extractText(obj["handle"]);
    } else if (obj.contains("accountByline")) {
        ch.handle = InnerTubeParser::extractText(obj["accountByline"]);
    } else if (obj.contains("byline")) {
        ch.handle = InnerTubeParser::extractText(obj["byline"]);
    } else if (obj.contains("email")) {
        ch.handle = InnerTubeParser::extractText(obj["email"]);
    }

    if (obj.contains("channelId") && obj["channelId"].is_string()) {
        ch.channelId = obj["channelId"].get<std::string>();
    }

    if (obj.contains("accountPhoto")) {
        ch.photoUrl = InnerTubeParser::extractBestThumbnail(obj["accountPhoto"], "");
    } else if (obj.contains("thumbnail")) {
        ch.photoUrl = InnerTubeParser::extractBestThumbnail(obj["thumbnail"], "");
    } else if (obj.contains("avatar")) {
        ch.photoUrl = InnerTubeParser::extractBestThumbnail(obj["avatar"], "");
    }

    if (obj.contains("pageId") && obj["pageId"].is_string()) {
        ch.pageId = obj["pageId"].get<std::string>();
    }

    auto searchTokens = [&](const nlohmann::json &tokensArr) {
        if (!tokensArr.is_array())
            return;
        for (const auto &tok : tokensArr) {
            if (tok.contains("pageIdToken") && tok["pageIdToken"].contains("pageId") &&
                tok["pageIdToken"]["pageId"].is_string()) {
                ch.pageId = tok["pageIdToken"]["pageId"].get<std::string>();
            }
        }
    };

    if (obj.contains("serviceEndpoint") && obj["serviceEndpoint"].is_object()) {
        const auto &se = obj["serviceEndpoint"];
        if (se.contains("selectActiveIdentityEndpoint") && se["selectActiveIdentityEndpoint"].is_object()) {
            const auto &sie = se["selectActiveIdentityEndpoint"];
            if (sie.contains("pageId") && sie["pageId"].is_string()) {
                ch.pageId = sie["pageId"].get<std::string>();
            }
            if (sie.contains("supportedTokens")) {
                searchTokens(sie["supportedTokens"]);
            }
        }
        if (se.contains("signInEndpoint") && se["signInEndpoint"].is_object()) {
            const auto &sie = se["signInEndpoint"];
            if (sie.contains("directSignIn") && sie["directSignIn"].is_object()) {
                if (sie["directSignIn"].contains("accountIndex") &&
                    sie["directSignIn"]["accountIndex"].is_number_integer()) {
                    ch.authUserIndex = sie["directSignIn"]["accountIndex"].get<int>();
                }
            }
        }
    }

    if (ch.pageId.empty()) {
        std::function<void(const nlohmann::json &, int)> findPageId = [&](const nlohmann::json &n, int depth) {
            if (depth > 64 || !ch.pageId.empty())
                return;
            if (n.is_object()) {
                if (n.contains("pageId") && n["pageId"].is_string() && !n["pageId"].get<std::string>().empty()) {
                    ch.pageId = n["pageId"].get<std::string>();
                    return;
                }
                for (const auto &[k, v] : n.items()) {
                    if (!isIgnoredSubtree(k)) {
                        findPageId(v, depth + 1);
                    }
                }
            } else if (n.is_array()) {
                for (const auto &elem : n) {
                    findPageId(elem, depth + 1);
                }
            }
        };
        findPageId(obj, 0);
    }

    if (ch.channelId.empty()) {
        std::function<void(const nlohmann::json &, int)> findBrowseId = [&](const nlohmann::json &n, int depth) {
            if (depth > 64 || !ch.channelId.empty())
                return;
            if (n.is_object()) {
                if (n.contains("browseId") && n["browseId"].is_string()) {
                    std::string bId = n["browseId"].get<std::string>();
                    if (bId.starts_with("UC")) {
                        ch.channelId = std::move(bId);
                        return;
                    }
                }
                for (const auto &[k, v] : n.items()) {
                    if (!isIgnoredSubtree(k)) {
                        findBrowseId(v, depth + 1);
                    }
                }
            } else if (n.is_array()) {
                for (const auto &elem : n) {
                    findBrowseId(elem, depth + 1);
                }
            }
        };
        findBrowseId(obj, 0);
    }

    if (!ch.pageId.empty() && ch.authUserIndex < 0) {
        ch.authUserIndex = 0;
    }

    return ch;
}

}  // namespace

std::string InnerTubeParser::extractText(const nlohmann::json &obj) {
    if (obj.is_null()) {
        return {};
    }
    if (obj.is_string()) {
        return obj.get<std::string>();
    }
    if (auto s = findString(obj, "text"); !s.empty()) {
        return std::string(s);
    }
    if (auto s = findString(obj, "content"); !s.empty()) {
        return std::string(s);
    }
    if (auto s = findString(obj, "simpleText"); !s.empty()) {
        return std::string(s);
    }
    if (const auto *runs = findArray(obj, "runs"); runs && !runs->empty()) {
        std::string res;
        for (const auto &run : *runs) {
            if (run.is_string()) {
                res += run.get<std::string>();
            } else if (auto txt = findString(run, "text"); !txt.empty()) {
                res += txt;
            } else if (auto cnt = findString(run, "content"); !cnt.empty()) {
                res += cnt;
            }
        }
        return res;
    }
    return {};
}

std::string InnerTubeParser::extractBestThumbnail(const nlohmann::json &thumbObj, std::string_view videoId,
                                                  bool preferWebp) {
    const nlohmann::json *arrPtr = nullptr;
    if (thumbObj.is_null()) {
        return {};
    }
    if (const auto *t = findArray(thumbObj, "thumbnails"); t && !t->empty()) {
        arrPtr = t;
    } else if (const auto *s = findArray(thumbObj, "sources"); s && !s->empty()) {
        arrPtr = s;
    } else if (const auto *img = findObject(thumbObj, "image")) {
        if (const auto *imgSources = findArray(*img, "sources"); imgSources && !imgSources->empty()) {
            arrPtr = imgSources;
        }
    } else if (const auto *av = findObject(thumbObj, "avatar")) {
        return extractBestThumbnail(*av, videoId, preferWebp);
    } else if (const auto *avm = findObject(thumbObj, "avatarViewModel")) {
        return extractBestThumbnail(*avm, videoId, preferWebp);
    } else if (const auto *davm = findObject(thumbObj, "decoratedAvatarViewModel")) {
        return extractBestThumbnail(*davm, videoId, preferWebp);
    }

    if (arrPtr == nullptr && videoId.empty() && thumbObj.is_object()) {
        std::function<const nlohmann::json *(const nlohmann::json &, int)> findSources =
            [&](const nlohmann::json &node, int depth) -> const nlohmann::json * {
            if (depth > 64)
                return nullptr;
            if (node.is_object()) {
                if (node.contains("sources") && node["sources"].is_array() && !node["sources"].empty()) {
                    return &node["sources"];
                }
                if (node.contains("thumbnails") && node["thumbnails"].is_array() && !node["thumbnails"].empty()) {
                    return &node["thumbnails"];
                }
                for (const auto &[k, v] : node.items()) {
                    if (!isIgnoredSubtree(k)) {
                        const auto *res = findSources(v, depth + 1);
                        if (res != nullptr)
                            return res;
                    }
                }
            } else if (node.is_array()) {
                for (const auto &elem : node) {
                    const auto *res = findSources(elem, depth + 1);
                    if (res != nullptr)
                        return res;
                }
            }
            return nullptr;
        };
        arrPtr = findSources(thumbObj, 0);
    }

    if (arrPtr != nullptr) {
        const auto &arr = *arrPtr;
        std::string bestUrl;
        int bestWidthDiff = 99999;
        constexpr int TARGET_WIDTH = 720;

        for (const auto &t : arr) {
            if (t.contains("url") && t["url"].is_string()) {
                int w = t.value("width", 0);
                if (w > 0) {
                    int diff = std::abs(w - TARGET_WIDTH);
                    if (diff < bestWidthDiff) {
                        bestWidthDiff = diff;
                        bestUrl = t["url"].get<std::string>();
                    }
                } else if (bestUrl.empty()) {
                    bestUrl = t["url"].get<std::string>();
                }
            }
        }
        if (!bestUrl.empty()) {
            if (bestUrl.starts_with("//")) {
                bestUrl = "https:" + bestUrl;
            }
            if (!preferWebp) {
                if (!videoId.empty() &&
                    (bestUrl.find(".webp") != std::string::npos || bestUrl.find("vi_webp") != std::string::npos)) {
                    return "https://i.ytimg.com/vi/" + std::string(videoId) + "/hqdefault.jpg";
                }
                bestUrl = utils::normalizeAvatarUrl(bestUrl, false);
            }
            return bestUrl;
        }
    }

    if (!videoId.empty()) {
        return "https://i.ytimg.com/vi/" + std::string(videoId) + "/hqdefault.jpg";
    }
    return {};
}

bool InnerTubeParser::channelsMatch(const AccountChannel &a, const AccountChannel &b) noexcept {
    if (!a.pageId.empty() && !b.pageId.empty() && a.pageId == b.pageId) {
        return true;
    }
    if (!a.channelId.empty() && !b.channelId.empty() && a.channelId == b.channelId) {
        return true;
    }
    if (!a.handle.empty() && !b.handle.empty() && a.handle.front() == '@' && b.handle.front() == '@') {
        if (equalsIgnoreCase(a.handle, b.handle)) {
            return true;
        }
    }
    if (!a.name.empty() && !b.name.empty() && equalsIgnoreCase(a.name, b.name)) {
        if ((!a.pageId.empty() && b.pageId.empty()) || (a.pageId.empty() && !b.pageId.empty())) {
            return false;
        }
        if (!a.handle.empty() && !b.handle.empty() && a.handle.front() == '@' && b.handle.front() == '@' &&
            !equalsIgnoreCase(a.handle, b.handle)) {
            return false;
        }
        if (!a.channelId.empty() && !b.channelId.empty() && a.channelId != b.channelId) {
            return false;
        }
        if (!a.pageId.empty() && !b.pageId.empty() && a.pageId != b.pageId) {
            return false;
        }
        if ((!a.pageId.empty() && (b.handle == "No channel" ||
                                   (b.handle.find('@') != std::string_view::npos && !b.handle.starts_with('@')))) ||
            (!b.pageId.empty() && (a.handle == "No channel" ||
                                   (a.handle.find('@') != std::string_view::npos && !a.handle.starts_with('@'))))) {
            return false;
        }
        return true;
    }
    return false;
}

void InnerTubeParser::mergeAccountChannel(AccountChannel &target, const AccountChannel &src) {
    if (target.name.empty() && !src.name.empty()) {
        target.name = src.name;
    }
    if (target.channelId.empty() && !src.channelId.empty()) {
        target.channelId = src.channelId;
    }
    if (target.pageId.empty() && !src.pageId.empty()) {
        target.pageId = src.pageId;
    }
    if (target.photoUrl.empty() && !src.photoUrl.empty()) {
        target.photoUrl = src.photoUrl;
    }
    if (src.isSelected) {
        target.isSelected = true;
    }
    if (target.authUserIndex < 0 && src.authUserIndex >= 0) {
        target.authUserIndex = src.authUserIndex;
    }
    auto handleScore = [](std::string_view h) -> int {
        if (h.empty())
            return 0;
        if (h == "No channel")
            return 1;
        if (h.find('@') != std::string_view::npos && !h.starts_with('@'))
            return 2;
        if (h.starts_with('@'))
            return 3;
        return 2;
    };
    if (handleScore(src.handle) > handleScore(target.handle)) {
        target.handle = src.handle;
    }
    if (!target.pageId.empty() && target.authUserIndex < 0) {
        target.authUserIndex = 0;
    }
}

void InnerTubeParser::mergeOrAddChannel(std::vector<AccountChannel> &list, AccountChannel ch) {
    if (ch.name.empty() && ch.channelId.empty() && ch.pageId.empty()) {
        return;
    }
    auto it = std::ranges::find_if(list, [&](const auto &existing) { return channelsMatch(existing, ch); });
    if (it != list.end()) {
        mergeAccountChannel(*it, ch);
        return;
    }
    if (!ch.pageId.empty() && ch.authUserIndex < 0) {
        ch.authUserIndex = 0;
    }
    list.push_back(std::move(ch));
}

std::vector<AccountChannel> InnerTubeParser::extractAccountChannels(const nlohmann::json &root) {
    std::vector<std::pair<std::string, const nlohmann::json *>> renderers;
    recursivelyFindAccountRenderers(root, renderers);

    std::vector<AccountChannel> channels;
    for (const auto &[type, r] : renderers) {
        if (r == nullptr)
            continue;
        AccountChannel ch = parseAccountRenderer(type, *r);
        mergeOrAddChannel(channels, std::move(ch));
    }
    return channels;
}

std::string InnerTubeParser::extractContinuationToken(const nlohmann::json &root) {
    std::string token;
    recursivelyFindContinuationToken(root, token);
    return token;
}

std::vector<VideoItem> InnerTubeParser::extractVideos(const nlohmann::json &root) {
    std::vector<const nlohmann::json *> renderers;
    recursivelyFindVideoRenderers(root, renderers);

    std::vector<VideoItem> videos;
    std::unordered_set<std::string> seenIds;
    videos.reserve(renderers.size());

    for (const auto *vr : renderers) {
        if (vr == nullptr)
            continue;
        VideoItem item;
        if (parseVideoRenderer(*vr, item)) {
            if (item.duration == "Shorts" || item.id.find("/shorts/") != std::string::npos) {
                item.isShort = true;
            }
            if (!item.id.empty() && !seenIds.contains(item.id)) {
                seenIds.insert(item.id);
                videos.push_back(std::move(item));
            }
        }
    }
    return videos;
}

bool InnerTubeParser::parseClassicVideoRenderer(const nlohmann::json &vr, VideoItem &outItem) {
    if (!vr.contains("videoId") || !vr["videoId"].is_string()) {
        return false;
    }

    outItem.id = vr["videoId"].get<std::string>();

    if (vr.contains("title")) {
        outItem.title = extractText(vr["title"]);
    }
    if (vr.contains("headline")) {
        if (outItem.title.empty()) {
            outItem.title = extractText(vr["headline"]);
        }
        outItem.isShort = true;
        if (outItem.duration.empty()) {
            outItem.duration = "Shorts";
        }
    }

    if (vr.contains("ownerText")) {
        outItem.author = extractText(vr["ownerText"]);
    } else if (vr.contains("shortBylineText")) {
        outItem.author = extractText(vr["shortBylineText"]);
    } else if (vr.contains("longBylineText")) {
        outItem.author = extractText(vr["longBylineText"]);
    }

    if (vr.contains("lengthText")) {
        outItem.duration = extractText(vr["lengthText"]);
    }

    if (vr.contains("viewCountText")) {
        outItem.viewCount = extractText(vr["viewCountText"]);
    }
    if (outItem.viewCount.empty() && vr.contains("shortViewCountText")) {
        outItem.viewCount = extractText(vr["shortViewCountText"]);
    }

    if (vr.contains("publishedTimeText")) {
        outItem.publishedTime = extractText(vr["publishedTimeText"]);
    }

    if (vr.contains("descriptionSnippet")) {
        outItem.description = extractText(vr["descriptionSnippet"]);
    } else if (vr.contains("detailedMetadataSnippets") && vr["detailedMetadataSnippets"].is_array()) {
        for (const auto &snip : vr["detailedMetadataSnippets"]) {
            if (snip.contains("snippetText")) {
                outItem.description = extractText(snip["snippetText"]);
                if (!outItem.description.empty())
                    break;
            }
        }
    }

    if (vr.contains("channelThumbnailSupportedRenderers") && vr["channelThumbnailSupportedRenderers"].is_object()) {
        const auto &ctsr = vr["channelThumbnailSupportedRenderers"];
        if (ctsr.contains("channelThumbnailWithLinkRenderer") && ctsr["channelThumbnailWithLinkRenderer"].is_object()) {
            const auto &ctwlr = ctsr["channelThumbnailWithLinkRenderer"];
            if (ctwlr.contains("thumbnail")) {
                outItem.channelAvatarUrl = extractBestThumbnail(ctwlr["thumbnail"], "");
            }
        }
    }
    if (outItem.channelAvatarUrl.empty() && vr.contains("channelThumbnail")) {
        outItem.channelAvatarUrl = extractBestThumbnail(vr["channelThumbnail"], "");
    }

    if (vr.contains("thumbnail")) {
        outItem.thumbnailUrl = extractBestThumbnail(vr["thumbnail"], outItem.id);
    }
    if (outItem.thumbnailUrl.empty() && !outItem.id.empty()) {
        outItem.thumbnailUrl = "https://i.ytimg.com/vi/" + outItem.id + "/mqdefault.jpg";
    }

    if (outItem.duration == "Shorts" || outItem.id.find("/shorts/") != std::string::npos) {
        outItem.isShort = true;
    }

    return !outItem.id.empty();
}

bool InnerTubeParser::parseModernLockupViewModel(const nlohmann::json &vr, VideoItem &outItem) {
    if (vr.contains("contentType") && vr["contentType"].is_string()) {
        std::string ct = vr["contentType"].get<std::string>();
        if (ct == "LOCKUP_CONTENT_TYPE_PLAYLIST" || ct == "LOCKUP_CONTENT_TYPE_MIX" ||
            ct == "LOCKUP_CONTENT_TYPE_CHANNEL") {
            return false;
        }
    }

    auto checkTap = [](const nlohmann::json &node) -> std::string {
        if (node.is_object() && node.contains("onTap") && node["onTap"].is_object()) {
            const auto &ot = node["onTap"];
            if (ot.contains("innertubeCommand") && ot["innertubeCommand"].is_object()) {
                const auto &ic = ot["innertubeCommand"];
                if (ic.contains("watchEndpoint") && ic["watchEndpoint"].is_object()) {
                    return ic["watchEndpoint"].value("videoId", "");
                }
                if (ic.contains("reelWatchEndpoint") && ic["reelWatchEndpoint"].is_object()) {
                    return ic["reelWatchEndpoint"].value("videoId", "");
                }
            }
            if (ot.contains("watchEndpoint") && ot["watchEndpoint"].is_object()) {
                return ot["watchEndpoint"].value("videoId", "");
            }
        }
        return {};
    };

    // 1. Authoritative navigation endpoint
    outItem.id = checkTap(vr);
    if (outItem.id.empty() && vr.contains("rendererContext") && vr["rendererContext"].is_object()) {
        const auto &rc = vr["rendererContext"];
        if (rc.contains("commandContext") && rc["commandContext"].is_object()) {
            outItem.id = checkTap(rc["commandContext"]);
        }
    }

    // 2. Direct identifier fields
    if (outItem.id.empty()) {
        if (vr.contains("contentId") && vr["contentId"].is_string()) {
            outItem.id = vr["contentId"].get<std::string>();
        } else if (vr.contains("videoId") && vr["videoId"].is_string()) {
            outItem.id = vr["videoId"].get<std::string>();
        } else if (vr.contains("entityId") && vr["entityId"].is_string()) {
            outItem.id = vr["entityId"].get<std::string>();
        }
    }

    // 3. Fallback recursive search for videoId
    if (outItem.id.empty()) {
        std::function<void(const nlohmann::json &, int)> findId = [&](const nlohmann::json &node, int depth) {
            if (depth > 64 || !outItem.id.empty())
                return;
            if (node.is_object()) {
                if (node.contains("videoId") && node["videoId"].is_string()) {
                    outItem.id = node["videoId"].get<std::string>();
                    return;
                }
                for (const auto &[k, v] : node.items()) {
                    if (!isIgnoredSubtree(k)) {
                        findId(v, depth + 1);
                    }
                }
            } else if (node.is_array()) {
                for (const auto &item : node) {
                    findId(item, depth + 1);
                }
            }
        };
        findId(vr, 0);
    }

    // Clean any prefix
    outItem.id = std::string(utils::stripShortsPrefix(outItem.id));

    if (outItem.id.empty()) {
        return false;
    }

    bool isShort = false;
    if (vr.contains("contentType") && vr["contentType"] == "LOCKUP_CONTENT_TYPE_SHORTS") {
        isShort = true;
    }
    if (vr.contains("entityId") && vr["entityId"].is_string()) {
        std::string ent = vr["entityId"].get<std::string>();
        if (ent.find("shorts") != std::string::npos) {
            isShort = true;
        }
    }
    if (vr.contains("onTap") && vr["onTap"].contains("innertubeCommand") &&
        vr["onTap"]["innertubeCommand"].contains("reelWatchEndpoint")) {
        isShort = true;
    }

    // 1. Check overlayMetadata (standard in Shorts lockup models)
    if (vr.contains("overlayMetadata") && vr["overlayMetadata"].is_object()) {
        const auto &om = vr["overlayMetadata"];
        if (om.contains("primaryText")) {
            outItem.title = extractText(om["primaryText"]);
        }
        if (outItem.author.empty() && om.contains("secondaryText")) {
            outItem.author = extractText(om["secondaryText"]);
        }
    }

    // 2. Check headline
    if (outItem.title.empty() && vr.contains("headline")) {
        outItem.title = extractText(vr["headline"]);
    }

    // 3. Check lockupMetadataViewModel
    if (vr.contains("metadata") && vr["metadata"].is_object()) {
        const auto &meta = vr["metadata"];
        if (meta.contains("lockupMetadataViewModel") && meta["lockupMetadataViewModel"].is_object()) {
            const auto &lm = meta["lockupMetadataViewModel"];
            if (outItem.title.empty() && lm.contains("title")) {
                outItem.title = extractText(lm["title"]);
            }
            if (outItem.channelAvatarUrl.empty()) {
                if (lm.contains("image") && lm["image"].is_object()) {
                    outItem.channelAvatarUrl = extractBestThumbnail(lm["image"], "");
                } else if (lm.contains("avatar") && lm["avatar"].is_object()) {
                    outItem.channelAvatarUrl = extractBestThumbnail(lm["avatar"], "");
                }
            }
            if (outItem.description.empty() && lm.contains("descriptionSnippet")) {
                outItem.description = extractText(lm["descriptionSnippet"]);
            }
            if (lm.contains("metadata") && lm["metadata"].contains("contentMetadataViewModel")) {
                const auto &cm = lm["metadata"]["contentMetadataViewModel"];
                auto processParts = [&](const nlohmann::json &partsArr) {
                    for (const auto &part : partsArr) {
                        std::string t;
                        if (part.contains("text")) {
                            t = extractText(part["text"]);
                        } else {
                            t = extractText(part);
                        }
                        if (t.empty())
                            continue;
                        if (outItem.author.empty()) {
                            outItem.author = t;
                        } else if (outItem.viewCount.empty() &&
                                   (t.find("view") != std::string::npos || t.find("View") != std::string::npos)) {
                            outItem.viewCount = t;
                        } else if (outItem.publishedTime.empty()) {
                            outItem.publishedTime = t;
                        }
                    }
                };

                if (cm.contains("metadataRows") && cm["metadataRows"].is_array()) {
                    for (const auto &row : cm["metadataRows"]) {
                        if (row.contains("parts") && row["parts"].is_array()) {
                            processParts(row["parts"]);
                        }
                        if (row.contains("metadataParts") && row["metadataParts"].is_array()) {
                            processParts(row["metadataParts"]);
                        }
                    }
                }
            }
        }
    }

    if (outItem.channelAvatarUrl.empty()) {
        if (vr.contains("avatar")) {
            outItem.channelAvatarUrl = extractBestThumbnail(vr["avatar"], "");
        } else if (vr.contains("channelThumbnail")) {
            outItem.channelAvatarUrl = extractBestThumbnail(vr["channelThumbnail"], "");
        }
    }

    // 4. Check direct title
    if (outItem.title.empty() && vr.contains("title")) {
        outItem.title = extractText(vr["title"]);
    }

    // 5. Check accessibilityText fallback
    if (outItem.title.empty() && vr.contains("accessibilityText") && vr["accessibilityText"].is_string()) {
        std::string a11y = vr["accessibilityText"].get<std::string>();
        auto dashPos = a11y.find(" - ");
        if (dashPos != std::string::npos) {
            outItem.title = a11y.substr(0, dashPos);
        } else {
            outItem.title = std::move(a11y);
        }
    }

    // Duration extraction from badges / overlays
    if (outItem.duration.empty() && vr.contains("contentImage") && vr["contentImage"].is_object()) {
        const auto &ci = vr["contentImage"];
        const nlohmann::json *tvm = nullptr;
        if (ci.contains("thumbnailViewModel") && ci["thumbnailViewModel"].is_object()) {
            tvm = &ci["thumbnailViewModel"];
        } else if (ci.contains("collectionThumbnailViewModel") &&
                   ci["collectionThumbnailViewModel"].contains("primaryThumbnail") &&
                   ci["collectionThumbnailViewModel"]["primaryThumbnail"].contains("thumbnailViewModel")) {
            tvm = &ci["collectionThumbnailViewModel"]["primaryThumbnail"]["thumbnailViewModel"];
        }
        if (tvm != nullptr && tvm->contains("overlays") && (*tvm)["overlays"].is_array()) {
            for (const auto &overlay : (*tvm)["overlays"]) {
                if (overlay.contains("thumbnailOverlayTimeStatusRenderer")) {
                    outItem.duration = extractText(overlay["thumbnailOverlayTimeStatusRenderer"]["text"]);
                    break;
                }
                if (overlay.contains("thumbnailBadgeViewModel")) {
                    outItem.duration = extractText(overlay["thumbnailBadgeViewModel"]["text"]);
                    break;
                }
            }
        }
    }

    if (isShort) {
        outItem.isShort = true;
        if (outItem.duration.empty()) {
            outItem.duration = "Shorts";
        }
    }

    if (outItem.title.empty()) {
        outItem.title = (isShort ? "Short " : "Video ") + outItem.id;
    }

    if (vr.contains("contentImage") && vr["contentImage"].is_object()) {
        const auto &ci = vr["contentImage"];
        const nlohmann::json *tv = nullptr;
        if (ci.contains("collectionThumbnailViewModel")) {
            const auto &ct = ci["collectionThumbnailViewModel"];
            if (ct.contains("primaryThumbnail") && ct["primaryThumbnail"].contains("thumbnailViewModel")) {
                tv = &ct["primaryThumbnail"]["thumbnailViewModel"];
            }
        } else if (ci.contains("thumbnailViewModel") && ci["thumbnailViewModel"].is_object()) {
            tv = &ci["thumbnailViewModel"];
        }

        if (tv != nullptr) {
            outItem.thumbnailUrl = extractBestThumbnail(*tv, outItem.id);
        }
    }
    if (outItem.thumbnailUrl.empty() && !outItem.id.empty()) {
        outItem.thumbnailUrl = "https://i.ytimg.com/vi/" + outItem.id + "/mqdefault.jpg";
    }

    if (outItem.duration == "Shorts" || outItem.id.find("/shorts/") != std::string::npos) {
        outItem.isShort = true;
    }

    return !outItem.id.empty();
}

bool InnerTubeParser::parseVideoRenderer(const nlohmann::json &vr, VideoItem &outItem) {
    if (parseClassicVideoRenderer(vr, outItem) || parseModernLockupViewModel(vr, outItem)) {
        if (outItem.duration == "Shorts" || outItem.id.find("/shorts/") != std::string::npos) {
            outItem.isShort = true;
        }
        return true;
    }
    return false;
}

int64_t InnerTubeParser::parseLikeCountString(std::string_view text) {
    if (text.empty()) {
        return 0;
    }

    std::string lowerText;
    lowerText.reserve(text.size());
    for (char c : text) {
        lowerText.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    bool hasAlongWithOther = false;
    if ((lowerText.find("along with") != std::string::npos || lowerText.find("you and") != std::string::npos) &&
        (lowerText.find("other") != std::string::npos)) {
        hasAlongWithOther = true;
    } else if (lowerText.find("other people") != std::string::npos || lowerText.find("others") != std::string::npos) {
        hasAlongWithOther = true;
    }

    // Find the first sequence of digits
    size_t start = 0;
    while (start < text.size() && !std::isdigit(static_cast<unsigned char>(text[start]))) {
        start++;
    }
    if (start >= text.size()) {
        return 0;
    }

    // Extract numeric portion (digits, commas, dots)
    size_t end = start;
    while (end < text.size()) {
        char c = text[end];
        if (std::isdigit(static_cast<unsigned char>(c)) || c == ',' || c == '.') {
            end++;
        } else {
            break;
        }
    }

    std::string_view numPart = text.substr(start, end - start);

    // Look for suffix after numeric part (e.g., K, M, B, or words thousand, million, billion)
    size_t suffixStart = end;
    while (suffixStart < text.size() && (text[suffixStart] == ' ' || text[suffixStart] == '\t')) {
        suffixStart++;
    }

    double multiplier = 1.0;
    if (suffixStart < text.size()) {
        char sc = static_cast<char>(std::tolower(static_cast<unsigned char>(text[suffixStart])));
        std::string_view rest = text.substr(suffixStart);
        if (sc == 'b') {
            multiplier = 1000000000.0;
        } else if (sc == 'm') {
            multiplier = 1000000.0;
        } else if (sc == 'k') {
            multiplier = 1000.0;
        } else {
            std::string restLower;
            restLower.reserve(rest.size());
            for (char rc : rest) {
                restLower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(rc))));
            }
            if (restLower.starts_with("billion")) {
                multiplier = 1000000000.0;
            } else if (restLower.starts_with("million")) {
                multiplier = 1000000.0;
            } else if (restLower.starts_with("thousand")) {
                multiplier = 1000.0;
            }
        }
    }

    double val = 0.0;
    if (multiplier > 1.0) {
        // Multiplier present: can have decimal dot or decimal comma (e.g. 1.2M, 1,2M, 123K)
        std::string cleaned;
        cleaned.reserve(numPart.size());
        bool hasDot = (numPart.find('.') != std::string_view::npos);
        for (size_t i = 0; i < numPart.size(); ++i) {
            char c = numPart[i];
            if (c == ',') {
                if (!hasDot && (i + 1 < numPart.size()) && (numPart.size() - i - 1 <= 2)) {
                    // Decimal comma (e.g. 1,5M)
                    cleaned.push_back('.');
                }
            } else {
                cleaned.push_back(c);
            }
        }
        try {
            val = std::stod(cleaned);
        } catch (...) {
            val = 0.0;
        }
    } else {
        // Multiplier is 1.0: commas are thousands separators (e.g. 123,456)
        std::string cleaned;
        cleaned.reserve(numPart.size());
        for (char c : numPart) {
            if (c != ',') {
                cleaned.push_back(c);
            }
        }
        try {
            val = std::stod(cleaned);
        } catch (...) {
            val = 0.0;
        }
    }

    auto count = static_cast<int64_t>(std::round(val * multiplier));
    if (hasAlongWithOther) {
        count += 1;
    }
    return std::max<int64_t>(0, count);
}

bool InnerTubeParser::parseWatchNextMetadata(const nlohmann::json &root, WatchMetadata &outMeta, bool preferWebp) {
    if (!root.is_object() && !root.is_array())
        return false;

    const nlohmann::json *twoColumn = findTwoColumnWatchNext(root);

    // 1. Identify active video ID
    if (outMeta.videoId.empty() && root.is_object()) {
        if (const auto *cve = findObject(root, "currentVideoEndpoint")) {
            if (const auto *we = findObject(*cve, "watchEndpoint")) {
                if (auto vid = findString(*we, "videoId"); !vid.empty()) {
                    outMeta.videoId = std::string(vid);
                }
            }
        }
        if (outMeta.videoId.empty()) {
            if (auto vid = findString(root, "videoId"); !vid.empty()) {
                outMeta.videoId = std::string(vid);
            }
        }
    }
    if (outMeta.videoId.empty() && twoColumn != nullptr) {
        if (const auto *cve = findObject(*twoColumn, "currentVideoEndpoint")) {
            if (const auto *we = findObject(*cve, "watchEndpoint")) {
                if (auto vid = findString(*we, "videoId"); !vid.empty()) {
                    outMeta.videoId = std::string(vid);
                }
            }
        }
    }

    std::function<void(const nlohmann::json &, int)> search = [&](const nlohmann::json &node, int depth) {
        if (depth > 64 || (!node.is_object() && !node.is_array()))
            return;

        if (node.is_object()) {
            if (node.contains("videoOwnerRenderer") && node["videoOwnerRenderer"].is_object()) {
                const auto &vor = node["videoOwnerRenderer"];
                if (outMeta.author.empty() && vor.contains("title")) {
                    outMeta.author = extractText(vor["title"]);
                }
                if (outMeta.channelAvatarUrl.empty() && vor.contains("thumbnail")) {
                    outMeta.channelAvatarUrl = extractBestThumbnail(vor["thumbnail"], "");
                }
                if (outMeta.subscriberCount.empty() && vor.contains("subscriberCountText")) {
                    outMeta.subscriberCount = extractText(vor["subscriberCountText"]);
                }
                if (vor.contains("subscribed") && vor["subscribed"].is_boolean()) {
                    outMeta.isSubscribed = vor["subscribed"].get<bool>();
                }
                if (outMeta.channelId.empty() && vor.contains("navigationEndpoint")) {
                    const auto &ep = vor["navigationEndpoint"];
                    if (ep.contains("browseEndpoint") && ep["browseEndpoint"].contains("browseId")) {
                        outMeta.channelId = ep["browseEndpoint"].value("browseId", "");
                    }
                }
            }

            if (node.contains("videoSecondaryInfoRenderer") && node["videoSecondaryInfoRenderer"].is_object()) {
                const auto &sec = node["videoSecondaryInfoRenderer"];
                if (outMeta.description.empty()) {
                    if (sec.contains("attributedDescription") && sec["attributedDescription"].contains("content")) {
                        outMeta.description = sec["attributedDescription"].value("content", "");
                    } else if (sec.contains("description")) {
                        outMeta.description = extractText(sec["description"]);
                    }
                }
                if (sec.contains("subscribeButton") && sec["subscribeButton"].is_object()) {
                    const auto &sb = sec["subscribeButton"];
                    if (sb.contains("subscribeButtonRenderer") &&
                        sb["subscribeButtonRenderer"].contains("subscribed")) {
                        outMeta.isSubscribed = sb["subscribeButtonRenderer"].value("subscribed", false);
                    }
                }
            }

            if (node.contains("videoPrimaryInfoRenderer") && node["videoPrimaryInfoRenderer"].is_object()) {
                const auto &prim = node["videoPrimaryInfoRenderer"];
                if (outMeta.title.empty() && prim.contains("title")) {
                    outMeta.title = extractText(prim["title"]);
                }
                if (outMeta.viewCount.empty() && prim.contains("viewCount")) {
                    const auto &vc = prim["viewCount"];
                    if (vc.is_object() && vc.contains("videoViewCountRenderer") &&
                        vc["videoViewCountRenderer"].is_object()) {
                        const auto &vvcr = vc["videoViewCountRenderer"];
                        if (vvcr.contains("viewCount")) {
                            outMeta.viewCount = extractText(vvcr["viewCount"]);
                        }
                        if (outMeta.viewCount.empty() && vvcr.contains("shortViewCount")) {
                            outMeta.viewCount = extractText(vvcr["shortViewCount"]);
                        }
                    } else {
                        outMeta.viewCount = extractText(vc);
                    }
                }
                if (outMeta.publishedDate.empty() && prim.contains("dateText")) {
                    outMeta.publishedDate = extractText(prim["dateText"]);
                }
            }

            // Subscription entity fallback (common in modern InnerTube entity mutations)
            if (node.contains("subscriptionStateEntity") && node["subscriptionStateEntity"].is_object()) {
                const auto &sse = node["subscriptionStateEntity"];
                if (sse.contains("subscribed") && sse["subscribed"].is_boolean()) {
                    outMeta.isSubscribed = sse["subscribed"].get<bool>();
                }
            }

            // 1. Direct likeStatus & likeStatusEntity
            if (node.contains("likeStatus") && node["likeStatus"].is_string()) {
                std::string s = node["likeStatus"].get<std::string>();
                if (s == "LIKE") {
                    outMeta.isLiked = true;
                    outMeta.isDisliked = false;
                } else if (s == "DISLIKE") {
                    outMeta.isDisliked = true;
                    outMeta.isLiked = false;
                }
            }
            if (node.contains("likeStatusEntity") && node["likeStatusEntity"].is_object()) {
                const auto &lse = node["likeStatusEntity"];
                if (lse.contains("likeStatus") && lse["likeStatus"].is_string()) {
                    std::string s = lse["likeStatus"].get<std::string>();
                    if (s == "LIKE") {
                        outMeta.isLiked = true;
                        outMeta.isDisliked = false;
                    } else if (s == "DISLIKE") {
                        outMeta.isDisliked = true;
                        outMeta.isLiked = false;
                    }
                }
            }
            if (node.contains("dislikeStatusEntity") && node["dislikeStatusEntity"].is_object()) {
                const auto &dse = node["dislikeStatusEntity"];
                if (dse.contains("likeStatus") && dse["likeStatus"].is_string()) {
                    std::string s = dse["likeStatus"].get<std::string>();
                    if (s == "DISLIKE") {
                        outMeta.isDisliked = true;
                        outMeta.isLiked = false;
                    }
                }
            }

            // 2. segmentedLikeDislikeButtonRenderer
            if (node.contains("segmentedLikeDislikeButtonRenderer") &&
                node["segmentedLikeDislikeButtonRenderer"].is_object()) {
                const auto &sldbr = node["segmentedLikeDislikeButtonRenderer"];
                if (sldbr.contains("likeButton") && sldbr["likeButton"].is_object()) {
                    const auto &lb = sldbr["likeButton"];
                    const nlohmann::json *tbr = nullptr;
                    if (lb.contains("toggleButtonRenderer") && lb["toggleButtonRenderer"].is_object()) {
                        tbr = &lb["toggleButtonRenderer"];
                    } else if (lb.contains("buttonRenderer") && lb["buttonRenderer"].is_object()) {
                        tbr = &lb["buttonRenderer"];
                    }
                    if (tbr != nullptr) {
                        if (tbr->contains("isToggled") && (*tbr)["isToggled"].is_boolean()) {
                            if ((*tbr)["isToggled"].get<bool>()) {
                                outMeta.isLiked = true;
                            }
                        }
                        if (tbr->contains("likeStatus") && (*tbr)["likeStatus"].is_string()) {
                            std::string s = (*tbr)["likeStatus"].get<std::string>();
                            if (s == "LIKE")
                                outMeta.isLiked = true;
                            else if (s == "DISLIKE")
                                outMeta.isDisliked = true;
                        }
                        if (outMeta.likeCount <= 0 && tbr->contains("defaultText")) {
                            const auto &dt = (*tbr)["defaultText"];
                            if (dt.contains("accessibility") && dt["accessibility"].contains("accessibilityData")) {
                                std::string lbl = dt["accessibility"]["accessibilityData"].value("label", "");
                                int64_t c = parseLikeCountString(lbl);
                                if (c > 0)
                                    outMeta.likeCount = c;
                            }
                            if (outMeta.likeCount <= 0) {
                                std::string txt = extractText(dt);
                                int64_t c = parseLikeCountString(txt);
                                if (c > 0)
                                    outMeta.likeCount = c;
                            }
                        }
                        if (outMeta.likeCount <= 0 && tbr->contains("accessibilityData")) {
                            std::string lbl = (*tbr)["accessibilityData"].value("label", "");
                            int64_t c = parseLikeCountString(lbl);
                            if (c > 0)
                                outMeta.likeCount = c;
                        }
                        if (outMeta.likeCount <= 0 && tbr->contains("accessibility")) {
                            std::string lbl = (*tbr)["accessibility"].value("label", "");
                            int64_t c = parseLikeCountString(lbl);
                            if (c > 0)
                                outMeta.likeCount = c;
                        }
                    }
                }
                if (sldbr.contains("dislikeButton") && sldbr["dislikeButton"].is_object()) {
                    const auto &db = sldbr["dislikeButton"];
                    if (db.contains("toggleButtonRenderer") && db["toggleButtonRenderer"].is_object()) {
                        const auto &tbr = db["toggleButtonRenderer"];
                        if (tbr.contains("isToggled") && tbr["isToggled"].is_boolean() &&
                            tbr["isToggled"].get<bool>()) {
                            outMeta.isDisliked = true;
                        }
                        if (tbr.contains("likeStatus") && tbr["likeStatus"].is_string() &&
                            tbr["likeStatus"].get<std::string>() == "DISLIKE") {
                            outMeta.isDisliked = true;
                        }
                    }
                }
            }

            // 3. segmentedLikeDislikeButtonViewModel
            if (node.contains("segmentedLikeDislikeButtonViewModel") &&
                node["segmentedLikeDislikeButtonViewModel"].is_object()) {
                const auto &sldvm = node["segmentedLikeDislikeButtonViewModel"];
                if (sldvm.contains("likeButtonViewModel") && sldvm["likeButtonViewModel"].is_object()) {
                    const auto &lbvm = sldvm["likeButtonViewModel"];
                    const nlohmann::json *targetLb = &lbvm;
                    if (lbvm.contains("likeButtonViewModel") && lbvm["likeButtonViewModel"].is_object()) {
                        targetLb = &lbvm["likeButtonViewModel"];
                    }
                    if (targetLb->contains("likeStatusEntity") &&
                        (*targetLb)["likeStatusEntity"].contains("likeStatus")) {
                        std::string s = (*targetLb)["likeStatusEntity"].value("likeStatus", "");
                        if (s == "LIKE")
                            outMeta.isLiked = true;
                        else if (s == "DISLIKE")
                            outMeta.isDisliked = true;
                    }
                    if (targetLb->contains("toggleButtonViewModel")) {
                        const auto &tbvm = (*targetLb)["toggleButtonViewModel"];
                        const nlohmann::json *innerTb = &tbvm;
                        if (tbvm.contains("toggleButtonViewModel")) {
                            innerTb = &tbvm["toggleButtonViewModel"];
                        }
                        if (innerTb->contains("isToggled") && (*innerTb)["isToggled"].is_boolean()) {
                            if ((*innerTb)["isToggled"].get<bool>()) {
                                outMeta.isLiked = true;
                            }
                        }
                        auto checkBvm = [&](const nlohmann::json &bvmContainer) {
                            const nlohmann::json *bvm = &bvmContainer;
                            if (bvmContainer.contains("buttonViewModel")) {
                                bvm = &bvmContainer["buttonViewModel"];
                            }
                            if (outMeta.likeCount <= 0 && bvm->contains("accessibilityText") &&
                                (*bvm)["accessibilityText"].is_string()) {
                                int64_t c = parseLikeCountString((*bvm)["accessibilityText"].get<std::string>());
                                if (c > 0)
                                    outMeta.likeCount = c;
                            }
                            if (outMeta.likeCount <= 0 && bvm->contains("title") && (*bvm)["title"].is_string()) {
                                int64_t c = parseLikeCountString((*bvm)["title"].get<std::string>());
                                if (c > 0)
                                    outMeta.likeCount = c;
                            }
                        };
                        if (innerTb->contains("defaultButtonViewModel")) {
                            checkBvm((*innerTb)["defaultButtonViewModel"]);
                        }
                        if (outMeta.likeCount <= 0 && innerTb->contains("toggledButtonViewModel")) {
                            checkBvm((*innerTb)["toggledButtonViewModel"]);
                        }
                    }
                }
                if (sldvm.contains("dislikeButtonViewModel") && sldvm["dislikeButtonViewModel"].is_object()) {
                    const auto &dbvm = sldvm["dislikeButtonViewModel"];
                    const nlohmann::json *targetDb = &dbvm;
                    if (dbvm.contains("dislikeButtonViewModel") && dbvm["dislikeButtonViewModel"].is_object()) {
                        targetDb = &dbvm["dislikeButtonViewModel"];
                    }
                    if (targetDb->contains("dislikeStatusEntity") &&
                        (*targetDb)["dislikeStatusEntity"].contains("likeStatus")) {
                        std::string s = (*targetDb)["dislikeStatusEntity"].value("likeStatus", "");
                        if (s == "DISLIKE")
                            outMeta.isDisliked = true;
                    }
                    if (targetDb->contains("toggleButtonViewModel")) {
                        const auto &tbvm = (*targetDb)["toggleButtonViewModel"];
                        const nlohmann::json *innerTb = &tbvm;
                        if (tbvm.contains("toggleButtonViewModel")) {
                            innerTb = &tbvm["toggleButtonViewModel"];
                        }
                        if (innerTb->contains("isToggled") && (*innerTb)["isToggled"].is_boolean() &&
                            (*innerTb)["isToggled"].get<bool>()) {
                            outMeta.isDisliked = true;
                        }
                    }
                }
            }

            // 4. likeButtonRenderer
            if (node.contains("likeButtonRenderer") && node["likeButtonRenderer"].is_object()) {
                const auto &lbr = node["likeButtonRenderer"];
                if (lbr.contains("likeStatus") && lbr["likeStatus"].is_string()) {
                    std::string s = lbr["likeStatus"].get<std::string>();
                    if (s == "LIKE")
                        outMeta.isLiked = true;
                    else if (s == "DISLIKE")
                        outMeta.isDisliked = true;
                }
                if (outMeta.likeCount <= 0 && lbr.contains("likeCount")) {
                    if (lbr["likeCount"].is_number_integer()) {
                        outMeta.likeCount = lbr["likeCount"].get<int64_t>();
                    } else if (lbr["likeCount"].is_string()) {
                        outMeta.likeCount = parseLikeCountString(lbr["likeCount"].get<std::string>());
                    }
                }
            }

            // 5. factoidRenderer
            if (node.contains("factoidRenderer") && node["factoidRenderer"].is_object()) {
                const auto &fr = node["factoidRenderer"];
                std::string a11y = fr.value("accessibilityText", "");
                if (a11y.find("like") != std::string::npos || a11y.find("Like") != std::string::npos) {
                    int64_t c = parseLikeCountString(a11y);
                    if (c > 0 && outMeta.likeCount <= 0) {
                        outMeta.likeCount = c;
                    }
                }
            }

            // 6. Direct likeCount / likeCountText
            if (outMeta.likeCount <= 0 && node.contains("likeCount")) {
                if (node["likeCount"].is_number_integer()) {
                    outMeta.likeCount = node["likeCount"].get<int64_t>();
                } else if (node["likeCount"].is_string()) {
                    outMeta.likeCount = parseLikeCountString(node["likeCount"].get<std::string>());
                }
            }
            if (outMeta.likeCount <= 0 && node.contains("likeCountText")) {
                outMeta.likeCount = parseLikeCountString(extractText(node["likeCountText"]));
            }

            // Fallback for avatar from any avatarViewModel or channelThumbnail renderer in the tree
            if (outMeta.channelAvatarUrl.empty()) {
                if (node.contains("channelThumbnailSupportedRenderers")) {
                    outMeta.channelAvatarUrl = extractBestThumbnail(node["channelThumbnailSupportedRenderers"], "");
                } else if (node.contains("decoratedAvatarViewModel") || node.contains("avatarViewModel")) {
                    outMeta.channelAvatarUrl = extractBestThumbnail(node, "");
                }
            }

            for (const auto &[k, v] : node.items()) {
                if (!isIgnoredWatchSubtree(k)) {
                    search(v, depth + 1);
                }
            }
        } else if (node.is_array()) {
            for (const auto &item : node) {
                search(item, depth + 1);
            }
        }
    };

    search(root, 0);

    if (outMeta.isLiked) {
        outMeta.isDisliked = false;
    }

    outMeta.channelAvatarUrl = utils::normalizeAvatarUrl(std::move(outMeta.channelAvatarUrl), preferWebp);

    // 2. Extract autoplayVideoId from twoColumnWatchNextResults.autoplay (or recursive fallback)
    if (twoColumn != nullptr) {
        if (const auto *ap = findObject(*twoColumn, "autoplay")) {
            outMeta.autoplayVideoId = extractAutoplayVideoId(*ap, true);
        }
    }
    if (outMeta.autoplayVideoId.empty() && root.is_object()) {
        if (const auto *ap = findObject(root, "autoplay")) {
            outMeta.autoplayVideoId = extractAutoplayVideoId(*ap, true);
        }
    }
    if (outMeta.autoplayVideoId.empty()) {
        outMeta.autoplayVideoId = extractAutoplayVideoId(root, false);
    }
    if (outMeta.autoplayVideoId == outMeta.videoId) {
        outMeta.autoplayVideoId.clear();
    }

    // 3. Extract relatedVideos from twoColumnWatchNextResults.secondaryResults (or root fallback), excluding active videoId
    const nlohmann::json *secondaryNode = nullptr;
    if (twoColumn != nullptr) {
        if (const auto *sec = findObject(*twoColumn, "secondaryResults")) {
            secondaryNode = sec;
        }
    }
    if (secondaryNode == nullptr && root.is_object()) {
        if (const auto *sec = findObject(root, "secondaryResults")) {
            secondaryNode = sec;
        }
    }

    const nlohmann::json &targetRecNode = (secondaryNode != nullptr) ? *secondaryNode : root;
    std::vector<const nlohmann::json *> recRenderers;
    recursivelyFindVideoRenderers(targetRecNode, recRenderers);

    std::unordered_set<std::string> seenIds;
    outMeta.relatedVideos.reserve(recRenderers.size());
    for (const auto *vr : recRenderers) {
        if (vr == nullptr) {
            continue;
        }
        VideoItem item;
        if (parseVideoRenderer(*vr, item)) {
            if (item.duration == "Shorts" || item.id.find("/shorts/") != std::string::npos) {
                item.isShort = true;
            }
            if (!item.id.empty() && item.id != outMeta.videoId && !seenIds.contains(item.id)) {
                seenIds.insert(item.id);
                outMeta.relatedVideos.push_back(std::move(item));
            }
        }
    }

    // 4. Extract continuationToken from secondaryResults first (to avoid comment stream continuation tokens)
    if (secondaryNode != nullptr) {
        recursivelyFindContinuationToken(*secondaryNode, outMeta.continuationToken);
    } else if (twoColumn == nullptr) {
        recursivelyFindContinuationToken(root, outMeta.continuationToken);
    }

    return !outMeta.channelAvatarUrl.empty() || !outMeta.description.empty() || !outMeta.title.empty() ||
           outMeta.likeCount > 0 || outMeta.isLiked || outMeta.isDisliked ||
           !outMeta.relatedVideos.empty() || !outMeta.autoplayVideoId.empty();
}

std::vector<std::string> InnerTubeParser::extractAvailableQualities(const nlohmann::json &root) {
    std::vector<std::string> qualities = {"Auto"};

    bool has1080 = false;
    bool has720 = false;
    bool has480 = false;
    bool has360 = false;

    auto inspectFormat = [&](const nlohmann::json &fmt) {
        if (!fmt.is_object())
            return;
        int height = 0;
        if (fmt.contains("height") && fmt["height"].is_number()) {
            height = fmt["height"].get<int>();
        } else if (fmt.contains("qualityLabel") && fmt["qualityLabel"].is_string()) {
            std::string ql = fmt["qualityLabel"].get<std::string>();
            if (ql.find("1080") != std::string::npos)
                height = 1080;
            else if (ql.find("720") != std::string::npos)
                height = 720;
            else if (ql.find("480") != std::string::npos)
                height = 480;
            else if (ql.find("360") != std::string::npos)
                height = 360;
        }

        if (height >= 1080) {
            has1080 = true;
        } else if (height >= 720) {
            has720 = true;
        } else if (height >= 480) {
            has480 = true;
        } else if (height >= 360) {
            has360 = true;
        }
    };

    if (const auto *sd = findObject(root, "streamingData")) {
        if (const auto *af = findArray(*sd, "adaptiveFormats")) {
            for (const auto &fmt : *af) {
                inspectFormat(fmt);
            }
        }
        if (const auto *f = findArray(*sd, "formats")) {
            for (const auto &fmt : *f) {
                inspectFormat(fmt);
            }
        }
    }

    if (has1080 || has720 || has480 || has360) {
        if (has1080)
            qualities.emplace_back("1080p");
        if (has720)
            qualities.emplace_back("720p");
        if (has480)
            qualities.emplace_back("480p");
        if (has360)
            qualities.emplace_back("360p");
    } else {
        qualities.emplace_back("1080p");
        qualities.emplace_back("720p");
        qualities.emplace_back("480p");
        qualities.emplace_back("360p");
    }

    qualities.emplace_back("Audio Only");
    return qualities;
}

}  // namespace lightyt
