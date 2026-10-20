#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#endif

namespace lightyt::utils {

// Trims leading and trailing whitespace characters (space, tab, \r, \n)
[[nodiscard]] constexpr std::string_view trim(std::string_view str) noexcept {
    auto start = str.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) {
        return {};
    }
    auto end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

// Case-insensitive prefix matching
[[nodiscard]] inline bool startsWithIgnoreCase(std::string_view str, std::string_view prefix) noexcept {
    if (str.size() < prefix.size()) {
        return false;
    }
    return std::ranges::equal(str.substr(0, prefix.size()), prefix, [](char c1, char c2) noexcept {
        return std::tolower(static_cast<unsigned char>(c1)) == std::tolower(static_cast<unsigned char>(c2));
    });
}

// Case-insensitive string equality
[[nodiscard]] inline bool equalsIgnoreCase(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    return std::ranges::equal(a, b, [](char c1, char c2) noexcept {
        return std::tolower(static_cast<unsigned char>(c1)) == std::tolower(static_cast<unsigned char>(c2));
    });
}

// Appends or replaces a URL query parameter (?key=val or &key=val)
inline void appendOrReplaceUrlParam(std::string &url, std::string_view key, std::string_view val) {
    std::string keyPrefix = std::format("{}=", key);
    auto pos = url.find(keyPrefix);
    if (pos != std::string::npos && (pos == 0 || url[pos - 1] == '?' || url[pos - 1] == '&')) {
        auto endPos = url.find('&', pos);
        if (endPos == std::string::npos) {
            url.replace(pos, url.size() - pos, std::format("{}={}", key, val));
        } else {
            url.replace(pos, endPos - pos, std::format("{}={}", key, val));
        }
        return;
    }
    char sep = (url.find('?') == std::string::npos) ? '?' : '&';
    url += sep;
    url.append(key);
    url += '=';
    url.append(val);
}

// Strips InnerTube Shorts prefixes (shorts-shelf-item-, shorts-item-, shorts-) from a video ID
[[nodiscard]] constexpr std::string_view stripShortsPrefix(std::string_view id) noexcept {
    if (id.starts_with("shorts-shelf-item-")) {
        return id.substr(18);
    }
    if (id.starts_with("shorts-item-")) {
        return id.substr(12);
    }
    if (id.starts_with("shorts-")) {
        return id.substr(7);
    }
    return id;
}

// Converts channel avatar URL to JPEG format (-rj)
[[nodiscard]] inline std::string convertAvatarUrlToJpeg(std::string url) {
    if (url.empty()) {
        return url;
    }
    if (url.starts_with("//")) {
        url = "https:" + url;
    }
    if (url.find("ggpht.com") != std::string::npos || url.find("googleusercontent.com") != std::string::npos) {
        auto rw = url.find("-rw");
        if (rw != std::string::npos) {
            url.replace(rw, 3, "-rj");
        }
    }
    return url;
}

// Converts channel avatar URL to WebP format (-rw)
[[nodiscard]] inline std::string convertAvatarUrlToWebp(std::string url) {
    if (url.empty()) {
        return url;
    }
    if (url.starts_with("//")) {
        url = "https:" + url;
    }
    if (url.find("ggpht.com") != std::string::npos || url.find("googleusercontent.com") != std::string::npos) {
        auto rj = url.find("-rj");
        if (rj != std::string::npos) {
            url.replace(rj, 3, "-rw");
        }
    }
    return url;
}

// Converts a video thumbnail URL to a JPEG fallback URL
[[nodiscard]] inline std::string convertThumbnailUrlToJpeg(std::string url, std::string_view videoId = {}) {
    if (url.empty()) {
        if (!videoId.empty()) {
            return "https://i.ytimg.com/vi/" + std::string(videoId) + "/hqdefault.jpg";
        }
        return url;
    }
    if (url.starts_with("//")) {
        url = "https:" + url;
    }
    auto posWebpDir = url.find("/vi_webp/");
    if (posWebpDir != std::string::npos) {
        url.replace(posWebpDir, 9, "/vi/");
    }
    auto posExt = url.rfind(".webp");
    if (posExt != std::string::npos) {
        url.replace(posExt, 5, ".jpg");
    }
    return url;
}

// Normalizes YouTube channel avatar URLs (adds https: to protocol-relative URLs; preserves WebP when preferWebp=true,
// else converts to JPEG)
[[nodiscard]] inline std::string normalizeAvatarUrl(std::string url, bool preferWebp = false) {
    if (url.empty()) {
        return url;
    }
    if (url.starts_with("//")) {
        url = "https:" + url;
    }
    if (preferWebp) {
        return convertAvatarUrlToWebp(std::move(url));
    }
    return convertAvatarUrlToJpeg(std::move(url));
}

#ifdef _WIN32
// Converts UTF-8 string_view to Windows wide std::wstring (UTF-16)
[[nodiscard]] inline std::wstring toWString(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (len <= 0) {
        return {};
    }
    std::wstring w(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), w.data(), len);
    return w;
}

// Converts Windows wide std::wstring (UTF-16) to UTF-8 std::string
[[nodiscard]] inline std::string toUtf8(const std::wstring &wstr) {
    if (wstr.empty()) {
        return {};
    }
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        return {};
    }
    std::string str(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), str.data(), len, nullptr, nullptr);
    return str;
}

// Converts std::filesystem::path to UTF-8 std::string cleanly
[[nodiscard]] inline std::string pathToUtf8(const std::filesystem::path &p) {
    return toUtf8(p.wstring());
}

// Constructs std::filesystem::path from UTF-8 string_view cleanly on Windows
[[nodiscard]] inline std::filesystem::path u8ToPath(std::string_view u8Str) {
    return {toWString(u8Str)};
}
#else
[[nodiscard]] inline std::string pathToUtf8(const std::filesystem::path &p) {
    return p.string();
}

[[nodiscard]] inline std::filesystem::path u8ToPath(std::string_view u8Str) {
    return {u8Str};
}
#endif

}  // namespace lightyt::utils
