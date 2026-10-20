#include "CurlApi.h"

#include "utils/AppPaths.h"
#include "utils/Logger.h"

#include <algorithm>
#include <array>
#include <filesystem>

#ifdef _WIN32
#include "utils/StringUtils.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#endif

namespace lightyt {

CurlApi &getCurlApi() {
    static auto *s_instance = new CurlApi();
    return *s_instance;
}

bool CurlApi::load() {
    if (isLoaded()) {
        return true;
    }

#ifdef _WIN32
    constexpr std::array<const char *, 4> libNames = {"libcurl-x64.dll", "libcurl.dll", "libcurl-4.dll", "curl.dll"};

    // 1. Check bundled lib/ directory first
    for (const char *name : libNames) {
        auto candidate = paths::findCandidateFile(name);
        if (!candidate.empty() && lib.load(candidate.string())) {
            LOG_INFO("[CurlApi] Loaded bundled libcurl from {}", candidate.string());
            break;
        }
    }

    // 2. Fall back to system path
    if (!lib.isLoaded()) {
        for (const char *name : libNames) {
            std::wstring wName = utils::toWString(name);
            std::array<wchar_t, MAX_PATH> searchBuf{};
            if (SearchPathW(nullptr, wName.c_str(), nullptr, static_cast<DWORD>(searchBuf.size()), searchBuf.data(),
                            nullptr) > 0) {
                std::filesystem::path p(searchBuf.data());
                if (lib.load(p.string())) {
                    LOG_INFO("[CurlApi] Loaded system libcurl from SearchPath: {}", p.string());
                    break;
                }
            }
            if (lib.load(name)) {
                LOG_INFO("[CurlApi] Loaded system libcurl by name: {}", name);
                break;
            }
        }
    }
#else
    constexpr std::array<const char *, 4> libNames = {"libcurl.so.4", "libcurl.so", "libcurl.so.3",
                                                      "libcurl-gnutls.so.4"};

    // 1. Check bundled lib/ directory first
    for (const char *name : libNames) {
        auto candidate = paths::findCandidateFile(name);
        if (!candidate.empty() && lib.load(candidate.string())) {
            LOG_INFO("[CurlApi] Loaded bundled libcurl from {}", candidate.string());
            break;
        }
    }

    // 2. Fall back to system path
    if (!lib.isLoaded()) {
        const std::array<const char *, 6> systemCandidates = {"libcurl.so.4",
                                                              "libcurl.so",
                                                              "/lib/x86_64-linux-gnu/libcurl.so.4",
                                                              "/usr/lib/x86_64-linux-gnu/libcurl.so.4",
                                                              "/usr/lib64/libcurl.so.4",
                                                              "/usr/lib/libcurl.so.4"};
        for (const char *cand : systemCandidates) {
            if (lib.load(cand)) {
                LOG_INFO("[CurlApi] Loaded system libcurl from {}", cand);
                break;
            }
        }
    }
#endif

    if (!lib.isLoaded()) {
        LOG_WARN("[CurlApi] Failed to locate or load libcurl from lib/ or system PATH.");
        return false;
    }

    // Resolve symbols
    global_init = lib.getSymbol<CURLcode (*)(long)>("curl_global_init");
    easy_init = lib.getSymbol<CURL *(*)()>("curl_easy_init");
    easy_cleanup = lib.getSymbol<void (*)(CURL *)>("curl_easy_cleanup");
    easy_setopt = lib.getSymbol<CURLcode (*)(CURL *, CURLoption, ...)>("curl_easy_setopt");
    easy_perform = lib.getSymbol<CURLcode (*)(CURL *)>("curl_easy_perform");
    easy_strerror = lib.getSymbol<const char *(*)(CURLcode)>("curl_easy_strerror");
    easy_getinfo = lib.getSymbol<CURLcode (*)(CURL *, CURLINFO, ...)>("curl_easy_getinfo");
    slist_append = lib.getSymbol<struct curl_slist *(*)(struct curl_slist *, const char *)>("curl_slist_append");
    slist_free_all = lib.getSymbol<void (*)(struct curl_slist *)>("curl_slist_free_all");

    if (!global_init || !easy_init || !easy_cleanup || !easy_setopt || !easy_perform || !easy_strerror ||
        !easy_getinfo || !slist_append || !slist_free_all) {
        LOG_ERROR("[CurlApi] One or more required libcurl symbols failed to resolve.");
        lib.unload();
        return false;
    }

    return true;
}

}  // namespace lightyt
