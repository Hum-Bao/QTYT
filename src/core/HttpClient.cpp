#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#endif

#include "CurlApi.h"
#include "HttpClient.h"
#include "utils/Logger.h"
#include "utils/StringUtils.h"

#include <curl/curl.h>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace {
using lightyt::utils::startsWithIgnoreCase;

struct HttpPerformOptions {
    std::string method;
    std::string url;
    std::string_view body;
    std::vector<std::string> headers;
    long timeoutSeconds = 15;
    size_t maxBytes = 10ULL * 1024 * 1024;
    const std::atomic<bool> *cancelToken = nullptr;
};

void ensureCurlGlobalInit() {
    static std::once_flag s_initOnce;
    std::call_once(s_initOnce, []() {
        if (lightyt::getCurlApi().load()) {
            lightyt::getCurlApi().global_init(CURL_GLOBAL_DEFAULT);
        } else {
            LOG_ERROR("[HttpClient] Could not load libcurl dynamic library from lib/ or system PATH.");
        }
    });
}

struct CurlHandleDeleter {
    void operator()(CURL *curl) const noexcept {
        if (curl != nullptr && lightyt::getCurlApi().isLoaded()) {
            lightyt::getCurlApi().easy_cleanup(curl);
        }
    }
};

struct CurlSlistDeleter {
    void operator()(curl_slist *list) const noexcept {
        if (list != nullptr && lightyt::getCurlApi().isLoaded()) {
            lightyt::getCurlApi().slist_free_all(list);
        }
    }
};

using ScopedCurl = std::unique_ptr<CURL, CurlHandleDeleter>;
using ScopedCurlSlist = std::unique_ptr<curl_slist, CurlSlistDeleter>;

struct WriteSink {
    std::string *stringBuf = nullptr;
    std::vector<uint8_t> *byteBuf = nullptr;
    size_t maxBytes = 10ULL * 1024 * 1024;
    bool exceeded = false;
};

size_t writeCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t totalBytes = size * nmemb;
    auto *sink = static_cast<WriteSink *>(userp);
    if (sink == nullptr) {
        return 0;
    }

    size_t currentSize = 0;
    if (sink->stringBuf != nullptr) {
        currentSize = sink->stringBuf->size();
    } else if (sink->byteBuf != nullptr) {
        currentSize = sink->byteBuf->size();
    }
    if (currentSize + totalBytes > sink->maxBytes) {
        sink->exceeded = true;
        return 0;
    }

    if (sink->stringBuf != nullptr) {
        sink->stringBuf->append(static_cast<const char *>(contents), totalBytes);
    } else if (sink->byteBuf != nullptr) {
        const auto *bytes = static_cast<const uint8_t *>(contents);
        sink->byteBuf->insert(sink->byteBuf->end(), bytes, bytes + totalBytes);
    }
    return totalBytes;
}

struct ProgressContext {
    const std::atomic<bool> *cancelToken = nullptr;
};

int xferInfoCallback(void *clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    auto *ctx = static_cast<ProgressContext *>(clientp);
    if (ctx != nullptr && ctx->cancelToken != nullptr && ctx->cancelToken->load()) {
        return 1;
    }
    return 0;
}

size_t headerCallback(char *buffer, size_t size, size_t nitems, void *userdata) {
    size_t totalBytes = size * nitems;
    auto *cookies = static_cast<std::vector<std::string> *>(userdata);
    if (cookies == nullptr || buffer == nullptr) {
        return totalBytes;
    }
    std::string_view line(buffer, totalBytes);
    constexpr std::string_view prefix = "Set-Cookie:";
    if (startsWithIgnoreCase(line, prefix)) {
        std::string_view cookieVal = line.substr(prefix.size());
        while (!cookieVal.empty() && (cookieVal.front() == ' ' || cookieVal.front() == '\t')) {
            cookieVal.remove_prefix(1);
        }
        while (!cookieVal.empty() &&
               (cookieVal.back() == '\r' || cookieVal.back() == '\n' || cookieVal.back() == ' ')) {
            cookieVal.remove_suffix(1);
        }
        if (!cookieVal.empty()) {
            cookies->emplace_back(cookieVal);
        }
    }
    return totalBytes;
}
}  // namespace

struct HttpClient::Impl {
    ScopedCurl curl;

