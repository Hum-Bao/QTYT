#include "CookieManager.h"

#include "HttpClient.h"
#include "utils/AppPaths.h"
#include "utils/StringUtils.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstring>
#include <ctime>
#include <format>
#include <fstream>
#include <mutex>
#include <sstream>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace {
// Self-contained RFC 3174 SHA-1 implementation (zero external library dependency)
std::string computeSha1Hex(std::string_view input) {
    uint32_t h0 = 0x67452301;
    uint32_t h1 = 0xEFCDAB89;
    uint32_t h2 = 0x98BADCFE;
    uint32_t h3 = 0x10325476;
    uint32_t h4 = 0xC3D2E1F0;

    size_t paddedLen = ((input.size() + 8) / 64 + 1) * 64;
    std::array<uint8_t, 256> stackBuf{};
    std::vector<uint8_t> heapBuf;
    uint8_t *msg = nullptr;

    if (paddedLen <= stackBuf.size()) {
        msg = stackBuf.data();
    } else {
        heapBuf.resize(paddedLen, 0);
        msg = heapBuf.data();
    }

    std::memcpy(msg, input.data(), input.size());
    msg[input.size()] = 0x80;
    uint64_t bitLen = static_cast<uint64_t>(input.size()) * 8;
    for (size_t i = 0; i < 8; ++i) {
        msg[paddedLen - 8 + i] = static_cast<uint8_t>((bitLen >> ((7 - i) * 8)) & 0xFF);
    }

    for (size_t chunk = 0; chunk < paddedLen; chunk += 64) {
        std::array<uint32_t, 80> w{};
        for (size_t i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(msg[chunk + i * 4]) << 24) |
                   (static_cast<uint32_t>(msg[chunk + i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(msg[chunk + i * 4 + 2]) << 8) |
                   (static_cast<uint32_t>(msg[chunk + i * 4 + 3]));
        }
        for (size_t i = 16; i < 80; ++i) {
            uint32_t temp = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
            w[i] = std::rotl(temp, 1);
        }

        uint32_t a = h0;
        uint32_t b = h1;
        uint32_t c = h2;
        uint32_t d = h3;
        uint32_t e = h4;

        for (size_t i = 0; i < 80; ++i) {
            uint32_t f = 0;
            uint32_t k = 0;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            uint32_t temp = std::rotl(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = std::rotl(b, 30);
            b = a;
            a = temp;
        }
        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    return std::format("{:08x}{:08x}{:08x}{:08x}{:08x}", h0, h1, h2, h3, h4);
}

using lightyt::utils::startsWithIgnoreCase;
using lightyt::utils::trim;

bool isCompanionSessionCookie(std::string_view name) noexcept {
    return name == "SIDCC" || name == "__Secure-1PSIDCC" || name == "__Secure-3PSIDCC" ||
           name == "__Secure-1PSIDTS" || name == "__Secure-3PSIDTS" || name == "__Secure-1PSIDRTS" ||
           name == "__Secure-3PSIDRTS" || name == "LOGIN_INFO" ||
           name == "PREF" || name == "YSC" || name == "VISITOR_INFO1_LIVE";
}

int64_t parseHttpDate(std::string_view dateStr) {
    if (dateStr.empty()) {
        return 0;
    }
    static constexpr std::array<std::string_view, 12> kMonths = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                                 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    int monthIdx = -1;
    size_t monthPos = std::string_view::npos;
    for (size_t i = 0; i < 12; ++i) {
        auto pos = dateStr.find(kMonths[i]);
        if (pos != std::string_view::npos) {
            monthIdx = static_cast<int>(i);
            monthPos = pos;
            break;
        }
    }
    if (monthIdx < 0 || monthPos == 0) {
        return 0;
    }

    // Parse day before month
    int day = 0;
    size_t dayEnd = dateStr.find_last_not_of(" -", monthPos - 1);
    if (dayEnd != std::string_view::npos) {
        size_t dayStart = dateStr.find_last_of(" ,", dayEnd);
        dayStart = (dayStart == std::string_view::npos) ? 0 : dayStart + 1;
        std::from_chars(dateStr.data() + dayStart, dateStr.data() + dayEnd + 1, day);
    }

    // Parse year after month
    int year = 0;
    size_t yearStart = dateStr.find_first_not_of(" -", monthPos + 3);
    if (yearStart != std::string_view::npos) {
        size_t yearEnd = dateStr.find_first_of(" \t\r\n", yearStart);
        size_t len = (yearEnd == std::string_view::npos) ? dateStr.size() - yearStart : yearEnd - yearStart;
        std::from_chars(dateStr.data() + yearStart, dateStr.data() + yearStart + len, year);
        if (year > 0 && year < 100) {
            year += 2000;
        }
    }

    // Parse hour:min:sec
    int hour = 0;
    int min = 0;
    int sec = 0;
    auto colonPos = dateStr.find(':');
    if (colonPos != std::string_view::npos && colonPos >= 2) {
        std::from_chars(dateStr.data() + colonPos - 2, dateStr.data() + colonPos, hour);
        std::from_chars(dateStr.data() + colonPos + 1, dateStr.data() + colonPos + 3, min);
        if (colonPos + 4 < dateStr.size() && dateStr[colonPos + 3] == ':') {
            std::from_chars(dateStr.data() + colonPos + 4, dateStr.data() + colonPos + 6, sec);
        }
    }

    if (day <= 0 || year <= 0) {
        return 0;
    }

    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = monthIdx;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = min;
    tm.tm_sec = sec;

#ifdef _WIN32
    time_t t = _mkgmtime(&tm);
#else
    time_t t = timegm(&tm);
#endif
    return (t > 0) ? static_cast<int64_t>(t) : 1;
}
}  // namespace

void CookieManager::indexCookieLocked(const CookieEntry &entry) {
    auto &vec = m_domainsByName[entry.name];
    if (std::find(vec.begin(), vec.end(), entry.domain) == vec.end()) {
        vec.push_back(entry.domain);
    }
}

void CookieManager::rebuildIndexLocked() {
    m_domainsByName.clear();
    for (const auto &[key, entry] : m_cookies) {
        indexCookieLocked(entry);
    }
}

CookieManager::CookieManager(const CookieManager &other) {
    std::shared_lock lock(other.m_mutex);
    m_cookies = other.m_cookies;
    m_domainsByName = other.m_domainsByName;
    m_cookieFilePath = other.m_cookieFilePath;
    m_dirty.store(other.m_dirty.load(std::memory_order_relaxed), std::memory_order_relaxed);
    m_lastAutoSave = other.m_lastAutoSave;
}

CookieManager &CookieManager::operator=(const CookieManager &other) {
    if (this != &other) {
        std::scoped_lock lock(m_mutex, other.m_mutex);
        m_cookies = other.m_cookies;
        m_domainsByName = other.m_domainsByName;
        m_cookieFilePath = other.m_cookieFilePath;
        m_dirty.store(other.m_dirty.load(std::memory_order_relaxed), std::memory_order_relaxed);
        m_lastAutoSave = other.m_lastAutoSave;
    }
    return *this;
}

CookieManager::CookieManager(CookieManager &&other) noexcept {
    std::unique_lock lock(other.m_mutex);
    m_cookies = std::move(other.m_cookies);
    m_domainsByName = std::move(other.m_domainsByName);
    m_cookieFilePath = std::move(other.m_cookieFilePath);
    m_dirty.store(other.m_dirty.load(std::memory_order_relaxed), std::memory_order_relaxed);
    m_lastAutoSave = other.m_lastAutoSave;
    other.m_dirty.store(false, std::memory_order_relaxed);
}

CookieManager &CookieManager::operator=(CookieManager &&other) noexcept {
    if (this != &other) {
        std::scoped_lock lock(m_mutex, other.m_mutex);
        m_cookies = std::move(other.m_cookies);
        m_domainsByName = std::move(other.m_domainsByName);
        m_cookieFilePath = std::move(other.m_cookieFilePath);
        m_dirty.store(other.m_dirty.load(std::memory_order_relaxed), std::memory_order_relaxed);
        m_lastAutoSave = other.m_lastAutoSave;
        other.m_dirty.store(false, std::memory_order_relaxed);
    }
    return *this;
}

bool CookieManager::domainMatches(std::string_view cookieDomain, std::string_view targetHost) noexcept {
    if (cookieDomain.empty() || targetHost.empty()) {
        return false;
    }

    std::string_view cleanCookieDomain = cookieDomain;
    if (cleanCookieDomain.starts_with('.')) {
        cleanCookieDomain.remove_prefix(1);
    }

    if (targetHost == cleanCookieDomain) {
        return true;
    }

    if (targetHost.ends_with(cleanCookieDomain)) {
        size_t prefixLen = targetHost.size() - cleanCookieDomain.size();
        if (prefixLen > 0 && targetHost[prefixLen - 1] == '.') {
            return true;
        }
    }

    // Strict Google -> YouTube authorization sharing
    if ((targetHost == "www.youtube.com" || targetHost == "youtube.com") &&
        (cleanCookieDomain == "google.com" || cleanCookieDomain.ends_with(".google.com"))) {
        return true;
    }

    return false;
}

bool CookieManager::loadFromFile(const std::filesystem::path &filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    bool ok = loadFromString(content);
    if (ok) {
        std::unique_lock lock(m_mutex);
        m_cookieFilePath = lightyt::utils::pathToUtf8(filePath);
        m_dirty.store(false, std::memory_order_release);
    }
    return ok;
}

bool CookieManager::loadFromString(std::string_view cookieContent) {
    std::vector<CookieEntry> parsedList;
    size_t start = 0;

    while (start < cookieContent.size()) {
        size_t nlPos = cookieContent.find('\n', start);
        std::string_view line = (nlPos == std::string_view::npos) ? cookieContent.substr(start)
                                                                  : cookieContent.substr(start, nlPos - start);

        std::string_view trimmed = trim(line);
        if (!trimmed.empty()) {
            if (trimmed.find('\t') != std::string_view::npos) {
                parseNetscapeLine(trimmed, parsedList);
            } else if (trimmed.find('=') != std::string_view::npos && !trimmed.starts_with('#')) {
                parseHeaderString(trimmed, parsedList);
            }
        }

        if (nlPos == std::string_view::npos) {
            break;
        }
        start = nlPos + 1;
    }

    if (parsedList.empty()) {
        return false;
    }

    std::unique_lock lock(m_mutex);
    m_cookies.clear();
    for (auto &c : parsedList) {
        std::string_view d = c.domain;
        if (d.find("youtube.com") == std::string_view::npos && d.find("google.com") == std::string_view::npos) {
            continue;
        }
        m_cookies[{c.domain, c.name}] = std::move(c);
    }
    rebuildIndexLocked();
    return true;
}

void CookieManager::parseNetscapeLine(std::string_view line, std::vector<CookieEntry> &outCookies) {
    std::string_view cleanLine = line;
    bool isHttpOnly = false;
    if (cleanLine.starts_with("#HttpOnly_")) {
        cleanLine.remove_prefix(10);
        isHttpOnly = true;
    } else if (cleanLine.starts_with('#')) {
        return;
    }

    size_t start = 0;
    std::array<std::string_view, 6> parts{};
    bool valid = true;
    for (size_t i = 0; i < 6; ++i) {
        size_t tabPos = cleanLine.find('\t', start);
        if (tabPos == std::string_view::npos) {
            valid = false;
            break;
        }
        parts[i] = cleanLine.substr(start, tabPos - start);
        start = tabPos + 1;
    }

    if (valid) {
        CookieEntry entry;
        entry.domain = std::string(trim(parts[0]));
        entry.path = std::string(trim(parts[2]));
        entry.secure = (parts[3] == "TRUE" || parts[3] == "true");
        try {
            entry.expires = std::stoll(std::string(trim(parts[4])));
        } catch (...) {
            entry.expires = 0;
        }
        entry.name = std::string(trim(parts[5]));
        std::string_view val = trim(cleanLine.substr(start));
        if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
            val = val.substr(1, val.size() - 2);
        }
        entry.value = std::string(val);
        entry.httpOnly = isHttpOnly;

        if (!entry.name.empty()) {
            outCookies.push_back(std::move(entry));
        }
    }
}

void CookieManager::parseHeaderString(std::string_view content, std::vector<CookieEntry> &outCookies) {
    size_t start = 0;
    while (start < content.size()) {
        size_t semiPos = content.find(';', start);
        std::string_view token =
            (semiPos == std::string_view::npos) ? content.substr(start) : content.substr(start, semiPos - start);

        auto eqPos = token.find('=');
        if (eqPos != std::string_view::npos) {
            std::string_view name = trim(token.substr(0, eqPos));
            std::string_view value = trim(token.substr(eqPos + 1));
            if (!name.empty()) {
                if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
                    value = value.substr(1, value.size() - 2);
                }
                CookieEntry entry;
                entry.name = std::string(name);
                entry.value = std::string(value);
                entry.domain = ".youtube.com";
                entry.path = "/";
                entry.expires = 0;
                outCookies.push_back(std::move(entry));
            }
        }

        if (semiPos == std::string_view::npos) {
            break;
        }
        start = semiPos + 1;
    }
}

