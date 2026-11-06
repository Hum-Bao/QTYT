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

void recursivelyFindPlaylistRenderers(const nlohmann::json &node,
                                      std::vector<const nlohmann::json *> &outList,
                                      int depth = 0) {
    if (depth > 64) {
        return;
    }
    if (node.is_object()) {
        if (const auto *obj = findObject(node, "playlistRenderer")) {
            outList.push_back(obj);
            return;
        }
        if (const auto *obj = findObject(node, "gridPlaylistRenderer")) {
            outList.push_back(obj);
            return;
        }
        if (const auto *obj = findObject(node, "lockupViewModel")) {
            if (obj->contains("contentType") && (*obj)["contentType"].is_string() &&
                (*obj)["contentType"].get<std::string>() == "LOCKUP_CONTENT_TYPE_PLAYLIST") {
                outList.push_back(obj);
                return;
            }
        }

        for (const auto &[key, val] : node.items()) {
            if (!isIgnoredSubtree(key)) {
                recursivelyFindPlaylistRenderers(val, outList, depth + 1);
            }
        }
    } else if (node.is_array()) {
        for (const auto &item : node) {
            recursivelyFindPlaylistRenderers(item, outList, depth + 1);
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

bool parseCommentRenderer(const nlohmann::json &cr, CommentItem &outItem, bool isReply, bool preferWebp) {
    if (!cr.is_object())
        return false;
    outItem.isReply = isReply;
    if (cr.contains("commentId") && cr["commentId"].is_string()) {
        outItem.commentId = cr["commentId"].get<std::string>();
    }
    if (cr.contains("authorText")) {
        outItem.author = InnerTubeParser::extractText(cr["authorText"]);
    }
    if (cr.contains("authorEndpoint") && cr["authorEndpoint"].is_object()) {
        const auto &ep = cr["authorEndpoint"];
        if (ep.contains("browseEndpoint") && ep["browseEndpoint"].contains("browseId")) {
            outItem.authorChannelId = ep["browseEndpoint"].value("browseId", "");
        }
    }
    if (cr.contains("authorThumbnail")) {
        outItem.authorAvatarUrl = InnerTubeParser::extractBestThumbnail(cr["authorThumbnail"], "", preferWebp);
    }
    if (cr.contains("publishedTimeText")) {
        outItem.publishedTime = InnerTubeParser::extractText(cr["publishedTimeText"]);
    }
    if (cr.contains("contentText")) {
        outItem.text = InnerTubeParser::extractText(cr["contentText"]);
    }
    if (cr.contains("voteCount")) {
        outItem.likeCountText = InnerTubeParser::extractText(cr["voteCount"]);
        outItem.likeCount = InnerTubeParser::parseLikeCountString(outItem.likeCountText);
    }
    if (cr.contains("voteStatus") && cr["voteStatus"].is_string()) {
        std::string vs = cr["voteStatus"].get<std::string>();
        if (vs == "LIKE") {
            outItem.isLiked = true;
            outItem.isDisliked = false;
        } else if (vs == "DISLIKE") {
            outItem.isDisliked = true;
            outItem.isLiked = false;
        }
    }
    if (cr.contains("authorIsChannelOwner") && cr["authorIsChannelOwner"].is_boolean()) {
        outItem.isAuthorChannelOwner = cr["authorIsChannelOwner"].get<bool>();
    }
    if (cr.contains("pinnedCommentBadge")) {
        outItem.isPinned = true;
    }
    if (const auto *actions = findObject(cr, "actionButtons")) {
        if (const auto *cab = findObject(*actions, "commentActionButtonsRenderer")) {
            if (const auto *ch = findObject(*cab, "creatorHeart")) {
                if (const auto *chr = findObject(*ch, "creatorHeartRenderer")) {
                    outItem.isHearted = chr->value("isHearted", false);
                    outItem.heartTooltip = chr->value("tooltip", "");
                }
            }
            if (const auto *lb = findObject(*cab, "likeButton")) {
                if (const auto *tb = findObject(*lb, "toggleButtonRenderer")) {
                    if (tb->contains("isToggled") && (*tb)["isToggled"].is_boolean() &&
                        (*tb)["isToggled"].get<bool>()) {
                        outItem.isLiked = true;
                    }
                    if (const auto *dse = findObject(*tb, "defaultServiceEndpoint")) {
                        if (const auto *pca = findObject(*dse, "performCommentActionEndpoint")) {
                            outItem.likeActionToken = pca->value("action", "");
                        }
                    }
                    if (const auto *tse = findObject(*tb, "toggledServiceEndpoint")) {
                        if (const auto *pca = findObject(*tse, "performCommentActionEndpoint")) {
                            outItem.unlikeActionToken = pca->value("action", "");
                        }
                    }
                }
            }
            if (const auto *db = findObject(*cab, "dislikeButton")) {
                if (const auto *tb = findObject(*db, "toggleButtonRenderer")) {
                    if (tb->contains("isToggled") && (*tb)["isToggled"].is_boolean() &&
                        (*tb)["isToggled"].get<bool>()) {
                        outItem.isDisliked = true;
                    }
                    if (const auto *dse = findObject(*tb, "defaultServiceEndpoint")) {
                        if (const auto *pca = findObject(*dse, "performCommentActionEndpoint")) {
                            outItem.dislikeActionToken = pca->value("action", "");
                        }
                    }
                    if (const auto *tse = findObject(*tb, "toggledServiceEndpoint")) {
                        if (const auto *pca = findObject(*tse, "performCommentActionEndpoint")) {
                            outItem.undislikeActionToken = pca->value("action", "");
                        }
                    }
                }
            }
        }
    }
    return !outItem.text.empty() || !outItem.author.empty();
}

bool parseCommentViewModel(const nlohmann::json &vm,
                           const std::unordered_map<std::string, const nlohmann::json *> &entityMap,
                           CommentItem &outItem, bool isReply, bool preferWebp) {
    if (!vm.is_object())
        return false;
    outItem.isReply = isReply;
    if (vm.contains("commentId") && vm["commentId"].is_string()) {
        outItem.commentId = vm["commentId"].get<std::string>();
    }
    const nlohmann::json *payload = nullptr;
    if (!outItem.commentId.empty()) {
        auto it = entityMap.find(outItem.commentId);
        if (it != entityMap.end()) {
            payload = it->second;
        }
    }
    if (payload != nullptr) {
        if (const auto *props = findObject(*payload, "properties")) {
            if (outItem.commentId.empty()) {
                outItem.commentId = props->value("commentId", "");
            }
            if (props->contains("content") && (*props)["content"].contains("content")) {
                outItem.text = (*props)["content"].value("content", "");
            }
            if (outItem.publishedTime.empty()) {
                outItem.publishedTime = props->value("publishedTime", "");
            }
        }
        if (const auto *author = findObject(*payload, "author")) {
            if (outItem.author.empty()) {
                outItem.author = author->value("displayName", "");
            }
            if (outItem.authorChannelId.empty()) {
                outItem.authorChannelId = author->value("channelId", "");
            }
            if (outItem.authorAvatarUrl.empty()) {
                outItem.authorAvatarUrl = author->value("avatarThumbnailUrl", "");
            }
            outItem.isAuthorChannelOwner = author->value("isCreator", false);
        }
        if (const auto *toolbar = findObject(*payload, "toolbar")) {
            std::string lk = toolbar->value("likeCountNotliked", "");
            if (lk.empty())
                lk = toolbar->value("likeCountLiked", "");
            if (!lk.empty()) {
                outItem.likeCountText = lk;
                outItem.likeCount = InnerTubeParser::parseLikeCountString(lk);
            }
            if (toolbar->contains("replyCount")) {
                if ((*toolbar)["replyCount"].is_number()) {
                    outItem.replyCount = (*toolbar)["replyCount"].get<int>();
                } else if ((*toolbar)["replyCount"].is_string()) {
                    outItem.replyCount = std::max(0, std::atoi((*toolbar)["replyCount"].get<std::string>().c_str()));
                }
                if (outItem.replyCount > 0 && outItem.replyCountText.empty()) {
                    outItem.replyCountText =
                        std::to_string(outItem.replyCount) + (outItem.replyCount == 1 ? " reply" : " replies");
                }
            }
            outItem.isHearted = toolbar->value("isHearted", false);
            outItem.heartTooltip = toolbar->value("heartActiveTooltip", "");

            auto extractAction = [](const nlohmann::json &cmd) -> std::string {
                if (const auto *ic = findObject(cmd, "innertubeCommand")) {
                    if (const auto *pca = findObject(*ic, "performCommentActionEndpoint")) {
                        return pca->value("action", "");
                    }
                }
                if (const auto *pca = findObject(cmd, "performCommentActionEndpoint")) {
                    return pca->value("action", "");
                }
                return {};
            };
            if (const auto *lc = findObject(*toolbar, "likeCommand")) {
                outItem.likeActionToken = extractAction(*lc);
            }
            if (const auto *ulc = findObject(*toolbar, "unlikeCommand")) {
                outItem.unlikeActionToken = extractAction(*ulc);
            }
            if (const auto *dc = findObject(*toolbar, "dislikeCommand")) {
                outItem.dislikeActionToken = extractAction(*dc);
            }
            if (const auto *udc = findObject(*toolbar, "undislikeCommand")) {
                outItem.undislikeActionToken = extractAction(*udc);
            }
        }
    }
    if (outItem.text.empty() && vm.contains("contentText")) {
        outItem.text = InnerTubeParser::extractText(vm["contentText"]);
    }
    if (outItem.text.empty() && vm.contains("content")) {
        outItem.text = InnerTubeParser::extractText(vm["content"]);
    }
    if (outItem.author.empty() && vm.contains("authorText")) {
        outItem.author = InnerTubeParser::extractText(vm["authorText"]);
    }
    if (outItem.author.empty() && vm.contains("authorName")) {
        outItem.author = InnerTubeParser::extractText(vm["authorName"]);
    }
    if (outItem.publishedTime.empty() && vm.contains("publishedTimeText")) {
        outItem.publishedTime = InnerTubeParser::extractText(vm["publishedTimeText"]);
    }
    if (outItem.authorAvatarUrl.empty() && vm.contains("authorAvatarImage")) {
        outItem.authorAvatarUrl = InnerTubeParser::extractBestThumbnail(vm["authorAvatarImage"], "", preferWebp);
    }
    if (outItem.likeCountText.empty() && vm.contains("likeCount")) {
        outItem.likeCountText = InnerTubeParser::extractText(vm["likeCount"]);
        outItem.likeCount = InnerTubeParser::parseLikeCountString(outItem.likeCountText);
    }
    if (vm.contains("isPinned") && vm["isPinned"].is_boolean()) {
        outItem.isPinned = vm["isPinned"].get<bool>();
    }
    if (vm.contains("pinnedBadge") || vm.contains("pinnedText")) {
        outItem.isPinned = true;
    }
    if (vm.contains("authorIsChannelOwner") && vm["authorIsChannelOwner"].is_boolean()) {
        outItem.isAuthorChannelOwner = vm["authorIsChannelOwner"].get<bool>();
    }
    return !outItem.text.empty() || !outItem.author.empty();
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

std::vector<PlaylistItem> InnerTubeParser::extractPlaylists(const nlohmann::json &root) {
    std::vector<const nlohmann::json *> renderers;
    recursivelyFindPlaylistRenderers(root, renderers);

    std::vector<PlaylistItem> playlists;
    std::unordered_set<std::string> seenIds;
    playlists.reserve(renderers.size());

    for (const auto *pr : renderers) {
        if (!pr)
            continue;
        PlaylistItem item;
        if (parsePlaylistRenderer(*pr, item)) {
            if (!item.id.empty() && !seenIds.contains(item.id)) {
                seenIds.insert(item.id);
                playlists.push_back(std::move(item));
            }
        }
    }
    return playlists;
}

bool InnerTubeParser::parsePlaylistRenderer(const nlohmann::json &pr, PlaylistItem &outItem) {
    if (!pr.is_object()) {
        return false;
    }

    // Modern lockupViewModel (contentType == "LOCKUP_CONTENT_TYPE_PLAYLIST")
    if (pr.contains("contentType") && pr["contentType"].is_string() &&
        pr["contentType"].get<std::string>() == "LOCKUP_CONTENT_TYPE_PLAYLIST") {
        if (pr.contains("contentId") && pr["contentId"].is_string()) {
            outItem.id = pr["contentId"].get<std::string>();
        }

        auto checkPlaylistEndpoints = [&](const nlohmann::json &node) {
            if (!node.is_object())
                return;
            if (node.contains("onTap") && node["onTap"].is_object()) {
                const auto &ot = node["onTap"];
                if (ot.contains("innertubeCommand") && ot["innertubeCommand"].is_object()) {
                    const auto &ic = ot["innertubeCommand"];
                    if (ic.contains("watchEndpoint") && ic["watchEndpoint"].is_object()) {
                        if (outItem.id.empty()) {
                            outItem.id = ic["watchEndpoint"].value("playlistId", "");
                        }
                    }
                    if (ic.contains("browseEndpoint") && ic["browseEndpoint"].is_object()) {
                        outItem.browseId = ic["browseEndpoint"].value("browseId", "");
                    }
                }
            }
        };

        checkPlaylistEndpoints(pr);
        if (const auto *rc = findObject(pr, "rendererContext")) {
            if (const auto *cc = findObject(*rc, "commandContext")) {
                checkPlaylistEndpoints(*cc);
            }
        }

        if (outItem.browseId.empty() && !outItem.id.empty()) {
            outItem.browseId = outItem.id.starts_with("VL") ? outItem.id : ("VL" + outItem.id);
        }
        if (outItem.id.empty() && !outItem.browseId.empty()) {
            outItem.id = outItem.browseId.starts_with("VL") ? outItem.browseId.substr(2) : outItem.browseId;
        }

        if (const auto *meta = findObject(pr, "metadata")) {
            if (const auto *lmvm = findObject(*meta, "lockupMetadataViewModel")) {
                if (lmvm->contains("title")) {
                    outItem.title = extractText((*lmvm)["title"]);
                }
                if (const auto *m = findObject(*lmvm, "metadata")) {
                    if (const auto *cmvm = findObject(*m, "contentMetadataViewModel")) {
                        if (const auto *rows = findArray(*cmvm, "metadataRows")) {
                            for (const auto &row : *rows) {
                                auto checkParts = [&](const nlohmann::json *parts) {
                                    if (!parts)
                                        return;
                                    for (const auto &part : *parts) {
                                        std::string t = extractText(part.contains("text") ? part["text"] : part);
                                        if (t.empty())
                                            continue;
                                        if (outItem.author.empty()) {
                                            outItem.author = t;
                                        } else if (outItem.updatedTime.empty()) {
                                            outItem.updatedTime = t;
                                        }
                                    }
                                };
                                checkParts(findArray(row, "parts"));
                                checkParts(findArray(row, "metadataParts"));
                            }
                        }
                    }
                }
            }
        }

        if (const auto *ci = findObject(pr, "contentImage")) {
            const nlohmann::json *primaryThumb = nullptr;
            if (const auto *ctvm = findObject(*ci, "collectionThumbnailViewModel")) {
                if (const auto *pt = findObject(*ctvm, "primaryThumbnail")) {
                    primaryThumb = findObject(*pt, "thumbnailViewModel");
                }
            } else if (const auto *tvm = findObject(*ci, "thumbnailViewModel")) {
                primaryThumb = tvm;
            }

            if (primaryThumb) {
                outItem.thumbnailUrl = extractBestThumbnail(*primaryThumb, outItem.id);
                if (const auto *overlays = findArray(*primaryThumb, "overlays")) {
                    for (const auto &ov : *overlays) {
                        if (const auto *tbvm = findObject(ov, "thumbnailBadgeViewModel")) {
                            if (tbvm->contains("text")) {
                                outItem.videoCountText = extractText((*tbvm)["text"]);
                            }
                        }
                        if (outItem.videoCountText.empty()) {
                            if (const auto *tots = findObject(ov, "thumbnailOverlayTimeStatusRenderer")) {
                                if (tots->contains("text")) {
                                    outItem.videoCountText = extractText((*tots)["text"]);
                                }
                            }
                        }
                        if (outItem.videoCountText.empty()) {
                            if (const auto *tobp = findObject(ov, "thumbnailOverlayBottomPanelViewModel")) {
                                if (tobp->contains("text")) {
                                    outItem.videoCountText = extractText((*tobp)["text"]);
                                }
                            }
                        }
                    }
                }
            }
        }

        if (outItem.thumbnailUrl.empty() && !outItem.id.empty()) {
            outItem.thumbnailUrl = "https://i.ytimg.com/vi/" + outItem.id + "/mqdefault.jpg";
        }

        return !outItem.id.empty();
    }

    // Classic playlistRenderer or gridPlaylistRenderer
    if (pr.contains("playlistId") && pr["playlistId"].is_string()) {
        outItem.id = pr["playlistId"].get<std::string>();
    }
    if (const auto *ne = findObject(pr, "navigationEndpoint")) {
        if (const auto *be = findObject(*ne, "browseEndpoint")) {
            if (auto bid = findString(*be, "browseId"); !bid.empty()) {
                outItem.browseId = std::string(bid);
            }
        }
    }
    if (outItem.browseId.empty() && !outItem.id.empty()) {
        outItem.browseId = outItem.id.starts_with("VL") ? outItem.id : ("VL" + outItem.id);
    }
    if (outItem.id.empty() && !outItem.browseId.empty()) {
        outItem.id = outItem.browseId.starts_with("VL") ? outItem.browseId.substr(2) : outItem.browseId;
    }

    if (pr.contains("title")) {
        outItem.title = extractText(pr["title"]);
    }

    if (pr.contains("shortBylineText")) {
        outItem.author = extractText(pr["shortBylineText"]);
    } else if (pr.contains("longBylineText")) {
        outItem.author = extractText(pr["longBylineText"]);
    }

    if (pr.contains("videoCountText")) {
        outItem.videoCountText = extractText(pr["videoCountText"]);
    } else if (pr.contains("videoCountShortText")) {
        outItem.videoCountText = extractText(pr["videoCountShortText"]);
    } else if (pr.contains("thumbnailText")) {
        outItem.videoCountText = extractText(pr["thumbnailText"]);
    }

    if (pr.contains("publishedTimeText")) {
        outItem.updatedTime = extractText(pr["publishedTimeText"]);
    }

    if (pr.contains("thumbnails")) {
        outItem.thumbnailUrl = extractBestThumbnail(pr["thumbnails"], outItem.id);
    } else if (pr.contains("thumbnail")) {
        outItem.thumbnailUrl = extractBestThumbnail(pr["thumbnail"], outItem.id);
    } else if (pr.contains("sidebarThumbnails")) {
        outItem.thumbnailUrl = extractBestThumbnail(pr["sidebarThumbnails"], outItem.id);
    }

    if (outItem.thumbnailUrl.empty() && !outItem.id.empty()) {
        outItem.thumbnailUrl = "https://i.ytimg.com/vi/" + outItem.id + "/mqdefault.jpg";
    }

    return !outItem.id.empty();
}

bool InnerTubeParser::parsePlaylistHeader(const nlohmann::json &root, PlaylistItem &outDetails) {
    if (!root.is_object()) {
        return false;
    }

    const nlohmann::json *headerNode = nullptr;
    if (const auto *h = findObject(root, "header")) {
        headerNode = h;
    } else {
        headerNode = &root;
    }

    // 1. Classic playlistHeaderRenderer
    std::vector<const nlohmann::json *> classicHeaders;
    auto findClassic = [&](auto &self, const nlohmann::json &node, int depth) -> void {
        if (!node.is_object() || depth > 8)
            return;
        if (const auto *ph = findObject(node, "playlistHeaderRenderer")) {
            classicHeaders.push_back(ph);
            return;
        }
        for (const auto &[k, v] : node.items()) {
            if (v.is_object() && !isIgnoredSubtree(k)) {
                self(self, v, depth + 1);
            }
        }
    };
    findClassic(findClassic, *headerNode, 0);

    if (!classicHeaders.empty()) {
        const auto &ph = *classicHeaders.front();
        if (ph.contains("playlistId") && ph["playlistId"].is_string()) {
            if (outDetails.id.empty())
                outDetails.id = ph["playlistId"].get<std::string>();
        }
        if (ph.contains("title")) {
            std::string t = extractText(ph["title"]);
            if (!t.empty())
                outDetails.title = std::move(t);
        }
        if (ph.contains("ownerText")) {
            std::string a = extractText(ph["ownerText"]);
            if (!a.empty())
                outDetails.author = std::move(a);
        }
        if (ph.contains("numVideosText")) {
            std::string c = extractText(ph["numVideosText"]);
            if (!c.empty())
                outDetails.videoCountText = std::move(c);
        }
        if (ph.contains("viewCountText")) {
            std::string v = extractText(ph["viewCountText"]);
            if (!v.empty())
                outDetails.viewCountText = std::move(v);
        }
        if (ph.contains("descriptionText")) {
            std::string d = extractText(ph["descriptionText"]);
            if (!d.empty())
                outDetails.description = std::move(d);
        }
        if (ph.contains("playlistThumbnail")) {
            std::string thumb = extractBestThumbnail(ph["playlistThumbnail"], outDetails.id);
            if (!thumb.empty())
                outDetails.thumbnailUrl = std::move(thumb);
        }
        return !outDetails.title.empty() || !outDetails.id.empty();
    }

    // 2. Modern pageHeaderRenderer / pageHeaderViewModel
    std::vector<const nlohmann::json *> modernHeaders;
    auto findModern = [&](auto &self, const nlohmann::json &node, int depth) -> void {
        if (!node.is_object() || depth > 8)
            return;
        if (const auto *ph = findObject(node, "pageHeaderViewModel")) {
            modernHeaders.push_back(ph);
            return;
        }
        for (const auto &[k, v] : node.items()) {
            if (v.is_object() && !isIgnoredSubtree(k)) {
                self(self, v, depth + 1);
            }
        }
    };
    findModern(findModern, *headerNode, 0);

    if (!modernHeaders.empty()) {
        const auto &vm = *modernHeaders.front();
        if (const auto *dt = findObject(vm, "dynamicTextViewModel")) {
            if (dt->contains("text")) {
                std::string t = extractText((*dt)["text"]);
                if (!t.empty())
                    outDetails.title = std::move(t);
            }
        } else if (vm.contains("title")) {
            std::string t = extractText(vm["title"]);
            if (!t.empty())
                outDetails.title = std::move(t);
        }

        if (const auto *cm = findObject(vm, "contentMetadataViewModel")) {
            if (const auto *rows = findArray(*cm, "metadataRows")) {
                for (const auto &row : *rows) {
                    if (!row.is_object())
                        continue;
                    if (const auto *as = findObject(row, "avatarStackViewModel")) {
                        if (as->contains("avatarStackText")) {
                            std::string a = extractText((*as)["avatarStackText"]);
                            if (!a.empty())
                                outDetails.author = std::move(a);
                        }
                    }
                    if (const auto *parts = findArray(row, "metadataParts")) {
                        for (const auto &part : *parts) {
                            if (!part.is_object())
                                continue;
                            if (part.contains("text")) {
                                std::string txt = extractText(part["text"]);
                                if (txt.find("video") != std::string::npos) {
                                    outDetails.videoCountText = txt;
                                } else if (txt.find("view") != std::string::npos) {
                                    outDetails.viewCountText = txt;
                                } else if (txt != "Playlist" && outDetails.updatedTime.empty()) {
                                    outDetails.updatedTime = txt;
                                }
                            }
                        }
                    }
                }
            }
        }

        if (const auto *dp = findObject(vm, "descriptionPreviewViewModel")) {
            if (dp->contains("description")) {
                std::string d = extractText((*dp)["description"]);
                if (!d.empty())
                    outDetails.description = std::move(d);
            }
        }

        if (const auto *cpi = findObject(vm, "contentPreviewImageViewModel")) {
            if (cpi->contains("image")) {
                std::string thumb = extractBestThumbnail((*cpi)["image"], outDetails.id);
                if (!thumb.empty())
                    outDetails.thumbnailUrl = std::move(thumb);
            }
        }

        return !outDetails.title.empty() || !outDetails.id.empty();
    }

    return false;
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

    if ((outItem.viewCount.empty() || outItem.publishedTime.empty()) && vr.contains("videoInfo")) {
        const auto &vi = vr["videoInfo"];
        if (vi.is_object()) {
            auto isOnlySeparator = [](std::string_view s) noexcept -> bool {
                if (s.empty())
                    return true;
                for (unsigned char c : s) {
                    if (std::isalnum(c))
                        return false;
                }
                return true;
            };

            auto splitAndCollect = [&](std::string_view str, std::vector<std::string> &out) {
                constexpr std::string_view kBullet = "•";
                constexpr std::string_view kMiddleDot = "·";
                size_t pos = str.find(kBullet);
                size_t delimLen = kBullet.size();
                if (pos == std::string_view::npos) {
                    pos = str.find(kMiddleDot);
                    delimLen = kMiddleDot.size();
                }
                if (pos == std::string_view::npos) {
                    pos = str.find(" - ");
                    delimLen = 3;
                }

                if (pos != std::string_view::npos) {
                    std::string_view p1 = utils::trim(str.substr(0, pos));
                    std::string_view p2 = utils::trim(str.substr(pos + delimLen));
                    if (!isOnlySeparator(p1)) {
                        out.emplace_back(p1);
                    }
                    if (!isOnlySeparator(p2)) {
                        out.emplace_back(p2);
                    }
                } else {
                    std::string_view trimmed = utils::trim(str);
                    if (!isOnlySeparator(trimmed)) {
                        out.emplace_back(trimmed);
                    }
                }
            };

            std::vector<std::string> parts;
            if (vi.contains("runs") && vi["runs"].is_array()) {
                for (const auto &run : vi["runs"]) {
                    if (run.is_object() && run.contains("text") && run["text"].is_string()) {
                        splitAndCollect(run["text"].get_ref<const std::string &>(), parts);
                    }
                }
            } else if (vi.contains("simpleText") && vi["simpleText"].is_string()) {
                splitAndCollect(vi["simpleText"].get_ref<const std::string &>(), parts);
            }

            for (const auto &part : parts) {
                if (outItem.viewCount.empty() &&
                    (part.find("view") != std::string::npos || part.find("View") != std::string::npos ||
                     part.find("watching") != std::string::npos || part.find("Watching") != std::string::npos)) {
                    outItem.viewCount = part;
                } else if (outItem.publishedTime.empty()) {
                    outItem.publishedTime = part;
                }
            }
        }
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

    // 3. Extract relatedVideos from twoColumnWatchNextResults.secondaryResults (or root fallback), excluding active
    // videoId
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

    // 5. Extract commentsContinuationToken and commentCountText from primary column results (not secondaryResults)
    std::function<void(const nlohmann::json &, int)> findCommentsInfo = [&](const nlohmann::json &node, int depth) {
        if (depth > 32 || (!outMeta.commentsContinuationToken.empty() && !outMeta.commentCountText.empty())) {
            return;
        }
        if (node.is_object()) {
            if (const auto *cep = findObject(node, "commentsEntryPointHeaderRenderer")) {
                if (outMeta.commentCountText.empty()) {
                    if (cep->contains("commentCount")) {
                        outMeta.commentCountText = extractText((*cep)["commentCount"]);
                    } else if (cep->contains("headerText")) {
                        outMeta.commentCountText = extractText((*cep)["headerText"]);
                    }
                }
            }
            if (const auto *ch = findObject(node, "commentsHeaderRenderer")) {
                if (outMeta.commentCountText.empty()) {
                    if (ch->contains("countText")) {
                        outMeta.commentCountText = extractText((*ch)["countText"]);
                    } else if (ch->contains("commentsCount")) {
                        outMeta.commentCountText = extractText((*ch)["commentsCount"]);
                    }
                }
            }
            if (const auto *isr = findObject(node, "itemSectionRenderer")) {
                std::string_view secId = findString(*isr, "sectionIdentifier");
                std::string_view tgtId = findString(*isr, "targetId");
                if (secId == "comment-item-section" || tgtId == "comments-section" ||
                    secId.find("comment") != std::string_view::npos ||
                    (secId.empty() && tgtId.empty())) {
                    if (outMeta.commentsContinuationToken.empty()) {
                        recursivelyFindContinuationToken(*isr, outMeta.commentsContinuationToken);
                    }
                    if (outMeta.commentCountText.empty()) {
                        if (const auto *cep = findObject(*isr, "commentsEntryPointHeaderRenderer")) {
                            outMeta.commentCountText = extractText((*cep)["commentCount"]);
                        }
                    }
                }
            }
            if (const auto *cir = findObject(node, "continuationItemRenderer")) {
                if (outMeta.commentsContinuationToken.empty()) {
                    recursivelyFindContinuationToken(*cir, outMeta.commentsContinuationToken);
                }
            }
            for (const auto &[k, v] : node.items()) {
                if (k != "secondaryResults" && !isIgnoredSubtree(k)) {
                    findCommentsInfo(v, depth + 1);
                }
            }
        } else if (node.is_array()) {
            for (const auto &item : node) {
                findCommentsInfo(item, depth + 1);
            }
        }
    };

    if (twoColumn != nullptr) {
        if (const auto *res = findObject(*twoColumn, "results")) {
            findCommentsInfo(*res, 0);
        }
    }
    if (outMeta.commentsContinuationToken.empty()) {
        findCommentsInfo(root, 0);
    }

    return !outMeta.channelAvatarUrl.empty() || !outMeta.description.empty() || !outMeta.title.empty() ||
           outMeta.likeCount > 0 || outMeta.isLiked || outMeta.isDisliked || !outMeta.relatedVideos.empty() ||
           !outMeta.autoplayVideoId.empty() || !outMeta.commentsContinuationToken.empty();
}

bool InnerTubeParser::parseCommentsResponse(const nlohmann::json &root, CommentsResult &outResult, bool preferWebp) {
    if (!root.is_object()) {
        return false;
    }

    // 1. Build lookup table for modern EntityBatchUpdate mutations (keyed by commentId)
    std::unordered_map<std::string, const nlohmann::json *> commentEntityMap;
    std::unordered_map<std::string, const nlohmann::json *> toolbarStateMap;
    std::unordered_map<std::string, const nlohmann::json *> toolbarSurfaceMap;
    std::unordered_map<std::string, const nlohmann::json *> commentSurfaceMap;

    auto indexMutations = [&](const nlohmann::json &mutationsArr) {
        if (!mutationsArr.is_array())
            return;
        for (const auto &mut : mutationsArr) {
            if (!mut.is_object() || !mut.contains("payload") || !mut["payload"].is_object())
                continue;
            const auto &payload = mut["payload"];
            if (const auto *cep = findObject(payload, "commentEntityPayload")) {
                if (const auto *props = findObject(*cep, "properties")) {
                    std::string cid = props->value("commentId", "");
                    if (!cid.empty()) {
                        commentEntityMap[cid] = cep;
                    }
                }
                std::string k = mut.value("entityKey", "");
                if (!k.empty()) {
                    commentEntityMap[k] = cep;
                }
            }
            if (const auto *tsp = findObject(payload, "engagementToolbarStateEntityPayload")) {
                std::string k = tsp->value("key", "");
                if (k.empty())
                    k = mut.value("entityKey", "");
                if (!k.empty())
                    toolbarStateMap[k] = tsp;
            }
            if (const auto *esp = findObject(payload, "engagementToolbarSurfaceEntityPayload")) {
                std::string k = esp->value("key", "");
                if (k.empty())
                    k = mut.value("entityKey", "");
                if (!k.empty())
                    toolbarSurfaceMap[k] = esp;
            }
            if (const auto *csp = findObject(payload, "commentSurfaceEntityPayload")) {
                std::string k = csp->value("key", "");
                if (k.empty())
                    k = mut.value("entityKey", "");
                if (!k.empty())
                    commentSurfaceMap[k] = csp;
            }
        }
    };

    if (const auto *fw = findObject(root, "frameworkUpdates")) {
        if (const auto *ebu = findObject(*fw, "entityBatchUpdate")) {
            if (const auto *muts = findArray(*ebu, "mutations")) {
                indexMutations(*muts);
            }
        }
    }

    auto parseCommentObj = [&](const nlohmann::json &obj, CommentItem &outItem, bool isReply) -> bool {
        if (!obj.is_object())
            return false;
        if (const auto *cr = findObject(obj, "commentRenderer")) {
            return parseCommentRenderer(*cr, outItem, isReply, preferWebp);
        }
        if (const auto *cvm = findObject(obj, "commentViewModel")) {
            const nlohmann::json *innerVm = cvm;
            if (const auto *nested = findObject(*cvm, "commentViewModel")) {
                innerVm = nested;
            }
            return parseCommentViewModel(*innerVm, commentEntityMap, outItem, isReply, preferWebp);
        }
        if (obj.contains("commentId")) {
            return parseCommentViewModel(obj, commentEntityMap, outItem, isReply, preferWebp);
        }
        return false;
    };

    auto handleContinuationItems = [&](const nlohmann::json &itemsArr) {
        if (!itemsArr.is_array())
            return;
        for (const auto &item : itemsArr) {
            if (!item.is_object())
                continue;

            // 1. Comments header (count and sort menu)
            if (const auto *ch = findObject(item, "commentsHeaderRenderer")) {
                if (outResult.commentCountText.empty()) {
                    if (ch->contains("countText")) {
                        outResult.commentCountText = extractText((*ch)["countText"]);
                    } else if (ch->contains("commentsCount")) {
                        outResult.commentCountText = extractText((*ch)["commentsCount"]);
                    }
                }
                if (const auto *sm = findObject(*ch, "sortMenu")) {
                    if (const auto *sfs = findObject(*sm, "sortFilterSubMenuRenderer")) {
                        if (const auto *subItems = findArray(*sfs, "subMenuItems")) {
                            for (const auto &si : *subItems) {
                                if (!si.is_object())
                                    continue;
                                std::string title = extractText(si.value("title", nlohmann::json{}));
                                std::string token;
                                recursivelyFindContinuationToken(si, token);
                                if (!token.empty()) {
                                    std::string lowerTitle;
                                    lowerTitle.reserve(title.size());
                                    for (char c : title)
                                        lowerTitle.push_back(
                                            static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                                    if (lowerTitle.find("top") != std::string::npos) {
                                        outResult.topCommentsToken = token;
                                    } else if (lowerTitle.find("new") != std::string::npos) {
                                        outResult.newestCommentsToken = token;
                                    }
                                }
                            }
                        }
                    }
                }
                continue;
            }

            // 2. Comment Thread (top-level comment + replies)
            if (const auto *ctr = findObject(item, "commentThreadRenderer")) {
                CommentItem topItem;
                bool ok = false;
                if (const auto *cObj = findObject(*ctr, "comment")) {
                    ok = parseCommentObj(*cObj, topItem, false);
                } else if (const auto *vmObj = findObject(*ctr, "commentViewModel")) {
                    ok = parseCommentObj(*vmObj, topItem, false);
                }
                if (!ok) {
                    ok = parseCommentObj(*ctr, topItem, false);
                }

                if (ok && !topItem.commentId.empty()) {
                    // Check replies within thread
                    if (const auto *rep = findObject(*ctr, "replies")) {
                        if (const auto *crr = findObject(*rep, "commentRepliesRenderer")) {
                            if (const auto *vr = findObject(*crr, "viewReplies")) {
                                if (const auto *btn = findObject(*vr, "buttonRenderer")) {
                                    topItem.replyCountText = extractText((*btn)["text"]);
                                }
                            }
                            if (topItem.replyCountText.empty() && crr->contains("viewRepliesText")) {
                                topItem.replyCountText = extractText((*crr)["viewRepliesText"]);
                            }
                            if (topItem.replyCount <= 0 && !topItem.replyCountText.empty()) {
                                topItem.replyCount = static_cast<int>(parseLikeCountString(topItem.replyCountText));
                            }
                            recursivelyFindContinuationToken(*crr, topItem.repliesContinuationToken);

                            // Preloaded replies if any
                            if (const auto *cArr = findArray(*crr, "contents")) {
                                for (const auto &repItem : *cArr) {
                                    CommentItem inlineReply;
                                    if (parseCommentObj(repItem, inlineReply, true) && !inlineReply.commentId.empty()) {
                                        outResult.comments.push_back(std::move(inlineReply));
                                    }
                                }
                            }
                        }
                    }
                    outResult.comments.push_back(std::move(topItem));
                }
                continue;
            }

            // 3. Direct comment (e.g. In reply continuation streams)
            CommentItem directItem;
            if (parseCommentObj(item, directItem, true) && !directItem.commentId.empty()) {
                outResult.comments.push_back(std::move(directItem));
                continue;
            }

            // 4. Trailing stream continuation
            if (const auto *cir = findObject(item, "continuationItemRenderer")) {
                recursivelyFindContinuationToken(*cir, outResult.nextContinuationToken);
            }
        }
    };

    // Process onResponseReceivedEndpoints
    if (const auto *endpoints = findArray(root, "onResponseReceivedEndpoints")) {
        for (const auto &ep : *endpoints) {
            if (!ep.is_object())
                continue;
            if (const auto *cmd = findObject(ep, "reloadContinuationItemsCommand")) {
                if (const auto *items = findArray(*cmd, "continuationItems")) {
                    handleContinuationItems(*items);
                }
            }
            if (const auto *cmd = findObject(ep, "appendContinuationItemsAction")) {
                if (const auto *items = findArray(*cmd, "continuationItems")) {
                    handleContinuationItems(*items);
                }
            }
        }
    }

    // Older / alternate fallback: continuationContents
    if (const auto *cc = findObject(root, "continuationContents")) {
        if (const auto *csc = findObject(*cc, "commentSectionContinuation")) {
            if (const auto *hdr = findObject(*csc, "header")) {
                if (const auto *ch = findObject(*hdr, "commentsHeaderRenderer")) {
                    if (outResult.commentCountText.empty()) {
                        outResult.commentCountText = extractText((*ch)["countText"]);
                    }
                }
            }
            if (const auto *contents = findArray(*csc, "contents")) {
                handleContinuationItems(*contents);
            }
            if (outResult.nextContinuationToken.empty()) {
                recursivelyFindContinuationToken(*csc, outResult.nextContinuationToken);
            }
        }
        if (const auto *crc = findObject(*cc, "commentRepliesContinuation")) {
            if (const auto *contents = findArray(*crc, "contents")) {
                handleContinuationItems(*contents);
            }
            if (outResult.nextContinuationToken.empty()) {
                recursivelyFindContinuationToken(*crc, outResult.nextContinuationToken);
            }
        }
    }

    return !outResult.comments.empty() || !outResult.nextContinuationToken.empty() ||
           !outResult.commentCountText.empty();
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
