#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

struct StreamInfo {
    std::string videoUrl;         // Playable video stream URL (or combined video+audio)
    std::string audioUrl;         // Optional separate audio stream URL (DASH adaptive)
    bool isDirectStream = false;  // true if resolved to direct CDN stream URLs
    std::string videoItag;        // Extracted video stream itag (e.g. "137", "248", "399")
    std::string audioItag;        // Extracted audio stream itag (e.g. "140", "251")
};

class StreamResolver {
public:
    // Resolve YouTube video stream URLs using yt-dlp with caching and cancellation
    [[nodiscard]] static bool resolve(std::string_view videoIdOrUrl, std::string_view quality, StreamInfo &outInfo,
                                      std::string &outError, std::string_view cookieFilePath = {},
                                      bool allowAv1 = false, const std::atomic<bool> *cancelToken = nullptr);

    // Asynchronously prefetch and cache stream URLs for a video in the background (managed queue)
    static void prefetch(std::string_view videoIdOrUrl, std::string_view quality, std::string_view cookieFilePath = {},
                         bool allowAv1 = false);

    // Prune stream cache entries that have passed their expiration timestamp
    static void pruneExpired();

    // Cleanly cancel pending background prefetches and join worker threads
    static void shutdown();

    // Locate yt-dlp executable on the system (executable dir, build folder, PATH, Winget)
    [[nodiscard]] static std::string findYtDlp();

    // Locate JavaScript runtime on the system (Deno, Node.js, Bun) for yt-dlp cipher/n-sig solving
    [[nodiscard]] static std::string findJsRuntime();

    // Map UI quality label ("1080p", "720p", "480p", "360p", "Auto", "Audio Only") to yt-dlp format selector
    [[nodiscard]] static std::string qualityToFormatString(std::string_view quality, bool allowAv1 = false);

    // Map UI quality label to yt-dlp format sort parameter (-S)
    [[nodiscard]] static std::string qualityToFormatSort(std::string_view quality);

    // Extract video ID from videoIdOrUrl (strips shorts prefixes, extracts from youtube.com, youtu.be, etc.)
    [[nodiscard]] static std::string extractVideoId(std::string_view videoIdOrUrl);

    // Extract format itag from stream URL (?itag=137, &itag=137, /itag/137/)
    [[nodiscard]] static std::string extractItag(std::string_view url);

    // Parse URL expiration timestamp from googlevideo expire= query parameter
    [[nodiscard]] static std::chrono::steady_clock::time_point parseUrlExpiry(std::string_view url);

    // Invalidate stream URL cache
    static void clearCache();

    // Check if yt-dlp error output indicates authentication / login is required
    [[nodiscard]] static bool isAuthRequiredError(std::string_view err) noexcept;

    // Helper to build yt-dlp process command line argument list
    [[nodiscard]] static std::vector<std::string>
    buildYtDlpArgs(const std::string &ytdlpPath, const std::string &url, const std::string &format, bool useCookies,
                   std::string_view cookieFilePath, std::string_view extractorArgs, std::string_view formatSort = "");
};