bool CookieManager::saveToFile(const std::filesystem::path &filePath) const {
    std::shared_lock lock(m_mutex);
    if (filePath.has_parent_path()) {
        std::error_code dirEc;
        std::filesystem::create_directories(filePath.parent_path(), dirEc);
    }
    std::filesystem::path tempPath = filePath;
    tempPath += ".tmp";
    std::ofstream file(tempPath, std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }

    std::error_code permEc;
    std::filesystem::permissions(tempPath, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace, permEc);
#ifndef _WIN32
    chmod(tempPath.c_str(), S_IRUSR | S_IWUSR);
#endif

    file << "# Netscape HTTP Cookie File\n";
    file << "# http://curl.haxx.se/rfc/cookie_spec.html\n";
    file << "# This file was generated by LightYT. Edit at your own risk.\n\n";

    int64_t now =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    for (const auto &[key, entry] : m_cookies) {
        // Only save Google and YouTube cookies to prevent foreign domain corruption in yt-dlp / mpv
        std::string_view d = entry.domain;
        if (d.find("youtube.com") == std::string_view::npos && d.find("google.com") == std::string_view::npos) {
            continue;
        }

        std::string domain = entry.domain;
        if (!domain.starts_with('.') &&
            (domain.find("youtube.com") != std::string::npos || domain.find("google.com") != std::string::npos)) {
            domain.insert(0, 1, '.');
        }
        bool isDomainSpecified = domain.starts_with('.');

        int64_t exp = (entry.expires > 0) ? entry.expires : (now + 315360000);  // 10 years fallback
        if (entry.httpOnly) {
            file << "#HttpOnly_";
        }
        file << domain << "\t" << (isDomainSpecified ? "TRUE" : "FALSE") << "\t" << entry.path << "\t"
             << (entry.secure ? "TRUE" : "FALSE") << "\t" << exp << "\t" << entry.name << "\t" << entry.value << "\n";
    }

    file.close();
    if (!file.good()) {
        return false;
    }

    std::error_code ec;
    std::filesystem::remove(filePath, ec);
    ec.clear();
    std::filesystem::rename(tempPath, filePath, ec);
    if (ec) {
        std::filesystem::copy_file(tempPath, filePath, std::filesystem::copy_options::overwrite_existing, ec);
        std::error_code ec2;
        std::filesystem::remove(tempPath, ec2);
    }
    if (!ec) {
        std::filesystem::permissions(filePath, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                     std::filesystem::perm_options::replace, ec);
#ifndef _WIN32
        chmod(filePath.c_str(), S_IRUSR | S_IWUSR);
#endif
    }
    if (!ec) {
        m_dirty.store(false, std::memory_order_release);
    }
    return !ec;
}

