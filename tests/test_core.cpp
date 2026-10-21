#include "core/CookieManager.h"
#include "core/CurlApi.h"
#include "core/HttpClient.h"
#include "core/InnerTubeParser.h"
#include "core/StreamResolver.h"
#include "core/ThumbnailCache.h"
#include "core/YouTubeClient.h"
#include "player/PlayerController.h"
#include "utils/AppPaths.h"
#include "utils/Logger.h"
#include "utils/StringUtils.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

int g_passCount = 0;
int g_failCount = 0;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            std::cerr << "  [FAIL] " << __FILE__ << ":" << __LINE__ << ": " #cond << "\n";                             \
            ++g_failCount;                                                                                             \
        } else {                                                                                                       \
            ++g_passCount;                                                                                             \
        }                                                                                                              \
    } while (0)

#include <utility>

template<typename T, typename U>
bool valuesEqual(const T &a, const U &b) {
    if constexpr (std::is_integral_v<T> && std::is_integral_v<U> && !std::is_same_v<T, bool> &&
                  !std::is_same_v<U, bool>) {
        return std::cmp_equal(a, b);
    } else {
        return (a == b);
    }
}

#define CHECK_EQ(a, b)                                                                                                 \
    do {                                                                                                               \
        const auto &valA = (a);                                                                                        \
        const auto &valB = (b);                                                                                        \
        if (!valuesEqual(valA, valB)) {                                                                                \
            std::cerr << "  [FAIL] " << __FILE__ << ":" << __LINE__ << ": " #a " == " #b << " ('" << valA << "' != '"  \
                      << valB << "')\n";                                                                               \
            ++g_failCount;                                                                                             \
        } else {                                                                                                       \
            ++g_passCount;                                                                                             \
        }                                                                                                              \
    } while (0)

}  // namespace

void testRfc3174Sha1Vectors() {
    // RFC 3174 / FIPS 180-1 standard test vectors
    CHECK_EQ(CookieManager::sha1Hex("abc"), "a9993e364706816aba3e25717850c26c9cd0d89d");
    CHECK_EQ(CookieManager::sha1Hex(""), "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    CHECK_EQ(CookieManager::sha1Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
             "84983e441c3bd26ebaae4aa1f95129e5e54670f1");

    std::cout << "[PASS] testRfc3174Sha1Vectors\n";
}

void testCookieManagerNetscape() {
    CookieManager mgr;
    std::string sampleNetscape = "# Netscape HTTP Cookie File\n"
                                 "# http://curl.haxx.se/rfc/cookie_spec.html\n"
                                 ".youtube.com\tTRUE\t/\tTRUE\t2147483647\tSAPISID\tSampleSapisid123\n"
                                 ".youtube.com\tTRUE\t/\tTRUE\t2147483647\tSID\tSampleSid456\n"
                                 "#HttpOnly_.youtube.com\tTRUE\t/\tTRUE\t2147483647\tLOGIN_INFO\tSampleLoginInfo789\n";

    bool ok = mgr.loadFromString(sampleNetscape);
    CHECK(ok);
    CHECK(mgr.hasAuth());
    CHECK_EQ(mgr.getCookie("SAPISID"), "SampleSapisid123");
    CHECK_EQ(mgr.getCookie("SID"), "SampleSid456");
    CHECK_EQ(mgr.getCookie("LOGIN_INFO"), "SampleLoginInfo789");

    std::string hash = mgr.getSapisidHash("https://www.youtube.com");
    CHECK(hash.starts_with("SAPISIDHASH "));
    CHECK(hash.find('_') != std::string::npos);

    std::cout << "[PASS] testCookieManagerNetscape\n";
}

void testCookieManagerHeader() {
    CookieManager mgr;
    std::string sampleHeader = "SAPISID=HeaderSapisid123; SID=HeaderSid456; __Secure-3PAPISID=SecureSapisid789";

    bool ok = mgr.loadFromString(sampleHeader);
    CHECK(ok);
    CHECK(mgr.hasAuth());
    CHECK_EQ(mgr.getCookie("SAPISID"), "HeaderSapisid123");
    CHECK_EQ(mgr.getCookie("__Secure-3PAPISID"), "SecureSapisid789");

    std::cout << "[PASS] testCookieManagerHeader\n";
}

void testCookieManagerSaveAndLoadRoundtrip() {
    CookieManager mgr1;
    const std::string sampleNetscape =
        "# Netscape HTTP Cookie File\n"
        "# Generated for LightYT testing\n"
        ".youtube.com\tTRUE\t/\tTRUE\t2107186271\tSAPISID\tSampleSapisid123\n"
        ".google.com\tTRUE\t/\tTRUE\t2107186271\tSAPISID\tGoogleSapisidFallback\n"
        ".youtube.com\tTRUE\t/\tTRUE\t2107186271\tSID\tSampleSid456\n"
        "#HttpOnly_.youtube.com\tTRUE\t/\tTRUE\t2107186271\tLOGIN_INFO\tSampleLoginInfo789\n"
        ".youtube.com\tTRUE\t/\tFALSE\t0\tPREF\tf6=40000480&hl=en\n";

    bool ok1 = mgr1.loadFromString(sampleNetscape);
    CHECK(ok1);
    CHECK(mgr1.hasAuth());

    const std::filesystem::path tmpPath =
        std::filesystem::temp_directory_path() / "lightyt_test_cache" / "roundtrip_cookies.txt";
    bool saved = mgr1.saveToFile(tmpPath);
    CHECK(saved);

    CookieManager mgr2;
    bool ok2 = mgr2.loadFromFile(tmpPath);
    CHECK(ok2);
    CHECK(mgr2.hasAuth());

    CHECK_EQ(mgr1.getCookie("SAPISID"), mgr2.getCookie("SAPISID"));
    CHECK_EQ(mgr1.getCookie("LOGIN_INFO"), mgr2.getCookie("LOGIN_INFO"));
    CHECK_EQ(mgr1.getCookie("SID"), mgr2.getCookie("SID"));
    CHECK_EQ(mgr1.getCookieHeaderValue("www.youtube.com"), mgr2.getCookieHeaderValue("www.youtube.com"));

    // Verify deduplication: youtube.com SAPISID must take precedence over google.com SAPISID
    CHECK_EQ(mgr2.getCookie("SAPISID"), "SampleSapisid123");
    std::string header = mgr2.getCookieHeaderValue("www.youtube.com");
    CHECK(header.find("SAPISID=SampleSapisid123") != std::string::npos);
    CHECK(header.find("GoogleSapisidFallback") == std::string::npos);

#ifndef _WIN32
    auto perms = std::filesystem::status(tmpPath).permissions();
    CHECK((perms & std::filesystem::perms::others_read) == std::filesystem::perms::none);
    CHECK((perms & std::filesystem::perms::others_write) == std::filesystem::perms::none);
    CHECK((perms & std::filesystem::perms::group_read) == std::filesystem::perms::none);
    CHECK((perms & std::filesystem::perms::group_write) == std::filesystem::perms::none);
    CHECK((perms & std::filesystem::perms::owner_read) != std::filesystem::perms::none);
    CHECK((perms & std::filesystem::perms::owner_write) != std::filesystem::perms::none);
#endif

    std::cout << "[PASS] testCookieManagerSaveAndLoadRoundtrip\n";
}

