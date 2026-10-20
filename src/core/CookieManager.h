#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct CookieEntry {
    std::string name;
    std::string value;
    std::string domain = ".youtube.com";
    std::string path = "/";
    int64_t expires = 0;  // 0 = session cookie, >0 = unix timestamp
    bool secure = false;
    bool httpOnly = false;
};

class CookieManager {
public:
    CookieManager() = default;
    ~CookieManager() = default;

    CookieManager(const CookieManager &other);
    CookieManager &operator=(const CookieManager &other);
    CookieManager(CookieManager &&other) noexcept;
    CookieManager &operator=(CookieManager &&other) noexcept;

    // Load from Netscape cookies.txt or custom cookie file
    [[nodiscard]] bool loadFromFile(const std::filesystem::path &filePath);

    // Load from pasted cookie string (Netscape text or raw Name=Value; strings)
    [[nodiscard]] bool loadFromString(std::string_view cookieContent);

    // Save current cookies to Netscape cookies.txt format atomically
    [[nodiscard]] bool saveToFile(const std::filesystem::path &filePath) const;

    // Save current cookies to canonical AppData cookies.txt and update cookieFilePath
    [[nodiscard]] bool saveToCanonicalAppData();

    // Generates Authorization: SAPISIDHASH <timestamp>_<sha1>
    [[nodiscard]] std::string getSapisidHash(std::string_view origin = "https://www.youtube.com") const;

    // RFC 3174 SHA-1 hex digest calculation
    [[nodiscard]] static std::string sha1Hex(std::string_view input);

    // Checks domain matching according to RFC 6265 and YouTube authorization sharing
    [[nodiscard]] static bool domainMatches(std::string_view cookieDomain, std::string_view targetHost) noexcept;

    // Formatted cookie string for the HTTP Cookie: header (filters to domain and active unexpired cookies)
    [[nodiscard]] std::string getCookieHeaderValue(std::string_view targetHost = "www.youtube.com") const;
    [[nodiscard]] std::string getCookieHeaderForUrl(std::string_view urlOrHost = "www.youtube.com") const;

    // Checks if we have valid login cookies (SAPISID or __Secure-3PAPISID or LOGIN_INFO)
    [[nodiscard]] bool hasAuth() const noexcept;

    // Get cookie value by name (returns empty if not found or expired)
    [[nodiscard]] std::string getCookie(std::string_view name) const;

    // Set cookie value
    void setCookie(std::string_view name, std::string_view value, std::string_view domain = ".youtube.com");

    // Parse and apply a single Set-Cookie header value into the cookie store
    void updateFromSetCookie(std::string_view setCookieHeader, std::string_view defaultDomain = ".youtube.com");

    // Process all Set-Cookie headers from an HttpResponse and update cookie store
    void updateFromResponse(const struct HttpResponse &response);

    // Debounced automatic save to m_cookieFilePath (if configured and dirty)
    void autoSaveIfChanged() const;

    // Clears all loaded cookies
    void clear() noexcept;

    // Returns file path of the saved cookies file, if any
    [[nodiscard]] std::string getCookieFilePath() const;
    void setCookieFilePath(std::string_view path);

private:
    mutable std::shared_mutex m_mutex;
    // Map of (domain, name) -> CookieEntry
    std::map<std::pair<std::string, std::string>, CookieEntry> m_cookies;
    std::unordered_map<std::string, std::vector<std::string>> m_domainsByName;
    std::string m_cookieFilePath;
    mutable std::atomic<bool> m_dirty{false};
    mutable std::chrono::steady_clock::time_point m_lastAutoSave;

    [[nodiscard]] bool isMasterAuthActiveLocked(int64_t now) const noexcept;
    void indexCookieLocked(const CookieEntry &entry);
    void rebuildIndexLocked();

    static void parseNetscapeLine(std::string_view line, std::vector<CookieEntry> &outCookies);
    static void parseHeaderString(std::string_view content, std::vector<CookieEntry> &outCookies);
};