std::string CookieManager::sha1Hex(std::string_view input) {
    return computeSha1Hex(input);
}

std::string CookieManager::getSapisidHash(std::string_view origin) const {
    std::string sapisid = getCookie("SAPISID");
    if (sapisid.empty()) {
        sapisid = getCookie("__Secure-3PAPISID");
    }
    if (sapisid.empty()) {
        sapisid = getCookie("__Secure-1PAPISID");
    }
    if (sapisid.empty()) {
        return {};
    }

    if (sapisid.size() >= 2 && sapisid.front() == '"' && sapisid.back() == '"') {
        sapisid = sapisid.substr(1, sapisid.size() - 2);
    }

    int64_t timestamp =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    std::string payload = std::format("{} {} {}", timestamp, sapisid, origin);
    std::string hash = computeSha1Hex(payload);
    return std::format("SAPISIDHASH {}_{}", timestamp, hash);
}

bool CookieManager::isMasterAuthActiveLocked(int64_t now) const noexcept {
    static constexpr std::array<std::string_view, 3> kMasterAuthKeys = {"SAPISID", "__Secure-3PAPISID", "SID"};
    return std::ranges::any_of(kMasterAuthKeys, [&](std::string_view authKey) {
        auto it = m_domainsByName.find(std::string(authKey));
        if (it == m_domainsByName.end()) {
            return false;
        }
        return std::ranges::any_of(it->second, [&](const auto &domain) {
            auto cookieIt = m_cookies.find({domain, std::string(authKey)});
            return cookieIt != m_cookies.end() && (cookieIt->second.expires == 0 || cookieIt->second.expires > now);
        });
    });
}

