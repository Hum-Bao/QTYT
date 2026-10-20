#pragma once

#include "CookieManager.h"
#include "HttpClient.h"

#include <atomic>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace lightyt::innertube {

inline constexpr std::string_view API_KEY = "AIzaSyAO_FJ2SlqU8Q4STEHLGCilw_Y9_11qcW8";

[[nodiscard]] inline std::string makeApiUrl(std::string_view endpoint) {
    return std::format("https://www.youtube.com/youtubei/v1/{}?key={}&prettyPrint=false", endpoint, API_KEY);
}

[[nodiscard]] inline HttpResponse post(const std::string &url, const std::string &body,
                                       const std::vector<std::string> &headers, CookieManager *cookieManager = nullptr,
                                       int timeoutSec = 10, const std::atomic<bool> *cancelToken = nullptr) {
    HttpResponse res = HttpClient::postJson(url, body, headers, timeoutSec, cancelToken);
    if (cookieManager != nullptr) {
        cookieManager->updateFromResponse(res);
    }
    return res;
}

[[nodiscard]] inline HttpResponse get(const std::string &url, const std::vector<std::string> &headers,
                                      CookieManager *cookieManager = nullptr, int timeoutSec = 10,
                                      const std::atomic<bool> *cancelToken = nullptr) {
    HttpResponse res = HttpClient::get(url, headers, timeoutSec, cancelToken);
    if (cookieManager != nullptr) {
        cookieManager->updateFromResponse(res);
    }
    return res;
}

}  // namespace lightyt::innertube
