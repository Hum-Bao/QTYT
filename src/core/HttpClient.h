#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

inline constexpr const char *DEFAULT_USER_AGENT =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36";

struct HttpResponse {
    int statusCode = 0;
    std::string body;
    std::string error;
    std::vector<std::string> setCookies;

    [[nodiscard]] constexpr bool isOk() const noexcept {
        return statusCode >= 200 && statusCode < 300 && error.empty();
    }
};

class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    HttpClient(const HttpClient &) = delete;
    HttpClient &operator=(const HttpClient &) = delete;
    HttpClient(HttpClient &&) noexcept;
    HttpClient &operator=(HttpClient &&) noexcept;

    // Thread-local client for zero-overhead connection reuse
    [[nodiscard]] static HttpClient &threadLocal();

    // Instance methods with cooperative cancellation support
    [[nodiscard]] HttpResponse sendPostJson(std::string_view url, std::string_view jsonBody,
                                            const std::vector<std::string> &headers = {}, long timeoutSeconds = 15,
                                            const std::atomic<bool> *cancelToken = nullptr);

    [[nodiscard]] HttpResponse sendGet(std::string_view url, const std::vector<std::string> &headers = {},
                                       long timeoutSeconds = 15, const std::atomic<bool> *cancelToken = nullptr);

    [[nodiscard]] bool fetchBytes(std::string_view url, std::vector<uint8_t> &outBuffer, long timeoutSeconds = 10,
                                  size_t maxBytes = 5ULL * 1024 * 1024, const std::atomic<bool> *cancelToken = nullptr);

    // Static convenience wrappers (forward to threadLocal())
    [[nodiscard]] static HttpResponse postJson(std::string_view url, std::string_view jsonBody,
                                               const std::vector<std::string> &headers = {}, long timeoutSeconds = 15,
                                               const std::atomic<bool> *cancelToken = nullptr) {
        return threadLocal().sendPostJson(url, jsonBody, headers, timeoutSeconds, cancelToken);
    }

    [[nodiscard]] static HttpResponse get(std::string_view url, const std::vector<std::string> &headers = {},
                                          long timeoutSeconds = 15, const std::atomic<bool> *cancelToken = nullptr) {
        return threadLocal().sendGet(url, headers, timeoutSeconds, cancelToken);
    }

    [[nodiscard]] static bool downloadBytes(std::string_view url, std::vector<uint8_t> &outBuffer,
                                            long timeoutSeconds = 10, size_t maxBytes = 5ULL * 1024 * 1024,
                                            const std::atomic<bool> *cancelToken = nullptr) {
        return threadLocal().fetchBytes(url, outBuffer, timeoutSeconds, maxBytes, cancelToken);
    }

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