    Impl() : curl(lightyt::getCurlApi().isLoaded() ? lightyt::getCurlApi().easy_init() : nullptr) {
        if (curl) {
            auto &api = lightyt::getCurlApi();
            api.easy_setopt(curl.get(), CURLOPT_ACCEPT_ENCODING, "");
            api.easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
            api.easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
            api.easy_setopt(curl.get(), CURLOPT_MAXREDIRS, 5L);
            api.easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 10L);
            api.easy_setopt(curl.get(), CURLOPT_USERAGENT, DEFAULT_USER_AGENT);
        }
    }

    HttpResponse perform(const HttpPerformOptions &opts, std::vector<uint8_t> *byteSink = nullptr) const {
        HttpResponse response;
        if (!curl || !lightyt::getCurlApi().isLoaded()) {
            response.error = "CURL context unavailable (libcurl library not loaded)";
            return response;
        }

        auto &api = lightyt::getCurlApi();

        std::string currentUrl = opts.url;
        if (currentUrl.starts_with("//")) {
            currentUrl = "https:" + currentUrl;
        }

        CURL *h = curl.get();

        curl_slist *rawHeaders = nullptr;
        if (opts.method == "POST") {
            rawHeaders = api.slist_append(rawHeaders, "Content-Type: application/json");
        }

        for (const auto &hdr : opts.headers) {
            if (startsWithIgnoreCase(hdr, "user-agent:")) {
                continue;
            }
            if (opts.method == "POST" && startsWithIgnoreCase(hdr, "content-type:")) {
                continue;
            }
            rawHeaders = api.slist_append(rawHeaders, hdr.c_str());
        }
        ScopedCurlSlist headerList(rawHeaders);

        WriteSink sink{
            .stringBuf = (byteSink == nullptr) ? &response.body : nullptr,
            .byteBuf = byteSink,
            .maxBytes = opts.maxBytes,
            .exceeded = false,
        };

        ProgressContext progCtx{.cancelToken = opts.cancelToken};

        api.easy_setopt(h, CURLOPT_URL, currentUrl.c_str());
        api.easy_setopt(h, CURLOPT_HTTPHEADER, headerList.get());
        api.easy_setopt(h, CURLOPT_WRITEFUNCTION, writeCallback);
        api.easy_setopt(h, CURLOPT_WRITEDATA, &sink);
        api.easy_setopt(h, CURLOPT_TIMEOUT, opts.timeoutSeconds);
        api.easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 10L);

        api.easy_setopt(h, CURLOPT_NOPROGRESS, 0L);
        api.easy_setopt(h, CURLOPT_XFERINFOFUNCTION, xferInfoCallback);
        api.easy_setopt(h, CURLOPT_XFERINFODATA, &progCtx);
        api.easy_setopt(h, CURLOPT_HEADERFUNCTION, headerCallback);
        api.easy_setopt(h, CURLOPT_HEADERDATA, &response.setCookies);

        if (opts.method == "POST") {
            api.easy_setopt(h, CURLOPT_POST, 1L);
            // Zero string copying: point directly to body data
            api.easy_setopt(h, CURLOPT_POSTFIELDS, opts.body.data());
            api.easy_setopt(h, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(opts.body.size()));
        } else {
            api.easy_setopt(h, CURLOPT_HTTPGET, 1L);
        }

        CURLcode res = api.easy_perform(h);

        api.easy_setopt(h, CURLOPT_NOPROGRESS, 1L);
        api.easy_setopt(h, CURLOPT_XFERINFOFUNCTION, nullptr);
        api.easy_setopt(h, CURLOPT_HEADERFUNCTION, nullptr);
        api.easy_setopt(h, CURLOPT_HEADERDATA, nullptr);
        api.easy_setopt(h, CURLOPT_HTTPHEADER, nullptr);
        if (opts.method == "POST") {
            api.easy_setopt(h, CURLOPT_POSTFIELDS, nullptr);
        }

        if (res == CURLE_ABORTED_BY_CALLBACK) {
            response.error = "Request cancelled";
            return response;
        }
        if (sink.exceeded) {
            response.error = "Response exceeded maxBytes limit";
            return response;
        }
        if (res != CURLE_OK) {
            response.error = api.easy_strerror(res);
            return response;
        }

        long httpCode = 0;
        api.easy_getinfo(h, CURLINFO_RESPONSE_CODE, &httpCode);
        response.statusCode = static_cast<int>(httpCode);
        return response;
    }
};

HttpClient::HttpClient() {
    ensureCurlGlobalInit();
    m_impl = std::make_unique<Impl>();
}

HttpClient::~HttpClient() = default;
HttpClient::HttpClient(HttpClient &&) noexcept = default;
HttpClient &HttpClient::operator=(HttpClient &&) noexcept = default;

HttpClient &HttpClient::threadLocal() {
    thread_local HttpClient instance;
    return instance;
}

HttpResponse HttpClient::sendPostJson(std::string_view url, std::string_view jsonBody,
                                      const std::vector<std::string> &headers, long timeoutSeconds,
                                      const std::atomic<bool> *cancelToken) {
    if (!m_impl)
        return HttpResponse{.statusCode = 0, .body = "", .error = "HttpClient not initialized", .setCookies = {}};
    return m_impl->perform(HttpPerformOptions{
        .method = "POST",
        .url = std::string(url),
        .body = jsonBody,
        .headers = headers,
        .timeoutSeconds = timeoutSeconds,
        .maxBytes = 10ULL * 1024 * 1024,
        .cancelToken = cancelToken,
    });
}

HttpResponse HttpClient::sendGet(std::string_view url, const std::vector<std::string> &headers, long timeoutSeconds,
                                 const std::atomic<bool> *cancelToken) {
    if (!m_impl)
        return HttpResponse{.statusCode = 0, .body = "", .error = "HttpClient not initialized", .setCookies = {}};
    return m_impl->perform(HttpPerformOptions{
        .method = "GET",
        .url = std::string(url),
        .body = {},
        .headers = headers,
        .timeoutSeconds = timeoutSeconds,
        .maxBytes = 10ULL * 1024 * 1024,
        .cancelToken = cancelToken,
    });
}

bool HttpClient::fetchBytes(std::string_view url, std::vector<uint8_t> &outBuffer, long timeoutSeconds, size_t maxBytes,
                            const std::atomic<bool> *cancelToken) {
    if (!m_impl)
        return false;
    outBuffer.clear();
    HttpResponse res = m_impl->perform(
        HttpPerformOptions{
            .method = "GET",
            .url = std::string(url),
            .body = {},
            .headers = {"Accept: image/jpeg,image/png,image/*;q=0.8"},
            .timeoutSeconds = timeoutSeconds,
            .maxBytes = maxBytes,
            .cancelToken = cancelToken,
        },
        &outBuffer);
    return res.isOk() && !outBuffer.empty();
}
