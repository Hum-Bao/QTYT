#include "core/CookieManager.h"
#include "core/StreamResolver.h"
#include "core/YouTubeClient.h"
#include "player/PlayerController.h"
#include "utils/AppPaths.h"
#include "utils/Logger.h"

#include <iomanip>
#include <iostream>

void printHelp() {
    std::cout
        << "LightYT CLI Tool - Test YouTube Feeds & Cookies\n"
        << "Usage:\n"
        << "  lightyt-cli [--cookies <path>] [--page-id <id>] [--auth-user <idx>] [--max <N>] [--verbose] [command]\n\n"
        << "Commands:\n"
        << "  feed                 Fetch home / recommended feed\n"
        << "  accounts             List detected Google/YouTube accounts and channels\n"
        << "  search <query>       Search YouTube for videos\n"
        << "  resolve <id/url>     Test yt-dlp direct stream URL resolution\n"
        << "  auth-check           Check cookie status and auth hash\n"
        << "  formats              Print supported quality format strings\n"
        << "  help                 Show this help message\n";
}

int main(int argc, char *argv[]) {
    std::string cookiePath;
    std::string command = "feed";
    std::string targetArg;
    std::string pageId;
    int authUser = -1;
    size_t maxResults = 0;
    std::string channelName;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--cookies") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --cookies\n";
                return 1;
            }
            cookiePath = argv[++i];
        } else if (arg == "--page-id") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --page-id\n";
                return 1;
            }
            pageId = argv[++i];
        } else if (arg == "--auth-user") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --auth-user\n";
                return 1;
            }
            try {
                authUser = std::stoi(argv[++i]);
            } catch (...) {
                std::cerr << "Invalid --auth-user value\n";
                return 1;
            }
        } else if (arg == "--max") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --max\n";
                return 1;
            }
            try {
                int val = std::stoi(argv[++i]);
                if (val <= 0) {
                    std::cerr << "--max must be a positive integer\n";
                    return 1;
                }
                maxResults = static_cast<size_t>(val);
            } catch (...) {
                std::cerr << "Invalid --max value\n";
                return 1;
            }
        } else if (arg == "--channel") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --channel\n";
                return 1;
            }
            channelName = argv[++i];
        } else if (arg == "--verbose" || arg == "-v") {
            lightyt::log::Logger::instance().setLevel(lightyt::log::Level::Debug);
        } else if (arg == "--trace" || arg == "-vv") {
            lightyt::log::Logger::instance().setLevel(lightyt::log::Level::Trace);
        } else if (arg == "--quiet" || arg == "-q") {
            lightyt::log::Logger::instance().setLevel(lightyt::log::Level::Error);
        } else if (arg == "feed" || arg == "accounts" || arg == "auth-check" || arg == "formats" || arg == "help") {
            command = arg;
        } else if (arg == "search" || arg == "resolve") {
            if (i + 1 >= argc) {
                std::cerr << "Missing argument for '" << arg << "'\n";
                return 1;
            }
            command = arg;
            targetArg = argv[++i];
        } else if (arg == "-h" || arg == "--help") {
            command = "help";
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            printHelp();
            return 1;
        }
    }

    if (command == "help") {
        printHelp();
        return 0;
    }

    if (command == "formats") {
        std::cout << "=== Supported Video Qualities ===\n";
        for (const auto &q : {"1080p", "720p", "480p", "360p", "Auto", "Audio Only"}) {
            std::cout << std::setw(12) << q << " -> " << PlayerController::qualityToFormatString(q) << "\n";
        }
        return 0;
    }

    if (cookiePath.empty()) {
        cookiePath = (lightyt::paths::getAppDataDir() / "cookies.txt").string();
    }

    auto cookieMgr = std::make_shared<CookieManager>();
    bool cookiesLoaded = cookieMgr->loadFromFile(cookiePath);
    // Keep CLI operations purely read-only so diagnostics never overwrite input files
    cookieMgr->setCookieFilePath("");

    std::cout << "========================================\n";
    std::cout << "       LightYT Diagnostic Tool          \n";
    std::cout << "========================================\n";
    std::cout << "Cookie File: " << cookiePath << " [" << (cookiesLoaded ? "Loaded" : "Not Found / Empty") << "]\n";
    std::cout << "Auth Status: " << (cookieMgr->hasAuth() ? "AUTHENTICATED (Personalized Mode)" : "GUEST (Public Feed)")
              << "\n";
    if (cookieMgr->hasAuth()) {
        std::cout << "SAPISID Hash:  " << cookieMgr->getSapisidHash() << "\n";
        std::cout << "Cookie Header: " << cookieMgr->getCookieHeaderValue("www.youtube.com") << "\n";
    }
    std::cout << "========================================\n\n";

    if (command == "auth-check") {
        return 0;
    }

    if (command == "resolve") {
        std::cout << "Resolving direct stream URLs for: " << targetArg << "...\n";
        StreamInfo info;
        std::string err;
        bool ok = StreamResolver::resolve(targetArg, "1080p", info, err, cookiesLoaded ? cookiePath : "");
        if (!ok) {
            std::cerr << "\n[Resolution Error]: " << err << "\n";
            return 1;
        }
        std::cout << "Successfully resolved stream:\n"
                  << "  Video URL: " << info.videoUrl << "\n"
                  << "  Audio URL: " << (info.audioUrl.empty() ? "(Combined in video stream)" : info.audioUrl) << "\n";
        return 0;
    }

    YouTubeClient client(cookieMgr);
    if (!pageId.empty() || authUser >= 0 || !channelName.empty()) {
        client.setSelectedChannel(pageId, authUser, channelName);
        std::cout << "Using active channel profile: '" << channelName << "' (pageId: " << pageId
                  << ", authUser: " << authUser << ")\n";
    }

    std::string error;

    if (command == "accounts") {
        std::cout << "Fetching account channels...\n";
        auto channels = client.fetchAccountChannels(error, true);
        if (!error.empty()) {
            std::cerr << "\n[Error]: " << error << "\n";
            return 1;
        }
        std::cout << "Detected " << channels.size() << " channel(s)/profile(s):\n";
        std::cout << "--------------------------------------------------------------------------------\n";
        for (size_t i = 0; i < channels.size(); ++i) {
            const auto &c = channels[i];
            std::cout << "[" << (i + 1) << "] Name:     " << c.name << "\n"
                      << "    Handle:   " << c.handle << "\n"
                      << "    PageID:   " << c.pageId << "\n"
                      << "    ChanID:   " << c.channelId << "\n"
                      << "    AuthUser: " << c.authUserIndex << "\n"
                      << "    Selected: " << (c.isSelected ? "true" : "false") << "\n\n";
        }
        return 0;
    }

    std::vector<VideoItem> videos;

    if (command == "feed") {
        std::cout << "Fetching " << (cookieMgr->hasAuth() ? "personalized recommendations..." : "home feed...") << "\n";
        videos = client.fetchHomeFeed(error);
    } else if (command == "search") {
        std::cout << "Searching for: \"" << targetArg << "\"...\n";
        videos = client.search(targetArg, error);
    }

    if (!error.empty()) {
        std::cerr << "\n[Error]: " << error << "\n";
        return 1;
    }

    const size_t count = (maxResults > 0 && maxResults < videos.size()) ? maxResults : videos.size();
    std::cout << "\nShowing " << count << " of " << videos.size() << " videos:\n";
    std::cout << "--------------------------------------------------------------------------------\n";
    for (size_t i = 0; i < count; ++i) {
        const auto &v = videos[i];
        std::cout << "[" << std::setw(2) << (i + 1) << "] " << v.title << "\n";
        std::cout << "     Channel:  " << v.author << "\n";
        std::cout << "     Duration: " << (v.duration.empty() ? "N/A" : v.duration)
                  << " | Views: " << (v.viewCount.empty() ? "N/A" : v.viewCount)
                  << " | Uploaded: " << (v.publishedTime.empty() ? "N/A" : v.publishedTime) << "\n";
        std::cout << "     Link:     " << v.getWatchUrl() << "\n\n";
    }
    std::cout << "--------------------------------------------------------------------------------\n";

    return 0;
}