void testCookieManagerConcurrentAccess() {
    CookieManager mgr;
    const std::filesystem::path tmpPath =
        std::filesystem::temp_directory_path() / "lightyt_test_cache" / "concurrent_cookies.txt";
    mgr.setCookieFilePath(tmpPath.string());

    std::atomic<bool> startFlag{false};
    std::atomic<bool> stopFlag{false};
    std::vector<std::thread> threads;

    // Mutator threads
    for (int i = 0; i < 3; ++i) {
        threads.emplace_back([&mgr, &startFlag, &stopFlag, i]() {
            while (!startFlag.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            int count = 0;
            while (!stopFlag.load(std::memory_order_relaxed) && count < 200) {
                mgr.setCookie("SAPISID", "val_" + std::to_string(count), ".youtube.com");
                mgr.setCookie("SID", "sid_" + std::to_string(i), ".youtube.com");
                mgr.setCookie("LOGIN_INFO", "info_" + std::to_string(count), ".youtube.com");
                ++count;
            }
        });
    }

    // Auto-save & save threads
    for (int i = 0; i < 2; ++i) {
        threads.emplace_back([&mgr, &startFlag, &stopFlag, &tmpPath]() {
            while (!startFlag.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            int count = 0;
            while (!stopFlag.load(std::memory_order_relaxed) && count < 100) {
                mgr.autoSaveIfChanged();
                if (count % 20 == 0) {
                    (void)mgr.saveToFile(tmpPath);
                }
                ++count;
                std::this_thread::yield();
            }
        });
    }

    // Reader threads
    for (int i = 0; i < 3; ++i) {
        threads.emplace_back([&mgr, &startFlag, &stopFlag]() {
            while (!startFlag.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            int count = 0;
            while (!stopFlag.load(std::memory_order_relaxed) && count < 200) {
                auto header = mgr.getCookieHeaderValue("www.youtube.com");
                auto sapisid = mgr.getCookie("SAPISID");
                bool auth = mgr.hasAuth();
                (void)header;
                (void)sapisid;
                (void)auth;
                ++count;
            }
        });
    }

    startFlag.store(true, std::memory_order_release);
    for (auto &t : threads) {
        t.join();
    }

    CHECK(mgr.hasAuth());
    bool saved = mgr.saveToFile(tmpPath);
    CHECK(saved);

    CookieManager reloaded;
    CHECK(reloaded.loadFromFile(tmpPath));
    CHECK(reloaded.hasAuth());

    std::cout << "[PASS] testCookieManagerConcurrentAccess\n";
}

void testCookieManagerHeaderForUrlAndLockContention() {
    CookieManager mgr;

    // Active login and session cookies
    mgr.setCookie("SAPISID", "valid_sapisid_token", ".youtube.com");
    mgr.setCookie("SID", "valid_sid_token", ".youtube.com");

    // Add an expired cookie via Set-Cookie with past Max-Age / Expires
    mgr.updateFromSetCookie("EXPIRED_EXP=old_val; Domain=.youtube.com; Expires=Thu, 01 Jan 1970 00:00:00 GMT");
    mgr.updateFromSetCookie("EXPIRED_MAX=old_val; Domain=.youtube.com; Max-Age=-3600");

    // Future non-expired cookie
    mgr.updateFromSetCookie("FUTURE_COOKIE=future_val; Domain=.youtube.com; Max-Age=86400");

    // Verify getCookieHeaderForUrl extracts host properly from various URL shapes
    std::string h1 = mgr.getCookieHeaderForUrl("https://www.youtube.com/feed/subscriptions");
    std::string h2 = mgr.getCookieHeaderForUrl("http://www.youtube.com:443/watch?v=12345");
    std::string h3 = mgr.getCookieHeaderForUrl("www.youtube.com");
    std::string h4 = mgr.getCookieHeaderValue("www.youtube.com");

    CHECK_EQ(h1, h4);
    CHECK_EQ(h2, h4);
    CHECK_EQ(h3, h4);

    // Verify active cookies are present and expired ones are skipped
    CHECK(h1.find("SAPISID=valid_sapisid_token") != std::string::npos);
    CHECK(h1.find("SID=valid_sid_token") != std::string::npos);
    CHECK(h1.find("FUTURE_COOKIE=future_val") != std::string::npos);
    CHECK(h1.find("EXPIRED_EXP") == std::string::npos);
    CHECK(h1.find("EXPIRED_MAX") == std::string::npos);

    // Verify concurrent reads run concurrently without deadlocks
    std::atomic<bool> startFlag{false};
    std::vector<std::thread> readers;
    for (int i = 0; i < 4; ++i) {
        readers.emplace_back([&mgr, &startFlag]() {
            while (!startFlag.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (int j = 0; j < 100; ++j) {
                std::string res = mgr.getCookieHeaderForUrl("https://www.youtube.com/feed/history");
                CHECK(!res.empty());
            }
        });
    }

    startFlag.store(true, std::memory_order_release);
    for (auto &t : readers) {
        t.join();
    }

    std::cout << "[PASS] testCookieManagerHeaderForUrlAndLockContention\n";
}

void testJsonClassicVideoRenderer() {
    nlohmann::json sampleJson = nlohmann::json::parse(R"({
        "contents": {
            "twoColumnBrowseResultsRenderer": {
                "tabs": [
                    {
                        "tabRenderer": {
                            "content": {
                                "richGridRenderer": {
                                    "contents": [
                                        {
                                            "richItemRenderer": {
                                                "content": {
                                                    "videoRenderer": {
                                                        "videoId": "abc123xyz",
                                                        "title": {
                                                            "runs": [
                                                                { "text": "Building a Native C++ YouTube App" }
                                                            ]
                                                        },
                                                        "ownerText": {
                                                            "runs": [
                                                                { "text": "Tech Channel" }
                                                            ]
                                                        },
                                                        "lengthText": {
                                                            "simpleText": "14:22"
                                                        },
                                                        "viewCountText": {
                                                            "simpleText": "54K views"
                                                        },
                                                        "publishedTimeText": {
                                                            "simpleText": "3 days ago"
                                                        },
                                                        "descriptionSnippet": {
                                                            "runs": [
                                                                { "text": "This is a detailed video description snippet." }
                                                            ]
                                                        },
                                                        "channelThumbnailSupportedRenderers": {
                                                            "channelThumbnailWithLinkRenderer": {
                                                                "thumbnail": {
                                                                    "thumbnails": [
                                                                        { "url": "https://yt3.ggpht.com/classic_avatar=s68-c-k-c0x00ffffff-no-rj", "width": 68, "height": 68 }
                                                                    ]
                                                                }
                                                            }
                                                        },
                                                        "thumbnail": {
                                                            "thumbnails": [
                                                                { "url": "https://i.ytimg.com/vi/abc123xyz/hqdefault.jpg", "width": 480, "height": 360 }
                                                            ]
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    ]
                                }
                            }
                        }
                    }
                ]
            }
        }
    })");

    auto videos = YouTubeClient::extractVideosFromJson(sampleJson);
    CHECK_EQ(videos.size(), 1);
    if (!videos.empty()) {
        CHECK_EQ(videos[0].id, "abc123xyz");
        CHECK_EQ(videos[0].title, "Building a Native C++ YouTube App");
        CHECK_EQ(videos[0].author, "Tech Channel");
        CHECK_EQ(videos[0].duration, "14:22");
        CHECK_EQ(videos[0].viewCount, "54K views");
        CHECK_EQ(videos[0].publishedTime, "3 days ago");
        CHECK_EQ(videos[0].description, "This is a detailed video description snippet.");
        CHECK_EQ(videos[0].channelAvatarUrl, "https://yt3.ggpht.com/classic_avatar=s68-c-k-c0x00ffffff-no-rj");
        CHECK_EQ(videos[0].thumbnailUrl, "https://i.ytimg.com/vi/abc123xyz/hqdefault.jpg");
        CHECK_EQ(videos[0].getWatchUrl(), "https://www.youtube.com/watch?v=abc123xyz");
    }

    std::cout << "[PASS] testJsonClassicVideoRenderer\n";
}

void testJsonModernLockupViewModel() {
    nlohmann::json sampleJson = nlohmann::json::parse(R"({
        "contents": {
            "sectionListRenderer": {
                "contents": [
                    {
                        "itemSectionRenderer": {
                            "contents": [
                                {
                                    "lockupViewModel": {
                                        "contentId": "modern999",
                                        "metadata": {
                                            "lockupMetadataViewModel": {
                                                "title": {
                                                    "content": "Modern ViewModel Video Title"
                                                },
                                                "descriptionSnippet": {
                                                    "content": "Modern lockup video description."
                                                },
                                                "avatar": {
                                                    "decoratedAvatarViewModel": {
                                                        "avatar": {
                                                            "avatarViewModel": {
                                                                "image": {
                                                                    "sources": [
                                                                        { "url": "https://yt3.ggpht.com/modern_avatar=s68-c-k-c0x00ffffff-no-rj", "width": 68, "height": 68 }
                                                                    ]
                                                                }
                                                            }
                                                        }
                                                    }
                                                },
                                                "metadata": {
                                                    "contentMetadataViewModel": {
                                                        "metadataRows": [
                                                            {
                                                                "parts": [
                                                                    { "text": { "content": "Modern Creator" } },
                                                                    { "text": { "content": "1.2M views" } },
                                                                    { "text": { "content": "2 weeks ago" } }
                                                                ]
                                                            }
                                                        ]
                                                    }
                                                }
                                            }
                                        },
                                        "contentImage": {
                                            "collectionThumbnailViewModel": {
                                                "primaryThumbnail": {
                                                    "thumbnailViewModel": {
                                                        "image": {
                                                            "sources": [
                                                                { "url": "https://i.ytimg.com/vi/modern999/hqdefault.jpg" }
                                                            ]
                                                        },
                                                        "overlays": [
                                                            {
                                                                "thumbnailOverlayTimeStatusRenderer": {
                                                                    "text": { "simpleText": "14:22" }
                                                                }
                                                            }
                                                        ]
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            ]
                        }
                    }
                ]
            }
        }
    })");

    auto videos = YouTubeClient::extractVideosFromJson(sampleJson);
    CHECK_EQ(videos.size(), 1);
    if (!videos.empty()) {
        CHECK_EQ(videos[0].id, "modern999");
        CHECK_EQ(videos[0].title, "Modern ViewModel Video Title");
        CHECK_EQ(videos[0].author, "Modern Creator");
        CHECK_EQ(videos[0].viewCount, "1.2M views");
        CHECK_EQ(videos[0].publishedTime, "2 weeks ago");
        CHECK_EQ(videos[0].duration, "14:22");
        CHECK_EQ(videos[0].description, "Modern lockup video description.");
        CHECK_EQ(videos[0].channelAvatarUrl, "https://yt3.ggpht.com/modern_avatar=s68-c-k-c0x00ffffff-no-rj");
        CHECK_EQ(videos[0].thumbnailUrl, "https://i.ytimg.com/vi/modern999/hqdefault.jpg");
        CHECK(!videos[0].isShort);
    }

    // Verify playlist/mix/channel lockups are properly filtered out
    nlohmann::json playlistJson = nlohmann::json::parse(R"({
        "contentType": "LOCKUP_CONTENT_TYPE_PLAYLIST",
        "contentId": "PL12345"
    })");
    VideoItem playlistItem;
    CHECK(!YouTubeClient::parseVideoRenderer(playlistJson, playlistItem));

    nlohmann::json channelJson = nlohmann::json::parse(R"({
        "contentType": "LOCKUP_CONTENT_TYPE_CHANNEL",
        "contentId": "UC12345"
    })");
    VideoItem channelItem;
    CHECK(!YouTubeClient::parseVideoRenderer(channelJson, channelItem));

    std::cout << "[PASS] testJsonModernLockupViewModel\n";
}

void testJsonCommandContextLockup() {
    nlohmann::json sampleJson = nlohmann::json::parse(R"({
        "contents": {
            "richGridRenderer": {
                "contents": [
                    {
                        "richItemRenderer": {
                            "content": {
                                "lockupViewModel": {
                                    "rendererContext": {
                                        "commandContext": {
                                            "onTap": {
                                                "innertubeCommand": {
                                                    "watchEndpoint": {
                                                        "videoId": "nestedTap123"
                                                    }
                                                }
                                            }
                                        }
                                    },
                                    "metadata": {
                                        "lockupMetadataViewModel": {
                                            "title": { "content": "Nested Tap Video Title" }
                                        }
                                    }
                                }
                            }
                        }
                    }
                ]
            }
        }
    })");

    auto videos = YouTubeClient::extractVideosFromJson(sampleJson);
    CHECK_EQ(videos.size(), 1);
    if (!videos.empty()) {
        CHECK_EQ(videos[0].id, "nestedTap123");
        CHECK_EQ(videos[0].title, "Nested Tap Video Title");
    }

    std::cout << "[PASS] testJsonCommandContextLockup\n";
}

void testJsonReelItemRenderer() {
    nlohmann::json sampleJson = nlohmann::json::parse(R"({
        "contents": {
            "reelItemRenderer": {
                "videoId": "reel987",
                "headline": { "simpleText": "Short Form Video" },
                "viewCountText": { "simpleText": "300K views" }
            }
        }
    })");

    auto videos = YouTubeClient::extractVideosFromJson(sampleJson);
    CHECK_EQ(videos.size(), 1);
    if (!videos.empty()) {
        CHECK_EQ(videos[0].id, "reel987");
        CHECK_EQ(videos[0].title, "Short Form Video");
        CHECK_EQ(videos[0].viewCount, "300K views");
        CHECK(videos[0].isShort);
        CHECK_EQ(videos[0].duration, "Shorts");
    }

    std::cout << "[PASS] testJsonReelItemRenderer\n";
}

void testShortsIdAndThumbnailMapping() {
    nlohmann::json sampleShort = nlohmann::json::parse(R"({
        "contents": {
            "richGridRenderer": {
                "contents": [
                    {
                        "richItemRenderer": {
                            "content": {
                                "lockupViewModel": {
                                    "entityId": "shorts-shelf-item-af4icvDs29g",
                                    "rendererContext": {
                                        "commandContext": {
                                            "onTap": {
                                                "innertubeCommand": {
                                                    "reelWatchEndpoint": {
                                                        "videoId": "af4icvDs29g"
                                                    }
                                                }
                                            }
                                        }
                                    },
                                    "metadata": {
                                        "lockupMetadataViewModel": {
                                            "title": { "content": "Viral Short Clip" }
                                        }
                                    },
                                    "contentImage": {
                                        "thumbnailViewModel": {
                                            "image": {
                                                "sources": [
                                                    { "url": "https://i.ytimg.com/vi/af4icvDs29g/hq720.jpg?sqp=-oaymwEcCOgCEMoBSFXyq4qpAw4IARUAAIhCGAFwAcABBg==&rs=AOn4CLDq8EirK8f1jqgn0t43cFBoPlCPVg" }
                                                ]
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                ]
            }
        }
    })");

    auto videos = YouTubeClient::extractVideosFromJson(sampleShort);
    CHECK_EQ(videos.size(), 1);
    if (!videos.empty()) {
        CHECK_EQ(videos[0].id, "af4icvDs29g");
        CHECK_EQ(videos[0].title, "Viral Short Clip");
        CHECK(videos[0].isShort);
        CHECK_EQ(
            videos[0].thumbnailUrl,
            "https://i.ytimg.com/vi/af4icvDs29g/"
            "hq720.jpg?sqp=-oaymwEcCOgCEMoBSFXyq4qpAw4IARUAAIhCGAFwAcABBg==&rs=AOn4CLDq8EirK8f1jqgn0t43cFBoPlCPVg");
    }

    // Verify InnerTubeParser isShort normalization
    nlohmann::json shortJson = nlohmann::json::parse(R"({
        "videoId": "shortId1234",
        "lengthText": { "simpleText": "Shorts" }
    })");
    VideoItem parsedShort;
    CHECK(lightyt::InnerTubeParser::parseClassicVideoRenderer(shortJson, parsedShort));
    CHECK(parsedShort.isShort);

    nlohmann::json regularJson = nlohmann::json::parse(R"({
        "videoId": "regId123456",
        "lengthText": { "simpleText": "03:45" }
    })");
    VideoItem parsedRegular;
    CHECK(lightyt::InnerTubeParser::parseClassicVideoRenderer(regularJson, parsedRegular));
    CHECK(!parsedRegular.isShort);

    std::cout << "[PASS] testShortsIdAndThumbnailMapping\n";
}

void testJsonWatchNextMetadata() {
    nlohmann::json sampleWatchNext = nlohmann::json::parse(R"({
        "contents": {
            "twoColumnWatchNextResults": {
                "results": {
                    "results": {
                        "contents": [
                            {
                                "videoPrimaryInfoRenderer": {
                                    "title": {
                                        "runs": [
                                            { "text": "Building a High Performance Video Player" }
                                        ]
                                    },
                                    "viewCount": {
                                        "videoViewCountRenderer": {
                                            "viewCount": {
                                                "simpleText": "1,420,000 views"
                                            }
                                        }
                                    },
                                    "dateText": {
                                        "simpleText": "Oct 14, 2026"
                                    }
                                }
                            },
                            {
                                "videoSecondaryInfoRenderer": {
                                    "owner": {
                                        "videoOwnerRenderer": {
                                            "thumbnail": {
                                                "thumbnails": [
                                                    {
                                                        "url": "//yt3.ggpht.com/avatar_sample=s176-c-k-c0x00ffffff-no-rw-mo",
                                                        "width": 176,
                                                        "height": 176
                                                    }
                                                ]
                                            },
                                            "title": {
                                                "runs": [
                                                    { "text": "Engineering Channel" }
                                                ]
                                            },
                                            "subscriberCountText": {
                                                "simpleText": "2.4M subscribers"
                                            },
                                            "navigationEndpoint": {
                                                "browseEndpoint": {
                                                    "browseId": "UC_x5XG1OV2P6uZZ5FSM9Ttw"
                                                }
                                            },
                                            "subscribed": false
                                        }
                                    },
                                    "attributedDescription": {
                                        "content": "Deep dive into C++ and Qt video architectures."
                                    },
                                    "subscribeButton": {
                                        "subscribeButtonRenderer": {
                                            "subscribed": false
                                        }
                                    }
                                }
                            }
                        ]
                    }
                }
            }
        }
    })");

    WatchMetadata meta;
    bool ok = lightyt::InnerTubeParser::parseWatchNextMetadata(sampleWatchNext, meta);
    CHECK(ok);
    CHECK_EQ(meta.title, "Building a High Performance Video Player");
    CHECK_EQ(meta.author, "Engineering Channel");
    CHECK_EQ(meta.channelId, "UC_x5XG1OV2P6uZZ5FSM9Ttw");
    CHECK_EQ(meta.subscriberCount, "2.4M subscribers");
    CHECK_EQ(meta.viewCount, "1,420,000 views");
    CHECK_EQ(meta.publishedDate, "Oct 14, 2026");
    CHECK_EQ(meta.description, "Deep dive into C++ and Qt video architectures.");
    CHECK(!meta.isSubscribed);
    // Avatars with // prefix should have https: added, and -rw replaced by -rj
    CHECK_EQ(meta.channelAvatarUrl, "https://yt3.ggpht.com/avatar_sample=s176-c-k-c0x00ffffff-no-rj-mo");

    // Test secondary description fallback
    nlohmann::json sampleFallback = nlohmann::json::parse(R"({
        "videoSecondaryInfoRenderer": {
            "description": {
                "runs": [
                    { "text": "Line 1\n" },
                    { "text": "Line 2" }
                ]
            }
        }
    })");
    WatchMetadata metaFallback;
    CHECK(lightyt::InnerTubeParser::parseWatchNextMetadata(sampleFallback, metaFallback));
    CHECK_EQ(metaFallback.description, "Line 1\nLine 2");

    // Test depth limit: create deeply nested JSON (100 levels) to verify recursion stops cleanly
    nlohmann::json deepJson = nlohmann::json::object();
    nlohmann::json *curr = &deepJson;
    for (int i = 0; i < 100; ++i) {
        (*curr)["nested"] = nlohmann::json::object();
        curr = &((*curr)["nested"]);
    }
    (*curr)["videoPrimaryInfoRenderer"] = {{"title", {{"runs", {{{"text", "Deep Title"}}}}}}};
    WatchMetadata deepMeta;
    // Should safely finish without stack overflow
    (void)lightyt::InnerTubeParser::parseWatchNextMetadata(deepJson, deepMeta);
    // Because depth > 64, title at depth 100 is not reached
    CHECK(deepMeta.title.empty());

    std::cout << "[PASS] testJsonWatchNextMetadata\n";
}

void testLikeCountParsing() {
    using lightyt::InnerTubeParser;
    CHECK_EQ(InnerTubeParser::parseLikeCountString(""), 0);
    CHECK_EQ(InnerTubeParser::parseLikeCountString("Like"), 0);
    CHECK_EQ(InnerTubeParser::parseLikeCountString("No likes"), 0);
    CHECK_EQ(InnerTubeParser::parseLikeCountString("0"), 0);
    CHECK_EQ(InnerTubeParser::parseLikeCountString("42"), 42);
    CHECK_EQ(InnerTubeParser::parseLikeCountString("123"), 123);
    CHECK_EQ(InnerTubeParser::parseLikeCountString("123K"), 123000);
    CHECK_EQ(InnerTubeParser::parseLikeCountString("123k"), 123000);
    CHECK_EQ(InnerTubeParser::parseLikeCountString("1.2M"), 1200000);
    CHECK_EQ(InnerTubeParser::parseLikeCountString("2.5B"), 2500000000);
    CHECK_EQ(InnerTubeParser::parseLikeCountString("123,456"), 123456);
    CHECK_EQ(InnerTubeParser::parseLikeCountString("123,456 likes"), 123456);
    CHECK_EQ(InnerTubeParser::parseLikeCountString("123,456 people liked this video"), 123456);
    CHECK_EQ(InnerTubeParser::parseLikeCountString("like this video along with 1,234 other people"), 1235);
    CHECK_EQ(InnerTubeParser::parseLikeCountString("along with 50 other people"), 51);
    CHECK_EQ(InnerTubeParser::parseLikeCountString("along with 50 others"), 51);
    CHECK_EQ(InnerTubeParser::parseLikeCountString("You and 99 other people liked this"), 100);
    CHECK_EQ(InnerTubeParser::parseLikeCountString("12.5K likes"), 12500);

    std::cout << "[PASS] testLikeCountParsing\n";
}

void testWatchNextMetadataLikes() {
    // 1. Classic segmentedLikeDislikeButtonRenderer with toggleButtonRenderer
    nlohmann::json classicJson = nlohmann::json::parse(R"({
        "contents": {
            "twoColumnWatchNextResults": {
                "results": {
                    "results": {
                        "contents": [
                            {
                                "videoPrimaryInfoRenderer": {
                                    "title": { "runs": [{ "text": "Classic Likes Test" }] },
                                    "videoActions": {
                                        "menuRenderer": {
                                            "topLevelButtons": [
                                                {
                                                    "segmentedLikeDislikeButtonRenderer": {
                                                        "likeButton": {
                                                            "toggleButtonRenderer": {
                                                                "isToggled": true,
                                                                "likeStatus": "LIKE",
                                                                "defaultText": {
                                                                    "accessibility": {
                                                                        "accessibilityData": {
                                                                            "label": "45,678 likes"
                                                                        }
                                                                    },
                                                                    "simpleText": "45K"
                                                                }
                                                            }
                                                        },
                                                        "dislikeButton": {
                                                            "toggleButtonRenderer": {
                                                                "isToggled": false,
                                                                "likeStatus": "INDIFFERENT"
                                                            }
                                                        }
                                                    }
                                                }
                                            ]
                                        }
                                    }
                                }
                            }
                        ]
                    }
                }
            }
        }
    })");

    WatchMetadata metaClassic;
    CHECK(lightyt::InnerTubeParser::parseWatchNextMetadata(classicJson, metaClassic));
    CHECK(metaClassic.isLiked);
    CHECK(!metaClassic.isDisliked);
    CHECK_EQ(metaClassic.likeCount, 45678);

    // 2. Modern segmentedLikeDislikeButtonViewModel
    nlohmann::json modernJson = nlohmann::json::parse(R"({
        "contents": {
            "segmentedLikeDislikeButtonViewModel": {
                "likeButtonViewModel": {
                    "likeButtonViewModel": {
                        "toggleButtonViewModel": {
                            "toggleButtonViewModel": {
                                "defaultButtonViewModel": {
                                    "buttonViewModel": {
                                        "title": "1.5M",
                                        "accessibilityText": "1,520,000 people liked this video"
                                    }
                                },
                                "isToggled": false
                            }
                        },
                        "likeStatusEntity": {
                            "likeStatus": "DISLIKE"
                        }
                    }
                },
                "dislikeButtonViewModel": {
                    "dislikeButtonViewModel": {
                        "toggleButtonViewModel": {
                            "toggleButtonViewModel": {
                                "isToggled": true
                            }
                        },
                        "dislikeStatusEntity": {
                            "likeStatus": "DISLIKE"
                        }
                    }
                }
            }
        }
    })");

    WatchMetadata metaModern;
    CHECK(lightyt::InnerTubeParser::parseWatchNextMetadata(modernJson, metaModern));
    CHECK(!metaModern.isLiked);
    CHECK(metaModern.isDisliked);
    CHECK_EQ(metaModern.likeCount, 1520000);

    // 3. Direct likeCount integer fallback
    nlohmann::json directJson = nlohmann::json::parse(R"({
        "videoPrimaryInfoRenderer": {
            "title": { "runs": [{ "text": "Direct Likes Test" }] }
        },
        "likeButtonRenderer": {
            "likeStatus": "LIKE",
            "likeCount": 8842
        }
    })");

    WatchMetadata metaDirect;
    CHECK(lightyt::InnerTubeParser::parseWatchNextMetadata(directJson, metaDirect));
    CHECK(metaDirect.isLiked);
    CHECK(!metaDirect.isDisliked);
    CHECK_EQ(metaDirect.likeCount, 8842);

    std::cout << "[PASS] testWatchNextMetadataLikes\n";
}

void testCookieManagerFallbackSapisid3P() {
    CookieManager mgr;
    std::string sample = "__Secure-3PAPISID=Only3PSapisidVal123; SID=SampleSid456";

    bool ok = mgr.loadFromString(sample);
    CHECK(ok);
    CHECK(mgr.hasAuth());

    std::string auth = mgr.getSapisidHash("https://www.youtube.com");
    CHECK(auth.find("SAPISIDHASH ") != std::string::npos);

    std::cout << "[PASS] testCookieManagerFallbackSapisid3P\n";
}

#include "core/Process.h"

void testStreamResolver() {
    // Quality format string checks with allowAv1=false (default)
    std::string fmt1080NoAv1 = StreamResolver::qualityToFormatString("1080p", false);
    CHECK(fmt1080NoAv1.find("1080") != std::string::npos);
    CHECK(fmt1080NoAv1.find("[vcodec!^=av01]") != std::string::npos);

    std::string fmt720NoAv1 = StreamResolver::qualityToFormatString("720p", false);
    CHECK(fmt720NoAv1.find("720") != std::string::npos);
    CHECK(fmt720NoAv1.find("[vcodec!^=av01]") != std::string::npos);

    std::string fmt480NoAv1 = StreamResolver::qualityToFormatString("480p", false);
    CHECK(fmt480NoAv1.find("480") != std::string::npos);
    CHECK(fmt480NoAv1.find("[vcodec!^=av01]") != std::string::npos);

    std::string fmt360NoAv1 = StreamResolver::qualityToFormatString("360p", false);
    CHECK(fmt360NoAv1.find("360") != std::string::npos);
    CHECK(fmt360NoAv1.find("[vcodec!^=av01]") != std::string::npos);

    CHECK_EQ(StreamResolver::qualityToFormatString("Auto", false), "bestvideo[vcodec!^=av01]+bestaudio/best");
    CHECK_EQ(StreamResolver::qualityToFormatString("Audio Only", false), "bestaudio/best");

    // Quality format string checks with allowAv1=true
    std::string fmt1080Av1 = StreamResolver::qualityToFormatString("1080p", true);
    CHECK(fmt1080Av1.find("1080") != std::string::npos);
    CHECK(fmt1080Av1.find("[vcodec!^=av01]") == std::string::npos);

    std::string fmt720Av1 = StreamResolver::qualityToFormatString("720p", true);
    CHECK(fmt720Av1.find("720") != std::string::npos);
    CHECK(fmt720Av1.find("[vcodec!^=av01]") == std::string::npos);

    CHECK_EQ(StreamResolver::qualityToFormatString("Auto", true), "bestvideo+bestaudio/best");
    CHECK_EQ(StreamResolver::qualityToFormatString("Audio Only", true), "bestaudio/best");

    // Test isAuthRequiredError
    CHECK(StreamResolver::isAuthRequiredError("ERROR: [youtube] dQw4w9WgXcQ: Sign in to confirm your age"));
    CHECK(StreamResolver::isAuthRequiredError("ERROR: [youtube] Private video. Sign in if you've been granted access"));
    CHECK(StreamResolver::isAuthRequiredError("ERROR: This video requires login"));
    CHECK(StreamResolver::isAuthRequiredError("ERROR: [youtube] 43lwWKIm7DQ: Sign in to confirm you’re not a bot. Use "
                                              "--cookies-from-browser or --cookies for the authentication."));
    CHECK(StreamResolver::isAuthRequiredError("ERROR: [youtube] Sign in to confirm you're not a bot."));
    CHECK(!StreamResolver::isAuthRequiredError("ERROR: [youtube] dQw4w9WgXcQ: Video unavailable"));
    CHECK(!StreamResolver::isAuthRequiredError("ERROR: connection timed out"));

    // Test buildYtDlpArgs anonymous fast-tier (no cookies, player_skip)
    auto argsAnon =
        StreamResolver::buildYtDlpArgs("yt-dlp", "https://www.youtube.com/watch?v=dQw4w9WgXcQ", "best", false, "",
                                       "youtube:player_skip=configs,webpage;player_client=default,ios");

    // Verify that buildYtDlpArgs does NOT override yt-dlp's internal per-client user-agent
    bool foundUa = false;
    bool foundCookies = false;
    bool foundExtractorArgs = false;
    bool foundGetUrl = false;
    for (size_t i = 0; i < argsAnon.size(); ++i) {
        if (argsAnon[i] == "--user-agent") {
            foundUa = true;
        }
        if (argsAnon[i] == "--cookies") {
            foundCookies = true;
        }
        if (argsAnon[i] == "-g") {
            foundGetUrl = true;
        }
        if (argsAnon[i] == "--extractor-args" && i + 1 < argsAnon.size() &&
            argsAnon[i + 1] == "youtube:player_skip=configs,webpage;player_client=default,ios") {
            foundExtractorArgs = true;
        }
    }
    CHECK(!foundUa);
    CHECK(!foundCookies);
    CHECK(foundGetUrl);
    CHECK(foundExtractorArgs);
    for (const auto &arg : argsAnon) {
        CHECK(arg != "--no-check-certificates");
    }

    // Test buildYtDlpArgs authenticated tier
    auto argsAuth = StreamResolver::buildYtDlpArgs("yt-dlp", "https://www.youtube.com/watch?v=dQw4w9WgXcQ", "best",
                                                   true, "/path/to/cookies.txt", "youtube:player_client=default,web");

    bool foundAuthCookies = false;
    bool foundAuthGetUrl = false;
    for (size_t i = 0; i < argsAuth.size(); ++i) {
        if (argsAuth[i] == "--cookies" && i + 1 < argsAuth.size() && argsAuth[i + 1] == "/path/to/cookies.txt") {
            foundAuthCookies = true;
        }
        if (argsAuth[i] == "-g") {
            foundAuthGetUrl = true;
        }
    }
    CHECK(foundAuthCookies);
    CHECK(foundAuthGetUrl);
    for (const auto &arg : argsAuth) {
        CHECK(arg != "--no-check-certificates");
    }

    // Test qualityToFormatSort and formatSort argument in buildYtDlpArgs
    CHECK(StreamResolver::qualityToFormatSort("1080p") == "res:1080,vcodec");
    CHECK(StreamResolver::qualityToFormatSort("720p") == "res:720,vcodec");
    CHECK(StreamResolver::qualityToFormatSort("480p") == "res:480,vcodec");
    CHECK(StreamResolver::qualityToFormatSort("360p") == "res:360,vcodec");
    CHECK(StreamResolver::qualityToFormatSort("Auto") == "vcodec");
    CHECK(StreamResolver::qualityToFormatSort("Audio Only").empty());

    auto argsSorted =
        StreamResolver::buildYtDlpArgs("yt-dlp", "https://www.youtube.com/watch?v=dQw4w9WgXcQ", "best", false, "",
                                       "youtube:player_skip=configs;player_client=default,-visionos", "res:1080");
    bool foundFormatSort = false;
    for (size_t i = 0; i < argsSorted.size(); ++i) {
        if (argsSorted[i] == "-S" && i + 1 < argsSorted.size() && argsSorted[i + 1] == "res:1080") {
            foundFormatSort = true;
            break;
        }
    }
    CHECK(foundFormatSort);

    CHECK(argsAnon.size() >= 2);
    CHECK(argsAnon[argsAnon.size() - 2] == "--");
    CHECK(argsAnon.back() == "https://www.youtube.com/watch?v=dQw4w9WgXcQ");

    CHECK(argsAuth.size() >= 2);
    CHECK(argsAuth[argsAuth.size() - 2] == "--");
    CHECK(argsAuth.back() == "https://www.youtube.com/watch?v=dQw4w9WgXcQ");

    // Test extractVideoId normalization
    CHECK_EQ(StreamResolver::extractVideoId("dQw4w9WgXcQ"), "dQw4w9WgXcQ");
    CHECK_EQ(StreamResolver::extractVideoId("https://www.youtube.com/watch?v=dQw4w9WgXcQ"), "dQw4w9WgXcQ");
    CHECK_EQ(StreamResolver::extractVideoId("https://www.youtube.com/watch?v=dQw4w9WgXcQ&t=42s&list=xyz"),
             "dQw4w9WgXcQ");
    CHECK_EQ(StreamResolver::extractVideoId("https://youtu.be/dQw4w9WgXcQ"), "dQw4w9WgXcQ");
    CHECK_EQ(StreamResolver::extractVideoId("https://youtu.be/dQw4w9WgXcQ?t=100"), "dQw4w9WgXcQ");
    CHECK_EQ(StreamResolver::extractVideoId("https://www.youtube.com/shorts/dQw4w9WgXcQ"), "dQw4w9WgXcQ");
    CHECK_EQ(StreamResolver::extractVideoId("shorts-shelf-item-dQw4w9WgXcQ"), "dQw4w9WgXcQ");
    CHECK_EQ(StreamResolver::extractVideoId("shorts-item-dQw4w9WgXcQ"), "dQw4w9WgXcQ");
    CHECK_EQ(StreamResolver::extractVideoId("shorts-dQw4w9WgXcQ"), "dQw4w9WgXcQ");
    CHECK_EQ(StreamResolver::extractVideoId("https://www.youtube.com/embed/dQw4w9WgXcQ"), "dQw4w9WgXcQ");
    CHECK_EQ(StreamResolver::extractVideoId("https://www.youtube.com/v/dQw4w9WgXcQ"), "dQw4w9WgXcQ");
    CHECK_EQ(StreamResolver::extractVideoId("https://www.youtube.com/watch?v=dQw4w9WgXcQ#t=10s"), "dQw4w9WgXcQ");
    CHECK_EQ(StreamResolver::extractVideoId("?v=dQw4w9WgXcQ"), "dQw4w9WgXcQ");
    CHECK_EQ(StreamResolver::extractVideoId("&v=dQw4w9WgXcQ"), "dQw4w9WgXcQ");
    CHECK_EQ(StreamResolver::extractVideoId("https://youtu.be/dQw4w9WgXcQ/extra"), "dQw4w9WgXcQ");
    CHECK_EQ(StreamResolver::extractVideoId("-abcdefghij"), "-abcdefghij");

    // Verify yt-dlp discovery checks and prioritizes lib/ directory
    auto exeDir = lightyt::paths::getExecutableDir();
    auto libDir = exeDir.parent_path() / "lib";
    bool libHasYtDlp = false;
    std::error_code ec;
    if (std::filesystem::exists(libDir / "yt-dlp", ec) || std::filesystem::exists(libDir / "yt-dlp.exe", ec) ||
        std::filesystem::exists(std::filesystem::path("lib") / "yt-dlp", ec) ||
        std::filesystem::exists(std::filesystem::path("lib") / "yt-dlp.exe", ec)) {
        libHasYtDlp = true;
    }

    std::string foundYtDlp = StreamResolver::findYtDlp();
    if (libHasYtDlp) {
        // When lib/ contains yt-dlp, findYtDlp must find it and prioritize lib/
        CHECK(!foundYtDlp.empty());
        CHECK(foundYtDlp.find("lib") != std::string::npos);
        std::cout << "[INFO] Verified yt-dlp resolved from lib/ directory: " << foundYtDlp << "\n";
    } else if (!foundYtDlp.empty()) {
        CHECK(foundYtDlp.find("yt-dlp") != std::string::npos);
        std::cout << "[INFO] Verified yt-dlp resolved from system path: " << foundYtDlp << "\n";
    } else {
        std::cout << "[WARN] yt-dlp not found in lib/ or system PATH\n";
    }

    std::cout << "[PASS] testStreamResolver\n";
}

void testStreamResolverExpireParsing() {
    auto nowSteady = std::chrono::steady_clock::now();
    int64_t nowSec =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    // 1. Valid future expiration timestamp: 7200 seconds (2 hours) from now
    // Safety margin is 600s, so remaining duration should be around 7200 - 600 = 6600s
    int64_t futureExpire = nowSec + 7200;
    std::string urlWithExpire =
        "https://rr1---sn-4g5ednks.googlevideo.com/videoplayback?expire=" + std::to_string(futureExpire) +
        "&ei=abc123xyz";
    auto exp1 = StreamResolver::parseUrlExpiry(urlWithExpire);
    auto diff1 = std::chrono::duration_cast<std::chrono::seconds>(exp1 - nowSteady).count();
    CHECK(diff1 >= 6590 && diff1 <= 6610);

    // 2. Fallback on missing expire param (should fallback to 2 hours = 7200s)
    std::string urlNoExpire = "https://rr1---sn-4g5ednks.googlevideo.com/videoplayback?sparams=expire&ei=abc";
    auto exp2 = StreamResolver::parseUrlExpiry(urlNoExpire);
    auto diff2 = std::chrono::duration_cast<std::chrono::seconds>(exp2 - nowSteady).count();
    CHECK(diff2 >= 7190 && diff2 <= 7210);

    // 3. Fallback on malformed expire param
    std::string urlMalformed = "https://rr1---sn-4g5ednks.googlevideo.com/videoplayback?expire=notanumber&ei=abc";
    auto exp3 = StreamResolver::parseUrlExpiry(urlMalformed);
    auto diff3 = std::chrono::duration_cast<std::chrono::seconds>(exp3 - nowSteady).count();
    CHECK(diff3 >= 7190 && diff3 <= 7210);

    std::cout << "[PASS] testStreamResolverExpireParsing\n";
}

void testCookieDomainAndExpiry() {
    CookieManager mgr;
    std::string sample = ".youtube.com\tTRUE\t/\tTRUE\t2147483647\tYT_COOKIE\tvalid123\n"
                         ".example.com\tTRUE\t/\tTRUE\t2147483647\tOTHER_COOKIE\tunwanted456\n"
                         ".google.com\tTRUE\t/\tTRUE\t1000\tEXPIRED_GOOGLE_AUTH\tauth789\n"
                         ".google.com\tTRUE\t/\tTRUE\t2147483647\tVALID_GOOGLE_AUTH\tauth999\n";

    bool ok = mgr.loadFromString(sample);
    CHECK(ok);

    std::string header = mgr.getCookieHeaderValue("www.youtube.com");
    CHECK(header.find("YT_COOKIE=valid123") != std::string::npos);
    CHECK(header.find("VALID_GOOGLE_AUTH=auth999") != std::string::npos);
    CHECK(header.find("EXPIRED_GOOGLE_AUTH") == std::string::npos);
    CHECK(header.find("OTHER_COOKIE") == std::string::npos);

    // Direct domainMatches checks
    CHECK(CookieManager::domainMatches(".youtube.com", "www.youtube.com"));
    CHECK(CookieManager::domainMatches("youtube.com", "youtube.com"));
    CHECK(CookieManager::domainMatches(".google.com", "www.youtube.com"));
    CHECK(!CookieManager::domainMatches(".example.com", "www.youtube.com"));
    CHECK(!CookieManager::domainMatches("attacker-youtube.com", "youtube.com"));
    CHECK(!CookieManager::domainMatches("", "www.youtube.com"));
    CHECK(!CookieManager::domainMatches(".youtube.com", ""));

    std::cout << "[PASS] testCookieDomainAndExpiry\n";
}

void testCookieManagerUpdateFromSetCookieAndResponse() {
    CookieManager mgr;
    mgr.updateFromSetCookie(
        "SIDCC=fresh_sidcc_123; Domain=.youtube.com; Path=/; Expires=Wed, 21 Oct 2026 07:28:00 GMT; Secure; HttpOnly");
    CHECK_EQ(mgr.getCookie("SIDCC"), "fresh_sidcc_123");

    HttpResponse resp{
        .statusCode = 200,
        .body = "{}",
        .error = "",
        .setCookies = {
            "__Secure-1PSIDCC=fresh_1psidcc_456; Domain=.youtube.com; Path=/; Max-Age=3600; Secure; HttpOnly",
            "LOGIN_INFO=new_login_info; Domain=.youtube.com; Path=/"}};
    mgr.updateFromResponse(resp);
    CHECK_EQ(mgr.getCookie("__Secure-1PSIDCC"), "fresh_1psidcc_456");
    CHECK_EQ(mgr.getCookie("LOGIN_INFO"), "new_login_info");

    std::string header = mgr.getCookieHeaderValue("www.youtube.com");
    CHECK(header.find("SIDCC=fresh_sidcc_123") != std::string::npos);
    CHECK(header.find("__Secure-1PSIDCC=fresh_1psidcc_456") != std::string::npos);
    CHECK(header.find("LOGIN_INFO=new_login_info") != std::string::npos);

    std::cout << "[PASS] testCookieManagerUpdateFromSetCookieAndResponse\n";
}

void testCookieManagerResilientExpiry() {
    CookieManager mgr;
    // SAPISID is valid into the far future; SIDCC is expired in the past (1000); OTHER_EXPIRED is also expired.
    std::string sample = ".youtube.com\tTRUE\t/\tTRUE\t2147483647\tSAPISID\tValidSapisid\n"
                         ".youtube.com\tTRUE\t/\tTRUE\t1000\tSIDCC\tExpiredCompanionSidcc\n"
                         ".youtube.com\tTRUE\t/\tTRUE\t1000\tOTHER_EXPIRED\tOldValue\n";

    bool ok = mgr.loadFromString(sample);
    CHECK(ok);
    CHECK(mgr.hasAuth());

    // Resilient expiry: companion session cookie SIDCC remains active because master auth (SAPISID) is valid
    CHECK_EQ(mgr.getCookie("SIDCC"), "ExpiredCompanionSidcc");
    // Non-companion expired cookie is dropped
    CHECK(mgr.getCookie("OTHER_EXPIRED").empty());

    std::string header = mgr.getCookieHeaderValue("www.youtube.com");
    CHECK(header.find("SAPISID=ValidSapisid") != std::string::npos);
    CHECK(header.find("SIDCC=ExpiredCompanionSidcc") != std::string::npos);
    CHECK(header.find("OTHER_EXPIRED") == std::string::npos);

    // If master auth itself is expired or absent, companion cookie should also be treated as expired
    CookieManager mgrExpiredAuth;
    std::string sampleExpiredAuth = ".youtube.com\tTRUE\t/\tTRUE\t1000\tSAPISID\tExpiredSapisid\n"
                                    ".youtube.com\tTRUE\t/\tTRUE\t1000\tSIDCC\tExpiredCompanionSidcc\n";
    CHECK(mgrExpiredAuth.loadFromString(sampleExpiredAuth));
    CHECK(!mgrExpiredAuth.hasAuth());
    CHECK(mgrExpiredAuth.getCookie("SIDCC").empty());

    std::cout << "[PASS] testCookieManagerResilientExpiry\n";
}

void testHttpClientBasics() {
    CHECK(std::string_view(DEFAULT_USER_AGENT).find("Mozilla/5.0") != std::string_view::npos);
    CHECK(std::string_view(DEFAULT_USER_AGENT).find("Chrome/131") != std::string_view::npos);

    HttpResponse okResp{.statusCode = 200, .body = "ok", .error = "", .setCookies = {}};
    CHECK(okResp.isOk());
    CHECK(okResp.setCookies.empty());

    HttpResponse respWithCookies{.statusCode = 200,
                                 .body = "ok",
                                 .error = "",
                                 .setCookies = {"SIDCC=NewSidccToken123; Domain=.youtube.com; Path=/; Secure"}};
    CHECK(respWithCookies.isOk());
    CHECK_EQ(respWithCookies.setCookies.size(), 1);
    CHECK_EQ(respWithCookies.setCookies[0], "SIDCC=NewSidccToken123; Domain=.youtube.com; Path=/; Secure");

    HttpResponse errResp{.statusCode = 404, .body = "not found", .error = "", .setCookies = {}};
    CHECK(!errResp.isOk());

    HttpResponse netErrResp{.statusCode = 200, .body = "", .error = "network error", .setCookies = {}};
    CHECK(!netErrResp.isOk());

    std::atomic<bool> cancelToken{true};
    auto cancelledResp = HttpClient::get("https://www.youtube.com", {}, 15, &cancelToken);
    CHECK(!cancelledResp.isOk());
    CHECK(cancelledResp.error.find("cancelled") != std::string::npos ||
          cancelledResp.error.find("aborted") != std::string::npos ||
          cancelledResp.error.find("Callback") != std::string::npos);

    std::cout << "[PASS] testHttpClientBasics\n";
}

void testCurlApiDynamicLoading() {
    auto &api = lightyt::getCurlApi();
    CHECK(api.load());
    CHECK(api.isLoaded());
    CHECK(api.easy_init != nullptr);
    CHECK(api.easy_setopt != nullptr);
    CHECK(api.easy_perform != nullptr);
    CHECK(api.easy_cleanup != nullptr);
    CHECK(api.easy_strerror != nullptr);
    CHECK(api.easy_getinfo != nullptr);
    CHECK(api.slist_append != nullptr);
    CHECK(api.slist_free_all != nullptr);

    CURL *handle = api.easy_init();
    CHECK(handle != nullptr);
    api.easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    api.easy_cleanup(handle);

    std::cout << "[PASS] testCurlApiDynamicLoading\n";
}

void testProcessCommandLineQuoting() {
    CHECK_EQ(lightyt::Process::buildCommandLine({"app", "arg1", "arg2"}), "app arg1 arg2");
    CHECK_EQ(lightyt::Process::buildCommandLine({"app", "hello world"}), "app \"hello world\"");
    CHECK_EQ(lightyt::Process::buildCommandLine({"app", ""}), "app \"\"");
    CHECK_EQ(lightyt::Process::buildCommandLine({"app", "tab\there"}), "app \"tab\there\"");
    CHECK_EQ(lightyt::Process::buildCommandLine({"app", "a\"b"}), "app \"a\\\"b\"");
    CHECK_EQ(lightyt::Process::buildCommandLine({"app", "a\\\"b"}), "app \"a\\\\\\\"b\"");
    CHECK_EQ(lightyt::Process::buildCommandLine({"app", "C:\\Program Files\\"}), "app \"C:\\Program Files\\\\\"");
    CHECK_EQ(lightyt::Process::buildCommandLine({"app", "C:\\Program Files\\\\"}), "app \"C:\\Program Files\\\\\\\\\"");
    CHECK_EQ(lightyt::Process::buildCommandLine({"app", "C:\\Windows\\System32"}), "app C:\\Windows\\System32");

    std::cout << "[PASS] testProcessCommandLineQuoting\n";
}

void testProcessExecution() {
#ifdef _WIN32
    auto res = lightyt::Process::run({"cmd.exe", "/c", "echo hello_lightyt"});
#else
    auto res = lightyt::Process::run({"echo", "hello_lightyt"});
#endif
    CHECK(res.success());
    CHECK(res.stdOut.find("hello_lightyt") != std::string::npos);

#ifdef _WIN32
    auto resErr = lightyt::Process::run({"cmd.exe", "/c", "echo err_msg 1>&2 & exit 3"});
#else
    auto resErr = lightyt::Process::run({"sh", "-c", "echo err_msg >&2; exit 3"});
#endif
    CHECK(!resErr.success());
    CHECK_EQ(resErr.exitCode, 3);
    CHECK(resErr.stdErr.find("err_msg") != std::string::npos);

#ifndef _WIN32
    // Test grandchild pipe inheritance: direct child exits immediately, background grandchild stays alive
    auto resGrandchild = lightyt::Process::run({"sh", "-c", "echo child_done; (sleep 5 >/dev/null 2>&1 &)"},
                                               std::chrono::milliseconds(2000));
    CHECK(!resGrandchild.timedOut);
    CHECK(resGrandchild.success());
    CHECK(resGrandchild.stdOut.find("child_done") != std::string::npos);
#endif

    std::cout << "[PASS] testProcessExecution\n";
}

void testProcessLargeOutput() {
#ifdef _WIN32
    auto res = lightyt::Process::run({"powershell", "-NoProfile", "-Command", "[string]::new('A', 80000)"});
#else
    auto res = lightyt::Process::run({"seq", "1", "20000"});
#endif
    CHECK(res.success());
    CHECK(res.stdOut.size() > 65536);

    std::cout << "[PASS] testProcessLargeOutput\n";
}

void testProcessTimeoutAndCancellation() {
#ifdef _WIN32
    auto resTimeout = lightyt::Process::run({"powershell", "-NoProfile", "-Command", "Start-Sleep -Seconds 5"},
                                            std::chrono::milliseconds(150));
#else
    auto resTimeout = lightyt::Process::run({"sleep", "5"}, std::chrono::milliseconds(150));
#endif
    CHECK(resTimeout.timedOut);
    CHECK(!resTimeout.success());

    std::atomic<bool> cancelToken{false};
    std::thread cancelThread([&cancelToken]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        cancelToken.store(true);
    });

#ifdef _WIN32
    auto resCancel = lightyt::Process::run({"powershell", "-NoProfile", "-Command", "Start-Sleep -Seconds 5"},
                                           std::chrono::milliseconds(5000), &cancelToken);
#else
    auto resCancel = lightyt::Process::run({"sleep", "5"}, std::chrono::milliseconds(5000), &cancelToken);
#endif
    cancelThread.join();

    CHECK(resCancel.cancelled);
    CHECK(!resCancel.success());

    std::cout << "[PASS] testProcessTimeoutAndCancellation\n";
}

void testThumbnailCache() {
    std::string url = "https://i.ytimg.com/vi/test_id/hqdefault.jpg";
    std::string hash = ThumbnailCache::hashUrl(url);
    CHECK(!hash.empty());
    CHECK_EQ(hash.length(), 16);

    std::vector<uint8_t> dummyData = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46};
    bool written = ThumbnailCache::instance().writeDisk(url, dummyData);
    CHECK(written);

    CHECK(ThumbnailCache::instance().hasDisk(url));

    std::vector<uint8_t> readData;
    bool readOk = ThumbnailCache::instance().readDisk(url, readData);
    CHECK(readOk);
    CHECK_EQ(readData.size(), dummyData.size());
    CHECK(readData == dummyData);

    // Verify readDisk touches last_write_time
    auto diskPath = ThumbnailCache::instance().getCacheDir() / (hash + ".jpg");
    std::error_code ec;
    auto pastTime = std::filesystem::file_time_type::clock::now() - std::chrono::hours(100);
    std::filesystem::last_write_time(diskPath, pastTime, ec);
    CHECK(!ec);

    // Reading should touch mtime to now
    readOk = ThumbnailCache::instance().readDisk(url, readData);
    CHECK(readOk);
    auto touchedTime = std::filesystem::last_write_time(diskPath, ec);
    CHECK(!ec);
    CHECK(touchedTime > pastTime);

    // Verify readDisk throttles last_write_time write amplification when read again immediately
    auto mtimeBeforeSecondRead = std::filesystem::last_write_time(diskPath, ec);
    CHECK(!ec);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    readOk = ThumbnailCache::instance().readDisk(url, readData);
    CHECK(readOk);
    auto mtimeAfterSecondRead = std::filesystem::last_write_time(diskPath, ec);
    CHECK(!ec);
    CHECK(mtimeBeforeSecondRead == mtimeAfterSecondRead);

    // Test pruning: url is 1000 bytes and old; url2 is 300 bytes and new
    std::string urlOld = "https://i.ytimg.com/vi/test_id_old/hqdefault.jpg";
    std::string hashOld = ThumbnailCache::hashUrl(urlOld);
    std::vector<uint8_t> oldData(1000, 0x11);
    CHECK(ThumbnailCache::instance().writeDisk(urlOld, oldData));

    auto oldDiskPath = ThumbnailCache::instance().getCacheDir() / (hashOld + ".jpg");
    std::filesystem::last_write_time(oldDiskPath, pastTime, ec);
    CHECK(!ec);

    std::string urlNew = "https://i.ytimg.com/vi/test_id_new/hqdefault.jpg";
    std::vector<uint8_t> newData(300, 0x22);
    CHECK(ThumbnailCache::instance().writeDisk(urlNew, newData));

    // Prune with maxBytes = 500. Total size is ~1300+ bytes.
    // Old file (1000 bytes) should be pruned, while new file (300 bytes <= 500) should remain.
    ThumbnailCache::instance().prune(500, 30);
    CHECK(!ThumbnailCache::instance().hasDisk(urlOld));
    CHECK(ThumbnailCache::instance().hasDisk(urlNew));

    bool removedNew = ThumbnailCache::instance().removeDisk(urlNew);
    CHECK(removedNew);
    CHECK(!ThumbnailCache::instance().hasDisk(urlNew));

    // Verify reading non-existent file returns false without seekg
    std::vector<uint8_t> missingData;
    CHECK(!ThumbnailCache::instance().readDisk("https://i.ytimg.com/vi/non_existent_file/hqdefault.jpg", missingData));

    // Test periodic background pruning trigger (50+ writes)
    for (int i = 0; i < 55; ++i) {
        std::string batchUrl = "https://i.ytimg.com/vi/batch_" + std::to_string(i) + "/hqdefault.jpg";
        std::vector<uint8_t> batchData = {0x01, 0x02, 0x03};
        CHECK(ThumbnailCache::instance().writeDisk(batchUrl, batchData));
    }
    // Clean up batch items
    for (int i = 0; i < 55; ++i) {
        std::string batchUrl = "https://i.ytimg.com/vi/batch_" + std::to_string(i) + "/hqdefault.jpg";
        ThumbnailCache::instance().removeDisk(batchUrl);
    }

    std::cout << "[PASS] testThumbnailCache\n";
}

void testContinuationTokenParsing() {
    nlohmann::json sampleContinuation = nlohmann::json::parse(R"({
        "contents": {
            "twoColumnBrowseResultsRenderer": {
                "tabs": [
                    {
                        "tabRenderer": {
                            "content": {
                                "richGridRenderer": {
                                    "contents": [
                                        {
                                            "continuationItemRenderer": {
                                                "trigger": "CONTINUATION_TRIGGER_ON_ITEM_SHOWN",
                                                "continuationEndpoint": {
                                                    "continuationCommand": {
                                                        "token": "4qmF-B9sample_token_xyz"
                                                    }
                                                }
                                            }
                                        }
                                    ]
                                }
                            }
                        }
                    }
                ]
            }
        }
    })");

    std::string token = YouTubeClient::extractContinuationToken(sampleContinuation);
    CHECK_EQ(token, "4qmF-B9sample_token_xyz");

    std::cout << "[PASS] testContinuationTokenParsing\n";
}

void testExtractAccountChannels() {
    nlohmann::json sampleAccountMenu = nlohmann::json::parse(R"({
        "actions": [
            {
                "openPopupAction": {
                    "popup": {
                        "multiPageMenuRenderer": {
                            "header": {
                                "activeAccountHeaderRenderer": {
                                    "accountName": {
                                        "simpleText": "Primary Channel"
                                    },
                                    "channelHandle": {
                                        "simpleText": "@primary"
                                    },
                                    "channelId": "UCprimary123",
                                    "email": {
                                        "simpleText": "user@example.com"
                                    },
                                    "accountPhoto": {
                                        "thumbnails": [
                                            { "url": "https://yt3.ggpht.com/avatar_primary.jpg" }
                                        ]
                                    }
                                }
                            },
                            "sections": [
                                {
                                    "multiPageMenuSectionRenderer": {
                                        "items": [
                                            {
                                                "accountItemRenderer": {
                                                    "accountName": {
                                                        "simpleText": "Brand Channel"
                                                    },
                                                    "channelHandle": {
                                                        "simpleText": "@brand"
                                                    },
                                                    "channelId": "UCbrand456",
                                                    "accountPhoto": {
                                                        "thumbnails": [
                                                            { "url": "https://yt3.ggpht.com/avatar_brand.jpg" }
                                                        ]
                                                    },
                                                    "isSelected": false,
                                                    "serviceEndpoint": {
                                                        "selectActiveIdentityEndpoint": {
                                                            "supportedTokens": [
                                                                {
                                                                    "pageIdToken": {
                                                                        "pageId": "109283746501928374650"
                                                                    }
                                                                }
                                                            ]
                                                        }
                                                    }
                                                }
                                            },
                                            {
                                                "accountItem": {
                                                    "accountName": {
                                                        "runs": [ { "text": "Slot 1 Channel" } ]
                                                    },
                                                    "channelHandle": {
                                                        "runs": [ { "text": "@slot1" } ]
                                                    },
                                                    "isSelected": false,
                                                    "serviceEndpoint": {
                                                        "signInEndpoint": {
                                                            "directSignIn": {
                                                                "accountIndex": 1
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        ]
                                    }
                                }
                            ]
                        }
                    }
                }
            }
        ]
    })");

    auto channels = YouTubeClient::extractChannelsFromJson(sampleAccountMenu);
    CHECK_EQ(channels.size(), 3);

    if (channels.size() >= 3) {
        CHECK_EQ(channels[0].name, "Primary Channel");
        CHECK_EQ(channels[0].handle, "@primary");
        CHECK_EQ(channels[0].channelId, "UCprimary123");
        CHECK(channels[0].isSelected);

        CHECK_EQ(channels[1].name, "Brand Channel");
        CHECK_EQ(channels[1].handle, "@brand");
        CHECK_EQ(channels[1].channelId, "UCbrand456");
        CHECK_EQ(channels[1].pageId, "109283746501928374650");
        CHECK(!channels[1].isSelected);

        CHECK_EQ(channels[2].name, "Slot 1 Channel");
        CHECK_EQ(channels[2].handle, "@slot1");
        CHECK_EQ(channels[2].authUserIndex, 1);
    }

    // Test realistic split identity deduplication (activeAccountHeaderRenderer with UC... + accountItemRenderer with
    // pageId)
    nlohmann::json splitIdentityJson = nlohmann::json::parse(R"({
        "contents": [
            {
                "activeAccountHeaderRenderer": {
                    "accountName": { "simpleText": "Hum Bao" },
                    "channelHandle": { "simpleText": "@HummBao" },
                    "channelId": "UCCGjdW0r4nt1HtDfgkFdIxA",
                    "accountPhoto": { "thumbnails": [ { "url": "https://avatar1.jpg" } ] }
                }
            },
            {
                "accountItemRenderer": {
                    "accountName": { "simpleText": "Kyle Ricks" },
                    "channelHandle": { "simpleText": "LiftedWorm84@gmail.com" },
                    "isSelected": false
                }
            },
            {
                "accountItemRenderer": {
                    "accountName": { "simpleText": "Kyle Ricks" },
                    "accountByline": { "simpleText": "No channel" },
                    "isSelected": false
                }
            },
            {
                "accountItemRenderer": {
                    "accountName": { "simpleText": "Hum Bao" },
                    "channelHandle": { "simpleText": "@HummBao" },
                    "isSelected": true,
                    "serviceEndpoint": {
                        "selectActiveIdentityEndpoint": {
                            "supportedTokens": [
                                { "pageIdToken": { "pageId": "100534006232163668770" } }
                            ]
                        }
                    }
                }
            }
        ]
    })");

    auto mergedChannels = YouTubeClient::extractChannelsFromJson(splitIdentityJson);
    CHECK_EQ(mergedChannels.size(), 2);
    if (mergedChannels.size() >= 2) {
        CHECK_EQ(mergedChannels[0].name, "Hum Bao");
        CHECK_EQ(mergedChannels[0].handle, "@HummBao");
        CHECK_EQ(mergedChannels[0].channelId, "UCCGjdW0r4nt1HtDfgkFdIxA");
        CHECK_EQ(mergedChannels[0].pageId, "100534006232163668770");
        CHECK_EQ(mergedChannels[0].authUserIndex, 0);
        CHECK(mergedChannels[0].isSelected);

        CHECK_EQ(mergedChannels[1].name, "Kyle Ricks");
        CHECK_EQ(mergedChannels[1].handle, "LiftedWorm84@gmail.com");
    }

    std::cout << "[PASS] testExtractAccountChannels\n";
}

void testChannelSelectionState() {
    YouTubeClient client;
    CHECK_EQ(client.getSelectedChannelPageId(), "");
    CHECK_EQ(client.getSelectedAuthUserIndex(), -1);
    CHECK_EQ(client.getSelectedChannelName(), "");

    // Setting a brand pageId with authUserIndex = -1 must default to 0
    client.setSelectedChannel("100534006232163668770", -1, "Hum Bao");
    CHECK_EQ(client.getSelectedChannelPageId(), "100534006232163668770");
    CHECK_EQ(client.getSelectedAuthUserIndex(), 0);
    CHECK_EQ(client.getSelectedChannelName(), "Hum Bao");

    // Setting explicit authUserIndex
    client.setSelectedChannel("109283746501928374650", 1, "Brand Channel");
    CHECK_EQ(client.getSelectedChannelPageId(), "109283746501928374650");
    CHECK_EQ(client.getSelectedAuthUserIndex(), 1);
    CHECK_EQ(client.getSelectedChannelName(), "Brand Channel");

    // Clearing channel
    client.setSelectedChannel("", -1, "");
    CHECK_EQ(client.getSelectedChannelPageId(), "");
    CHECK_EQ(client.getSelectedAuthUserIndex(), -1);
    CHECK_EQ(client.getSelectedChannelName(), "");

    std::cout << "[PASS] testChannelSelectionState\n";
}

void testChannelCachingAndSeparation() {
    YouTubeClient client;
    CHECK(client.getCachedAccountChannels().empty());

    // Verify separation between Brand Account and Primary Account sharing the same name
    nlohmann::json jsonSameName = nlohmann::json::parse(R"({
        "contents": [
            {
                "accountItemRenderer": {
                    "title": { "simpleText": "Kyle Ricks" },
                    "accountByline": { "simpleText": "No channel" },
                    "isSelected": false
                }
            },
            {
                "accountItemRenderer": {
                    "title": { "simpleText": "Kyle Ricks" },
                    "accountByline": { "simpleText": "100 subscribers" },
                    "isSelected": true,
                    "serviceEndpoint": {
                        "selectActiveIdentityEndpoint": {
                            "supportedTokens": [
                                { "pageIdToken": { "pageId": "100534006232163668770" } }
                            ]
                        }
                    }
                }
            }
        ]
    })");

    auto channels = YouTubeClient::extractChannelsFromJson(jsonSameName);
    CHECK_EQ(channels.size(), 2);
    if (channels.size() >= 2) {
        CHECK_EQ(channels[0].name, "Kyle Ricks");
        CHECK_EQ(channels[0].pageId, "");

        CHECK_EQ(channels[1].name, "Kyle Ricks");
        CHECK_EQ(channels[1].pageId, "100534006232163668770");
    }

    client.clearCachedAccountChannels();
    CHECK(client.getCachedAccountChannels().empty());

    std::cout << "[PASS] testChannelCachingAndSeparation\n";
}

void testHeadersAndContextWithChannel() {
    auto cm = std::make_shared<CookieManager>();
    YouTubeClient client(cm);

    // Initial state (anonymous / no channel)
    auto anonHeaders = client.buildHeaders(false);
    for (const auto &h : anonHeaders) {
        CHECK(h.find("X-Goog-AuthUser") == std::string::npos);
        CHECK(h.find("X-Goog-PageId") == std::string::npos);
    }

    auto baseCtx = client.buildBaseContext();
    CHECK(!baseCtx.contains("user") || !baseCtx["user"].contains("authuser"));
    CHECK(!baseCtx["client"].contains("onBehalfOfUser"));

    // Set channel and account index
    client.setSelectedChannel("99887766554433221100", 2, "Test Channel");

    // Authenticated headers include X-Goog-AuthUser, X-Goog-PageId, Chrome 131 UA, Sec-Ch-Ua, and custom Referer
    auto authHeaders = client.buildHeaders(true, "https://www.youtube.com/watch?v=dQw4w9WgXcQ");
    bool foundAuthUser = false;
    bool foundPageId = false;
    bool foundChrome131 = false;
    bool foundSecChUa = false;
    bool foundCustomReferer = false;
    for (const auto &h : authHeaders) {
        if (h == "X-Goog-AuthUser: 2")
            foundAuthUser = true;
        if (h == "X-Goog-PageId: 99887766554433221100")
            foundPageId = true;
        if (h.find("Chrome/131") != std::string::npos)
            foundChrome131 = true;
        if (h.find("Sec-Ch-Ua:") != std::string::npos)
            foundSecChUa = true;
        if (h == "Referer: https://www.youtube.com/watch?v=dQw4w9WgXcQ")
            foundCustomReferer = true;
    }
    CHECK(foundAuthUser);
    CHECK(foundPageId);
    CHECK(foundChrome131);
    CHECK(foundSecChUa);
    CHECK(foundCustomReferer);

    // Context contains client onBehalfOfUser and user authuser
    auto chCtx = client.buildBaseContext();
    CHECK_EQ(chCtx["client"]["onBehalfOfUser"].get<std::string>(), "99887766554433221100");
    CHECK(!chCtx["user"].contains("onBehalfOfUser"));
    CHECK_EQ(chCtx["user"]["authuser"].get<int>(), 2);

    // Verify rootAccountOnly suppresses X-Goog-PageId and onBehalfOfUser
    auto rootHeaders = client.buildHeaders(true, "https://www.youtube.com/", /*rootAccountOnly=*/true);
    for (const auto &h : rootHeaders) {
        CHECK(h.find("X-Goog-PageId") == std::string::npos);
        CHECK(h.find("X-Goog-AuthUser") == std::string::npos);
    }
    auto rootCtx = client.buildBaseContext(false, /*rootAccountOnly=*/true);
    CHECK(!rootCtx.contains("user") || !rootCtx["user"].contains("onBehalfOfUser"));
    CHECK(!rootCtx["client"].contains("onBehalfOfUser"));

    std::cout << "[PASS] testHeadersAndContextWithChannel\n";
}

void testYouTubeClientAnonymousVsAuthContext() {
    YouTubeClient client;
    client.setSelectedChannel("99887766554433221100", 2, "Test Channel");

    // Authenticated context has authuser and onBehalfOfUser
    auto authCtx = client.buildBaseContext(false);
    CHECK(authCtx["client"].contains("onBehalfOfUser"));
    CHECK(authCtx["user"].contains("authuser"));

    // Anonymous context strips authuser and onBehalfOfUser
    auto anonCtx = client.buildBaseContext(true);
    CHECK(!anonCtx["client"].contains("onBehalfOfUser"));
    CHECK(!anonCtx.contains("user"));
    CHECK_EQ(anonCtx["client"]["clientName"].get<std::string>(), "WEB");

    std::cout << "[PASS] testYouTubeClientAnonymousVsAuthContext\n";
}

void testYouTubeClientLikeAndSubscription() {
    auto cm = std::make_shared<CookieManager>();
    YouTubeClient client(cm);

    std::string err;
    // Without authentication, liking or subscribing should safely return false
    CHECK(!client.likeVideo("dQw4w9WgXcQ", true, err));
    CHECK(!err.empty());

    CHECK(!client.likeVideo("dQw4w9WgXcQ", false, err));
    CHECK(!err.empty());

    CHECK(!client.removeLike("dQw4w9WgXcQ", err));
    CHECK(!err.empty());

    CHECK(!client.subscribeChannel("UC_x5XG1OV2P6uZZ5FSM9Ttw", true, err));
    CHECK(!err.empty());

    // Empty parameters
    CHECK(!client.likeVideo("", true, err));
    CHECK(!client.removeLike("", err));
    CHECK(!client.subscribeChannel("", true, err));

    std::cout << "[PASS] testYouTubeClientLikeAndSubscription\n";
}

void testPlayerControllerStateAndEvents() {
    PlayerController controller;

    // Check defaults
    CHECK_EQ(static_cast<int>(controller.getState()), static_cast<int>(PlaybackState::Idle));
    CHECK_EQ(controller.getPosition(), 0.0);
    CHECK_EQ(controller.getDuration(), 0.0);
    CHECK(!controller.isPaused());
    CHECK_EQ(controller.getVolume(), 100);

    // State changes
    controller.setState(PlaybackState::Buffering);
    CHECK_EQ(static_cast<int>(controller.getState()), static_cast<int>(PlaybackState::Buffering));

    // Volume clamping
    controller.setVolume(120);
    CHECK_EQ(controller.getVolume(), 100);
    controller.setVolume(-10);
    CHECK_EQ(controller.getVolume(), 0);
    controller.setVolume(75);
    CHECK_EQ(controller.getVolume(), 75);

    // Event callbacks
    PlaybackState lastState = PlaybackState::Idle;
    double lastPos = 0.0;
    double lastDur = 0.0;
    bool lastPaused = false;
    double lastCache = 0.0;
    std::string lastError;

    PlayerEvents events;
    events.onState = [&](PlaybackState st) { lastState = st; };
    events.onTime = [&](double p, double d) {
        lastPos = p;
        lastDur = d;
    };
    events.onPause = [&](bool p) { lastPaused = p; };
    events.onCache = [&](double c) { lastCache = c; };
    events.onError = [&](const std::string &err) { lastError = err; };

    controller.setEvents(events);

    controller.notifyState(PlaybackState::Playing);
    CHECK_EQ(static_cast<int>(lastState), static_cast<int>(PlaybackState::Playing));
    CHECK_EQ(static_cast<int>(controller.getState()), static_cast<int>(PlaybackState::Playing));

    controller.notifyTime(42.5, 300.0);
    CHECK_EQ(lastPos, 42.5);
    CHECK_EQ(lastDur, 300.0);
    CHECK_EQ(controller.getPosition(), 42.5);
    CHECK_EQ(controller.getDuration(), 300.0);

    // Same integer-second floor (42) and same duration within 250ms: state updates, callback throttled
    controller.notifyTime(42.8, 300.0);
    CHECK_EQ(controller.getPosition(), 42.8);
    CHECK_EQ(lastPos, 42.5);

    // Integer-second floor changes (43): callback fires immediately
    controller.notifyTime(43.1, 300.0);
    CHECK_EQ(lastPos, 43.1);

    // Duration changes: callback fires immediately even within same integer second
    controller.notifyTime(43.2, 310.0);
    CHECK_EQ(lastPos, 43.2);
    CHECK_EQ(lastDur, 310.0);

    controller.notifyPause(true);
    CHECK(lastPaused);
    CHECK(controller.isPaused());
    CHECK_EQ(static_cast<int>(controller.getState()), static_cast<int>(PlaybackState::Paused));

    controller.notifyCache(15.0);
    CHECK_EQ(lastCache, 15.0);

    controller.notifyError("test playback error");
    CHECK_EQ(lastError, "test playback error");

    // Concurrent safety test: reassigning callbacks while dispatching
    // First, clear existing callbacks so no local stack variables are referenced concurrently
    controller.setEvents({});

    std::atomic<bool> stopFlag{false};
    std::atomic<int> dispatchCount{0};
    std::thread dispatchThread([&]() {
        double pos = 0.0;
        while (!stopFlag.load()) {
            controller.notifyTime(pos += 0.1, 100.0);
            controller.notifyState(PlaybackState::Playing);
            dispatchCount.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::yield();
        }
    });

    for (int i = 0; i < 50; ++i) {
        PlayerEvents newEvents;
        newEvents.onTime = [](double, double) {};
        newEvents.onState = [](PlaybackState) {};
        controller.setEvents(std::move(newEvents));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    stopFlag.store(true);
    dispatchThread.join();
    CHECK(dispatchCount.load() > 0);

    // Verify qualityToFormatString helper delegation
    CHECK(!PlayerController::qualityToFormatString("1080p").empty());
    CHECK(!PlayerController::qualityToFormatString("Audio Only").empty());

    // Verify anti-throttling headers and buffer options
    auto headers = PlayerController::getAntiThrottlingHeaders();
    CHECK(headers.find("Origin: https://www.youtube.com") != std::string_view::npos);
    CHECK(headers.find("Referer: https://www.youtube.com/") != std::string_view::npos);

    auto opts = PlayerController::getStreamingBufferOptions();
    CHECK_EQ(std::string_view(opts.cachePauseInitial), "yes");
    CHECK_EQ(std::string_view(opts.cachePauseWait), "2");
    CHECK_EQ(std::string_view(opts.demuxerReadaheadSecs), "60");
    CHECK_EQ(std::string_view(opts.demuxerMaxBytes), "150MiB");
    CHECK_EQ(std::string_view(opts.demuxerMaxBackBytes), "50MiB");
    CHECK_EQ(std::string_view(opts.cacheSecs), "120");
    CHECK_EQ(std::string_view(opts.demuxerCacheWait), "no");
    CHECK_EQ(std::string_view(opts.streamBufferSize), "2MiB");

    std::cout << "[PASS] testPlayerControllerStateAndEvents\n";
}

void testYouTubeClientBeaconUrlsAndNonce() {
    std::string nonce1 = YouTubeClient::generatePlaybackNonce();
    std::string nonce2 = YouTubeClient::generatePlaybackNonce();
    CHECK_EQ(nonce1.size(), 16);
    CHECK_EQ(nonce2.size(), 16);
    CHECK(nonce1 != nonce2);

    for (char c : nonce1) {
        bool valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        CHECK(valid);
    }

    std::string basePlayback = "https://s.youtube.com/api/stats/playback";
    std::string playbackBeacon = YouTubeClient::buildPlaybackBeaconUrl(basePlayback, nonce1);
    CHECK(playbackBeacon.find("ns=yt") != std::string::npos);
    CHECK(playbackBeacon.find("ver=2") != std::string::npos);
    CHECK(playbackBeacon.find("cpn=" + nonce1) != std::string::npos);

    std::string baseWatchtime = "https://s.youtube.com/api/stats/watchtime?docid=dQw4w9WgXcQ";
    std::string watchtimeBeacon = YouTubeClient::buildWatchtimeBeaconUrl(baseWatchtime, nonce1, 45.5, 212.0, false);
    CHECK(watchtimeBeacon.find("ns=yt") != std::string::npos);
    CHECK(watchtimeBeacon.find("ver=2") != std::string::npos);
    CHECK(watchtimeBeacon.find("cpn=" + nonce1) != std::string::npos);
    CHECK(watchtimeBeacon.find("state=playing") != std::string::npos);
    CHECK(watchtimeBeacon.find("cmt=45.500") != std::string::npos);
    CHECK(watchtimeBeacon.find("len=212.000") != std::string::npos);

    std::string finishedBeacon = YouTubeClient::buildWatchtimeBeaconUrl(baseWatchtime, nonce1, 212.0, 212.0, true);
    CHECK(finishedBeacon.find("state=completed") != std::string::npos);
    CHECK(finishedBeacon.find("cmt=212.000") != std::string::npos);

    // Verify st and et intervals
    std::string intervalBeacon =
        YouTubeClient::buildWatchtimeBeaconUrl(baseWatchtime, nonce1, 10.0, 20.0, 212.0, false);
    CHECK(intervalBeacon.find("st=10.000") != std::string::npos);
    CHECK(intervalBeacon.find("et=20.000") != std::string::npos);
    CHECK(intervalBeacon.find("cmt=20.000") != std::string::npos);

    std::cout << "[PASS] testYouTubeClientBeaconUrlsAndNonce\n";
}

void testYouTubeClientWatchHistory() {
    auto cm = std::make_shared<CookieManager>();
    YouTubeClient client(cm);

    // Unauthenticated recordWatchHistory and recordWatchTime must safely no-op
    client.recordWatchHistory("");
    client.recordWatchHistory("dQw4w9WgXcQ");
    client.recordWatchTime("", 10.0, 100.0, false);
    client.recordWatchTime("dQw4w9WgXcQ", 10.0, 100.0, false);

    // Load auth cookie
    CHECK(cm->loadFromString("# Netscape HTTP Cookie File\n"
                             ".youtube.com\tTRUE\t/\tTRUE\t2147483647\tSAPISID\tTestSapisid\n"
                             ".youtube.com\tTRUE\t/\tTRUE\t2147483647\tLOGIN_INFO\tTestLogin\n"));
    CHECK(cm->hasAuth());

    // Empty video ID must safely no-op
    client.recordWatchHistory("");
    client.recordWatchTime("", 10.0, 100.0, false);

    // Safe destruction test: destruction with queue joins cleanly without hangs
    {
        YouTubeClient clientScope(cm);
        clientScope.recordWatchHistory("dQw4w9WgXcQ");
        clientScope.recordWatchTime("dQw4w9WgXcQ", 15.0, 100.0, false);
        clientScope.recordWatchTime("dQw4w9WgXcQ", 100.0, 100.0, true);
    }

    std::cout << "[PASS] testYouTubeClientWatchHistory\n";
}

void testCookieManagerCrossDomainGoogleAuth() {
    CookieManager mgr;
    // Simulate user exporting cookies where Google session cookies are under .google.com
    // while YouTube specific cookies are under .youtube.com
    std::string netscape = "# Netscape HTTP Cookie File\n"
                           ".google.com\tTRUE\t/\tTRUE\t2107186271\tSAPISID\tGoogleSapisidSecret\n"
                           ".google.com\tTRUE\t/\tTRUE\t2107186271\tSID\tGoogleSidSecret\n"
                           ".youtube.com\tTRUE\t/\tTRUE\t2107186271\tLOGIN_INFO\tYtLoginInfo\n";

    bool ok = mgr.loadFromString(netscape);
    CHECK(ok);
    CHECK(mgr.hasAuth());

    // Verify that getCookie("SAPISID") retrieves the .google.com cookie
    CHECK_EQ(mgr.getCookie("SAPISID"), "GoogleSapisidSecret");

    // Verify that getCookieHeaderValue("www.youtube.com") includes the .google.com session cookies
    // so that YouTube's InnerTube API receives the exact SAPISID and SID needed for authorization!
    std::string header = mgr.getCookieHeaderValue("www.youtube.com");
    CHECK(header.find("SAPISID=GoogleSapisidSecret") != std::string::npos);
    CHECK(header.find("SID=GoogleSidSecret") != std::string::npos);
    CHECK(header.find("LOGIN_INFO=YtLoginInfo") != std::string::npos);

    // Verify that getSapisidHash() produces a valid header based on that SAPISID
    std::string hash = mgr.getSapisidHash("https://www.youtube.com");
    CHECK(hash.starts_with("SAPISIDHASH "));

    // Now test shadowing: if .youtube.com also defines SAPISID, .youtube.com must win
    mgr.setCookie("SAPISID", "YtSpecificSapisid", ".youtube.com");
    CHECK_EQ(mgr.getCookie("SAPISID"), "YtSpecificSapisid");
    std::string shadowedHeader = mgr.getCookieHeaderValue("www.youtube.com");
    CHECK(shadowedHeader.find("SAPISID=YtSpecificSapisid") != std::string::npos);
    CHECK(shadowedHeader.find("GoogleSapisidSecret") == std::string::npos);

    std::cout << "[PASS] testCookieManagerCrossDomainGoogleAuth\n";
}

void testCookieManagerNetscapeStrictCompliance() {
    CookieManager mgr;
    // Test that foreign domain cookies like x.com are purged and Netscape formatting
    // complies strictly with Python MozillaCookieJar (assert domain_specified == initial_dot)
    std::string dirtyNetscape = "# Netscape HTTP Cookie File\n"
                                "x.com\tTRUE\t/\tFALSE\t1826485339\t__cuid\t105f0a4511494b2e98ed08141ab95cc2\n"
                                "twitter.com\tTRUE\t/\tFALSE\t1826485339\tguest_id\tv1%3A123\n"
                                ".youtube.com\tTRUE\t/\tTRUE\t2107186271\tSAPISID\tCleanYtSapisid\n"
                                "google.com\tFALSE\t/\tTRUE\t2107186271\tSID\tCleanGoogleSid\n"
                                "#HttpOnly_.youtube.com\tTRUE\t/\tTRUE\t2107186271\tLOGIN_INFO\tCleanLoginInfo\n";

    bool ok = mgr.loadFromString(dirtyNetscape);
    CHECK(ok);
    CHECK(mgr.hasAuth());

    // Foreign cookies must be discarded
    CHECK_EQ(mgr.getCookie("__cuid"), "");
    CHECK_EQ(mgr.getCookie("guest_id"), "");

    // Google/YouTube cookies must be preserved
    CHECK_EQ(mgr.getCookie("SAPISID"), "CleanYtSapisid");
    CHECK_EQ(mgr.getCookie("SID"), "CleanGoogleSid");
    CHECK_EQ(mgr.getCookie("LOGIN_INFO"), "CleanLoginInfo");

    const std::filesystem::path tmpPath =
        std::filesystem::temp_directory_path() / "lightyt_test_cache" / "strict_netscape_cookies.txt";
    bool saved = mgr.saveToFile(tmpPath);
    CHECK(saved);

    // Read back saved file line by line and verify Python MozillaCookieJar compliance:
    // Every line MUST satisfy: (column 1 == "TRUE") == (column 0 starts with '.')
    std::ifstream file(tmpPath);
    CHECK(file.is_open());
    std::string line;
    int dataLineCount = 0;
    while (std::getline(file, line)) {
        if (line.empty()) {
            continue;
        }
        if (line.starts_with("#HttpOnly_")) {
            line.erase(0, 10);
        } else if (line.starts_with('#')) {
            continue;
        }
        std::stringstream ss(line);
        std::string col0, col1, col2, col3, col4, col5, col6;
        CHECK(std::getline(ss, col0, '\t'));
        CHECK(std::getline(ss, col1, '\t'));
        CHECK(std::getline(ss, col2, '\t'));
        CHECK(std::getline(ss, col3, '\t'));
        CHECK(std::getline(ss, col4, '\t'));
        CHECK(std::getline(ss, col5, '\t'));
        CHECK(std::getline(ss, col6, '\t'));

        // Crucial Python http.cookiejar assertion check:
        bool initialDot = col0.starts_with('.');
        bool domainSpecified = (col1 == "TRUE");
        CHECK_EQ(domainSpecified, initialDot);

        // Crucial domain check: no foreign domain allowed
        CHECK(col0.find("youtube.com") != std::string::npos || col0.find("google.com") != std::string::npos);
        CHECK(col0.find("x.com") == std::string::npos);
        CHECK(col0.find("twitter.com") == std::string::npos);

        ++dataLineCount;
    }
    CHECK(dataLineCount >= 3);

    std::cout << "[PASS] testCookieManagerNetscapeStrictCompliance\n";
}

void testStringUtilsAndDeepJsonGuard() {
    CHECK_EQ(lightyt::utils::stripShortsPrefix("shorts-shelf-item-dQw4w9WgXcQ"), "dQw4w9WgXcQ");
    CHECK_EQ(lightyt::utils::stripShortsPrefix("shorts-item-dQw4w9WgXcQ"), "dQw4w9WgXcQ");
    CHECK_EQ(lightyt::utils::stripShortsPrefix("shorts-dQw4w9WgXcQ"), "dQw4w9WgXcQ");
    CHECK_EQ(lightyt::utils::stripShortsPrefix("dQw4w9WgXcQ"), "dQw4w9WgXcQ");

    CHECK_EQ(lightyt::utils::normalizeAvatarUrl("//yt3.ggpht.com/abc-rw"), "https://yt3.ggpht.com/abc-rj");
    CHECK_EQ(lightyt::utils::normalizeAvatarUrl("//yt3.ggpht.com/abc-rw", /*preferWebp=*/true),
             "https://yt3.ggpht.com/abc-rw");
    CHECK_EQ(lightyt::utils::normalizeAvatarUrl("//yt3.ggpht.com/abc-rj", /*preferWebp=*/true),
             "https://yt3.ggpht.com/abc-rw");
    CHECK_EQ(lightyt::utils::convertAvatarUrlToJpeg("//yt3.ggpht.com/abc-rw"), "https://yt3.ggpht.com/abc-rj");
    CHECK_EQ(lightyt::utils::convertAvatarUrlToWebp("//yt3.ggpht.com/abc-rj"), "https://yt3.ggpht.com/abc-rw");
    CHECK_EQ(lightyt::utils::convertThumbnailUrlToJpeg("https://i.ytimg.com/vi_webp/dQw4w9WgXcQ/maxresdefault.webp"),
             "https://i.ytimg.com/vi/dQw4w9WgXcQ/maxresdefault.jpg");
    CHECK_EQ(lightyt::utils::convertThumbnailUrlToJpeg("https://i.ytimg.com/vi/abc/hqdefault.webp"),
             "https://i.ytimg.com/vi/abc/hqdefault.jpg");
    CHECK_EQ(lightyt::utils::normalizeAvatarUrl("https://yt3.ggpht.com/abc"), "https://yt3.ggpht.com/abc");
    CHECK_EQ(lightyt::utils::normalizeAvatarUrl(""), "");

    // Verify extractBestThumbnail adaptive WebP vs JPEG selection
    nlohmann::json webpThumbObj = {
        {"thumbnails", {{{"url", "https://i.ytimg.com/vi_webp/dQw4w9WgXcQ/hq720.webp"}, {"width", 720}}}}};
    CHECK_EQ(lightyt::InnerTubeParser::extractBestThumbnail(webpThumbObj, "dQw4w9WgXcQ", /*preferWebp=*/true),
             "https://i.ytimg.com/vi_webp/dQw4w9WgXcQ/hq720.webp");
    CHECK_EQ(lightyt::InnerTubeParser::extractBestThumbnail(webpThumbObj, "dQw4w9WgXcQ", /*preferWebp=*/false),
             "https://i.ytimg.com/vi/dQw4w9WgXcQ/hqdefault.jpg");

    // Build a 100-level deeply nested JSON object to verify depth > 64 guard in InnerTubeParser
    nlohmann::json deep = nlohmann::json::object();
    nlohmann::json *curr = &deep;
    for (int i = 0; i < 100; ++i) {
        (*curr)["nested"] = nlohmann::json::object();
        curr = &((*curr)["nested"]);
    }
    (*curr)["videoRenderer"] = {{"videoId", "dQw4w9WgXcQ"}, {"title", {{"simpleText", "Too Deep"}}}};
    (*curr)["continuationCommand"] = {{"token", "DeepToken"}};

    auto videos = lightyt::InnerTubeParser::extractVideos(deep);
    CHECK(videos.empty());
    auto token = lightyt::InnerTubeParser::extractContinuationToken(deep);
    CHECK(token.empty());
    auto channels = lightyt::InnerTubeParser::extractAccountChannels(deep);
    CHECK(channels.empty());

    std::cout << "[PASS] testStringUtilsAndDeepJsonGuard\n";
}

void testLogger() {
    using namespace lightyt::log;

    Logger &logger = Logger::instance();
    // Default level should be Warn
    CHECK_EQ(static_cast<int>(logger.getLevel()), static_cast<int>(Level::Warn));

    // Test levelToString
    CHECK_EQ(std::string(levelToString(Level::Trace)), "trace");
    CHECK_EQ(std::string(levelToString(Level::Debug)), "debug");
    CHECK_EQ(std::string(levelToString(Level::Info)), "info");
    CHECK_EQ(std::string(levelToString(Level::Warn)), "warn");
    CHECK_EQ(std::string(levelToString(Level::Error)), "error");
    CHECK_EQ(std::string(levelToString(Level::None)), "none");

    // Test stringToLevel with standard values
    CHECK_EQ(static_cast<int>(stringToLevel("trace")), static_cast<int>(Level::Trace));
    CHECK_EQ(static_cast<int>(stringToLevel("debug")), static_cast<int>(Level::Debug));
    CHECK_EQ(static_cast<int>(stringToLevel("info")), static_cast<int>(Level::Info));
    CHECK_EQ(static_cast<int>(stringToLevel("warn")), static_cast<int>(Level::Warn));
    CHECK_EQ(static_cast<int>(stringToLevel("warning")), static_cast<int>(Level::Warn));
    CHECK_EQ(static_cast<int>(stringToLevel("error")), static_cast<int>(Level::Error));
    CHECK_EQ(static_cast<int>(stringToLevel("none")), static_cast<int>(Level::None));
    CHECK_EQ(static_cast<int>(stringToLevel("off")), static_cast<int>(Level::None));

    // Test case-insensitivity
    CHECK_EQ(static_cast<int>(stringToLevel("DEBUG")), static_cast<int>(Level::Debug));
    CHECK_EQ(static_cast<int>(stringToLevel("WaRnInG")), static_cast<int>(Level::Warn));

    // Test fallback for invalid string
    CHECK_EQ(static_cast<int>(stringToLevel("invalid_foo", Level::Warn)), static_cast<int>(Level::Warn));
    CHECK_EQ(static_cast<int>(stringToLevel("", Level::Info)), static_cast<int>(Level::Info));

    // Test setLevel and getLevel
    logger.setLevel(Level::Debug);
    CHECK_EQ(static_cast<int>(logger.getLevel()), static_cast<int>(Level::Debug));
    logger.setLevel(Level::Warn);
    CHECK_EQ(static_cast<int>(logger.getLevel()), static_cast<int>(Level::Warn));

    std::cout << "[PASS] testLogger\n";
}

void testPlayerControllerPlaybackSpeed() {
    PlayerController controller;

    // Default speed is 1.0
    CHECK_EQ(controller.getPlaybackSpeed(), 1.0);

    // Setting speeds within [0.25, 4.0]
    controller.setPlaybackSpeed(1.25);
    CHECK_EQ(controller.getPlaybackSpeed(), 1.25);

    controller.setPlaybackSpeed(1.5);
    CHECK_EQ(controller.getPlaybackSpeed(), 1.5);

    controller.setPlaybackSpeed(1.75);
    CHECK_EQ(controller.getPlaybackSpeed(), 1.75);

    controller.setPlaybackSpeed(2.0);
    CHECK_EQ(controller.getPlaybackSpeed(), 2.0);

    // Clamping lower bound
    controller.setPlaybackSpeed(0.1);
    CHECK_EQ(controller.getPlaybackSpeed(), 0.25);

    controller.setPlaybackSpeed(-1.0);
    CHECK_EQ(controller.getPlaybackSpeed(), 0.25);

    // Clamping upper bound
    controller.setPlaybackSpeed(5.0);
    CHECK_EQ(controller.getPlaybackSpeed(), 4.0);

    // stop() resets speed to 1.0
    controller.setPlaybackSpeed(1.5);
    CHECK_EQ(controller.getPlaybackSpeed(), 1.5);
    controller.stop();
    CHECK_EQ(controller.getPlaybackSpeed(), 1.0);

    std::cout << "[PASS] testPlayerControllerPlaybackSpeed\n";
}

void testExtractAvailableQualities() {
    // 1. Fallback when empty or invalid root
    {
        nlohmann::json emptyObj = nlohmann::json::object();
        auto q = lightyt::InnerTubeParser::extractAvailableQualities(emptyObj);
        std::vector<std::string> expected = {"Auto", "1080p", "720p", "480p", "360p", "Audio Only"};
        CHECK_EQ(q.size(), expected.size());
        for (size_t i = 0; i < q.size() && i < expected.size(); ++i) {
            CHECK_EQ(q[i], expected[i]);
        }
    }

    // 2. Specific adaptiveFormats (e.g., 720p and 360p only)
    {
        nlohmann::json root = {
            {"streamingData",
             {{"adaptiveFormats", nlohmann::json::array({{{"height", 720}, {"qualityLabel", "720p"}},
                                                         {{"height", 360}, {"qualityLabel", "360p"}}})}}}};
        auto q = lightyt::InnerTubeParser::extractAvailableQualities(root);
        std::vector<std::string> expected = {"Auto", "720p", "360p", "Audio Only"};
        CHECK_EQ(q.size(), expected.size());
        for (size_t i = 0; i < q.size() && i < expected.size(); ++i) {
            CHECK_EQ(q[i], expected[i]);
        }
    }

    // 3. Formats with duplicates / multiple framerates (e.g. 1080p60 and 1080p)
    {
        nlohmann::json root = {{"streamingData",
                                {{"adaptiveFormats", nlohmann::json::array({{{"height", 1080}, {"fps", 60}},
                                                                            {{"height", 1080}, {"fps", 30}},
                                                                            {{"height", 720}, {"fps", 60}},
                                                                            {{"height", 480}, {"fps", 30}},
                                                                            {{"height", 360}, {"fps", 30}}})}}}};
        auto q = lightyt::InnerTubeParser::extractAvailableQualities(root);
        std::vector<std::string> expected = {"Auto", "1080p", "720p", "480p", "360p", "Audio Only"};
        CHECK_EQ(q.size(), expected.size());
        for (size_t i = 0; i < q.size() && i < expected.size(); ++i) {
            CHECK_EQ(q[i], expected[i]);
        }
    }

    // 4. qualityLabel fallback when height is missing
    {
        nlohmann::json root = {
            {"streamingData", {{"formats", nlohmann::json::array({{{"qualityLabel", "720p HD"}}})}}}};
        auto q = lightyt::InnerTubeParser::extractAvailableQualities(root);
        std::vector<std::string> expected = {"Auto", "720p", "Audio Only"};
        CHECK_EQ(q.size(), expected.size());
        for (size_t i = 0; i < q.size() && i < expected.size(); ++i) {
            CHECK_EQ(q[i], expected[i]);
        }
    }

    std::cout << "[PASS] testExtractAvailableQualities\n";
}

int main() noexcept {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    // Isolate test cache directory portably
    const std::filesystem::path testCachePath = std::filesystem::temp_directory_path() / "lightyt_test_cache";
    const std::string testCache = testCachePath.string();
#ifdef _WIN32
    _putenv_s("LIGHTYT_CACHE_DIR", testCache.c_str());
    SetEnvironmentVariableA("LIGHTYT_CACHE_DIR", testCache.c_str());
#else
    setenv("LIGHTYT_CACHE_DIR", testCache.c_str(), 1);
#endif
    std::error_code ec;
    std::filesystem::remove_all(testCachePath, ec);
    std::filesystem::create_directories(testCachePath, ec);

    std::cout << "Running LightYT Unit Tests...\n";
    try {
        testRfc3174Sha1Vectors();
        testCookieManagerNetscape();
        testCookieManagerHeader();
        testCookieManagerSaveAndLoadRoundtrip();
        testCookieManagerConcurrentAccess();
        testCookieManagerHeaderForUrlAndLockContention();
        testCookieManagerFallbackSapisid3P();
        testCookieDomainAndExpiry();
        testCookieManagerUpdateFromSetCookieAndResponse();
        testCookieManagerResilientExpiry();
        testCookieManagerCrossDomainGoogleAuth();
        testCookieManagerNetscapeStrictCompliance();
        testHttpClientBasics();
        testCurlApiDynamicLoading();
        testJsonClassicVideoRenderer();
        testJsonModernLockupViewModel();
        testJsonCommandContextLockup();
        testJsonReelItemRenderer();
        testShortsIdAndThumbnailMapping();
        testJsonWatchNextMetadata();
        testLikeCountParsing();
        testWatchNextMetadataLikes();
        testStreamResolver();
        testStreamResolverExpireParsing();
        testProcessCommandLineQuoting();
        testProcessExecution();
        testProcessLargeOutput();
        testProcessTimeoutAndCancellation();
        testThumbnailCache();
        testContinuationTokenParsing();
        testExtractAccountChannels();
        testChannelSelectionState();
        testChannelCachingAndSeparation();
        testHeadersAndContextWithChannel();
        testYouTubeClientAnonymousVsAuthContext();
        testYouTubeClientLikeAndSubscription();
        testYouTubeClientBeaconUrlsAndNonce();
        testYouTubeClientWatchHistory();
        testPlayerControllerStateAndEvents();
        testPlayerControllerPlaybackSpeed();
        testExtractAvailableQualities();
        testStringUtilsAndDeepJsonGuard();
        testLogger();
    } catch (const std::exception &e) {
        std::cerr << "Fatal test exception: " << e.what() << "\n";
        return 1;
    }

    std::filesystem::remove_all(testCache, ec);
#ifdef _WIN32
    _putenv_s("LIGHTYT_CACHE_DIR", "");
    SetEnvironmentVariableA("LIGHTYT_CACHE_DIR", nullptr);
#else
    unsetenv("LIGHTYT_CACHE_DIR");
#endif

    std::cout << "\nResults: " << g_passCount << " passed, " << g_failCount << " failed.\n";
    std::cout.flush();
    std::cerr.flush();
    std::fflush(stdout);
    std::fflush(stderr);
#ifdef _WIN32
    std::_Exit((g_failCount == 0) ? 0 : 1);
#else
    return (g_failCount == 0) ? 0 : 1;
#endif
}