std::string CookieManager::getCookieHeaderValue(std::string_view targetHost) const {
    std::shared_lock lock(m_mutex);
    int64_t now =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    bool masterAuthActive = isMasterAuthActiveLocked(now);

    // Deduplicate by cookie name: if both .youtube.com and .google.com cookies exist for a name,
    // prioritize .youtube.com to ensure exact alignment with getSapisidHash()
    std::map<std::string, const CookieEntry *> selected;
    for (const auto &[key, entry] : m_cookies) {
        bool matches = domainMatches(entry.domain, targetHost);
        if (!matches &&
            (targetHost == "youtube.com" || targetHost.ends_with(".youtube.com") || targetHost == "www.youtube.com")) {
            matches = domainMatches(entry.domain, "google.com") || domainMatches(entry.domain, ".google.com");
        }
        if (!matches) {
            continue;
        }
        if (entry.expires > 0 && entry.expires <= now) {
            // Keep companion session cookies alive as long as master session is active
            if (!masterAuthActive || !isCompanionSessionCookie(entry.name)) {
                continue;
            }
        }

        auto it = selected.find(entry.name);
        if (it == selected.end()) {
            selected[entry.name] = &entry;
        } else {
            if (entry.domain.find("youtube.com") != std::string::npos &&
                it->second->domain.find("youtube.com") == std::string::npos) {
                it->second = &entry;
            }
        }
    }

    std::string result;
    bool first = true;
    for (const auto &[name, entryPtr] : selected) {
        if (!first) {
            result += "; ";
        }
        result += entryPtr->name;
        result += '=';
        result += entryPtr->value;
        first = false;
    }
    return result;
}

