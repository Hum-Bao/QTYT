#pragma once

#include "utils/DynamicLibrary.h"

#include <curl/curl.h>

namespace lightyt {

struct CurlApi {
    CURLcode (*global_init)(long flags) = nullptr;
    CURL *(*easy_init)() = nullptr;
    void (*easy_cleanup)(CURL *handle) = nullptr;
    CURLcode (*easy_setopt)(CURL *handle, CURLoption option, ...) = nullptr;
    CURLcode (*easy_perform)(CURL *handle) = nullptr;
    const char *(*easy_strerror)(CURLcode) = nullptr;
    CURLcode (*easy_getinfo)(CURL *handle, CURLINFO info, ...) = nullptr;
    struct curl_slist *(*slist_append)(struct curl_slist *list, const char *string) = nullptr;
    void (*slist_free_all)(struct curl_slist *list) = nullptr;

    DynamicLibrary lib;

    [[nodiscard]] bool load();
    [[nodiscard]] bool isLoaded() const noexcept { return lib.isLoaded() && easy_init != nullptr; }
};

[[nodiscard]] CurlApi &getCurlApi();

}  // namespace lightyt
