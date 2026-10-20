#pragma once

#include "utils/DynamicLibrary.h"

#include <cstdint>
#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>
#include <string>
#include <string_view>

namespace lightyt {

struct MpvApi {
    mpv_handle *(*create)() = nullptr;
    int (*initialize)(mpv_handle *) = nullptr;
    void (*destroy)(mpv_handle *) = nullptr;
    void (*terminate_destroy)(mpv_handle *) = nullptr;
    int (*command)(mpv_handle *, const char **) = nullptr;
    int (*command_async)(mpv_handle *, uint64_t, const char **) = nullptr;
    int (*set_option)(mpv_handle *, const char *, mpv_format, void *) = nullptr;
    int (*set_option_string)(mpv_handle *, const char *, const char *) = nullptr;
    int (*get_property)(mpv_handle *, const char *, mpv_format, void *) = nullptr;
    int (*set_property)(mpv_handle *, const char *, mpv_format, void *) = nullptr;
    int (*set_property_string)(mpv_handle *, const char *, const char *) = nullptr;
    int (*observe_property)(mpv_handle *, uint64_t, const char *, mpv_format) = nullptr;
    mpv_event *(*wait_event)(mpv_handle *, double) = nullptr;
    void (*wakeup)(mpv_handle *) = nullptr;
    void (*set_wakeup_callback)(mpv_handle *, void (*)(void *), void *) = nullptr;
    const char *(*error_string)(int) = nullptr;
    int (*request_log_messages)(mpv_handle *, const char *) = nullptr;

    // mpv_render_context API
    int (*render_context_create)(mpv_render_context **, mpv_handle *, mpv_render_param *) = nullptr;
    void (*render_context_set_update_callback)(mpv_render_context *, mpv_render_update_fn, void *) = nullptr;
    uint64_t (*render_context_update)(mpv_render_context *) = nullptr;
    int (*render_context_render)(mpv_render_context *, mpv_render_param *) = nullptr;
    void (*render_context_report_swap)(mpv_render_context *) = nullptr;
    void (*render_context_free)(mpv_render_context *) = nullptr;

    DynamicLibrary lib;

    [[nodiscard]] bool load();
    [[nodiscard]] bool isLoaded() const noexcept { return lib.isLoaded(); }
    [[nodiscard]] bool hasRenderContext() const noexcept {
        return render_context_create != nullptr && render_context_render != nullptr && render_context_free != nullptr;
    }
};

[[nodiscard]] MpvApi &getMpvApi();

#ifdef _WIN32
[[nodiscard]] std::string findFileInPaths(std::string_view filename);
#endif

}  // namespace lightyt