std::string CookieManager::getCookieHeaderForUrl(std::string_view urlOrHost) const {
    std::string_view host = urlOrHost;
    auto protoPos = host.find("://");
    if (protoPos != std::string_view::npos) {
        host = host.substr(protoPos + 3);
    }
    auto slashPos = host.find('/');
    if (slashPos != std::string_view::npos) {
        host = host.substr(0, slashPos);
    }
    auto colonPos = host.find(':');
    if (colonPos != std::string_view::npos) {
        host = host.substr(0, colonPos);
    }
    return getCookieHeaderValue(host.empty() ? "www.youtube.com" : host);
}

bool CookieManager::hasAuth() const noexcept {
    std::shared_lock lock(m_mutex);
    int64_t now =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    static constexpr std::array<std::string_view, 4> kAuthKeys = {"SAPISID", "__Secure-3PAPISID", "__Secure-1PAPISID",
                                                                  "LOGIN_INFO"};
    return std::ranges::any_of(kAuthKeys, [&](std::string_view authKey) {
        auto it = m_domainsByName.find(std::string(authKey));
        if (it == m_domainsByName.end()) {
            return false;
        }
        return std::ranges::any_of(it->second, [&](const auto &domain) {
            auto cookieIt = m_cookies.find({domain, std::string(authKey)});
            return cookieIt != m_cookies.end() && (cookieIt->second.expires == 0 || cookieIt->second.expires > now);
        });
    });
}

std::string CookieManager::getCookie(std::string_view name) const {
    std::shared_lock lock(m_mutex);
    int64_t now =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    bool masterAuthActive = isMasterAuthActiveLocked(now);

    auto isExpired = [&](const CookieEntry &entry) {
        if (entry.expires > 0 && entry.expires <= now) {
            return !masterAuthActive || !isCompanionSessionCookie(entry.name);
        }
        return false;
    };

    auto it = m_domainsByName.find(std::string(name));
    if (it == m_domainsByName.end()) {
        return {};
    }

    const CookieEntry *fallback = nullptr;
    for (const auto &domain : it->second) {
        auto cookieIt = m_cookies.find({domain, std::string(name)});
        if (cookieIt != m_cookies.end()) {
            if (isExpired(cookieIt->second)) {
                continue;
            }
            if (cookieIt->second.domain.find("youtube.com") != std::string::npos) {
                return cookieIt->second.value;
            }
            if (!fallback) {
                fallback = &cookieIt->second;
            }
        }
    }

    if (fallback) {
        return fallback->value;
    }

    return {};
}

void CookieManager::setCookie(std::string_view name, std::string_view value, std::string_view domain) {
    std::unique_lock lock(m_mutex);
    CookieEntry entry;
    entry.name = std::string(name);
    entry.value = std::string(value);
    entry.domain = std::string(domain);
    entry.path = "/";
    entry.expires = 0;
    indexCookieLocked(entry);
    m_cookies[{entry.domain, entry.name}] = std::move(entry);
    m_dirty.store(true, std::memory_order_release);
}

void CookieManager::updateFromSetCookie(std::string_view setCookieHeader, std::string_view defaultDomain) {
    std::string_view cleanHeader = trim(setCookieHeader);
    if (cleanHeader.empty()) {
        return;
    }

    size_t firstSemi = cleanHeader.find(';');
    std::string_view nameValToken =
        (firstSemi == std::string_view::npos) ? cleanHeader : cleanHeader.substr(0, firstSemi);
    auto eqPos = nameValToken.find('=');
    if (eqPos == std::string_view::npos) {
        return;
    }

    std::string_view name = trim(nameValToken.substr(0, eqPos));
    std::string_view val = trim(nameValToken.substr(eqPos + 1));
    if (name.empty()) {
        return;
    }
    if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
        val = val.substr(1, val.size() - 2);
    }

    CookieEntry entry;
    entry.name = std::string(name);
    entry.value = std::string(val);
    entry.domain = std::string(defaultDomain);
    entry.path = "/";
    entry.expires = 0;
    entry.secure = false;
    entry.httpOnly = false;

    size_t start = (firstSemi == std::string_view::npos) ? cleanHeader.size() : firstSemi + 1;
    while (start < cleanHeader.size()) {
        size_t nextSemi = cleanHeader.find(';', start);
        std::string_view attrToken = (nextSemi == std::string_view::npos) ? cleanHeader.substr(start)
                                                                          : cleanHeader.substr(start, nextSemi - start);
        attrToken = trim(attrToken);
        if (!attrToken.empty()) {
            auto attrEq = attrToken.find('=');
            if (attrEq != std::string_view::npos) {
                std::string_view attrName = trim(attrToken.substr(0, attrEq));
                std::string_view attrVal = trim(attrToken.substr(attrEq + 1));
                if (startsWithIgnoreCase(attrName, "domain")) {
                    if (!attrVal.empty() && !attrVal.starts_with('.')) {
                        entry.domain = "." + std::string(attrVal);
                    } else {
                        entry.domain = std::string(attrVal);
                    }
                } else if (startsWithIgnoreCase(attrName, "path")) {
                    entry.path = std::string(attrVal);
                } else if (startsWithIgnoreCase(attrName, "max-age")) {
                    int64_t maxAge = 0;
                    auto [ptr, ec] = std::from_chars(attrVal.data(), attrVal.data() + attrVal.size(), maxAge);
                    if (ec == std::errc{}) {
                        int64_t nowSec = std::chrono::duration_cast<std::chrono::seconds>(
                                             std::chrono::system_clock::now().time_since_epoch())
                                             .count();
                        entry.expires = nowSec + maxAge;
                    }
                } else if (startsWithIgnoreCase(attrName, "expires")) {
                    if (entry.expires == 0) {
                        int64_t exp = parseHttpDate(attrVal);
                        if (exp > 0) {
                            entry.expires = exp;
                        }
                    }
                }
            } else {
                if (startsWithIgnoreCase(attrToken, "secure")) {
                    entry.secure = true;
                } else if (startsWithIgnoreCase(attrToken, "httponly")) {
                    entry.httpOnly = true;
                }
            }
        }
        if (nextSemi == std::string_view::npos) {
            break;
        }
        start = nextSemi + 1;
    }

    {
        std::unique_lock lock(m_mutex);
        indexCookieLocked(entry);
        m_cookies[{entry.domain, entry.name}] = std::move(entry);
        m_dirty.store(true, std::memory_order_release);
    }
}

void CookieManager::updateFromResponse(const HttpResponse &response) {
    if (response.setCookies.empty()) {
        return;
    }

    for (const auto &sc : response.setCookies) {
        updateFromSetCookie(sc);
    }

    autoSaveIfChanged();
}

void CookieManager::autoSaveIfChanged() const {
    std::string path;
    bool shouldSave = false;
    {
        if (!m_dirty.load(std::memory_order_relaxed)) {
            return;
        }
        std::unique_lock lock(m_mutex);
        if (!m_dirty.load(std::memory_order_relaxed) || m_cookieFilePath.empty()) {
            return;
        }
        auto now = std::chrono::steady_clock::now();
        if (m_lastAutoSave.time_since_epoch().count() == 0 || now - m_lastAutoSave >= std::chrono::seconds(10)) {
            path = m_cookieFilePath;
            shouldSave = true;
            m_lastAutoSave = now;
            m_dirty.store(false, std::memory_order_release);
        }
    }
    if (shouldSave && !path.empty()) {
        if (!saveToFile(lightyt::utils::u8ToPath(path))) {
            m_dirty.store(true, std::memory_order_release);
        }
    }
}

void CookieManager::clear() noexcept {
    std::unique_lock lock(m_mutex);
    m_cookies.clear();
    m_domainsByName.clear();
    m_dirty.store(true, std::memory_order_release);
}

std::string CookieManager::getCookieFilePath() const {
    std::shared_lock lock(m_mutex);
    return m_cookieFilePath;
}

void CookieManager::setCookieFilePath(std::string_view path) {
    std::unique_lock lock(m_mutex);
    m_cookieFilePath = std::string(path);
}

bool CookieManager::saveToCanonicalAppData() {
    auto appData = lightyt::paths::getAppDataDir();
    lightyt::paths::ensureDirExists(appData);
    auto targetPath = appData / "cookies.txt";
    if (!saveToFile(targetPath)) {
        return false;
    }
    setCookieFilePath(lightyt::utils::pathToUtf8(targetPath));
    return true;
}
