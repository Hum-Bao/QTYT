#include "LibMpvBackend.h"

#include "PlayerController.h"
#include "core/HttpClient.h"
#include "utils/Logger.h"

#include <algorithm>

namespace lightyt {

LibMpvBackend::LibMpvBackend(PlayerController *controller) : m_controller(controller) {}

LibMpvBackend::~LibMpvBackend() {
    stopInternal();
}

bool LibMpvBackend::isRunning() const {
    return m_running.load() && m_mpv != nullptr;
}

bool LibMpvBackend::hasRenderContext() const noexcept {
    return getMpvApi().hasRenderContext();
}

bool LibMpvBackend::start(int64_t windowId) {
    auto &api = getMpvApi();
    if (!api.load())
        return false;

    m_mpv = api.create();
    if (m_mpv == nullptr)
        return false;

    if (windowId != 0 && api.set_option != nullptr) {
        api.set_option(m_mpv, "wid", MPV_FORMAT_INT64, &windowId);
    }

    // Apply optimized streaming buffer and cache options
    if (api.set_option_string != nullptr) {
        api.set_option_string(m_mpv, "hwdec", "auto-safe");
        api.set_option_string(m_mpv, "vd-lavc-dr", "yes");
        if (windowId != 0) {
            api.set_option_string(m_mpv, "vo", "gpu");
        } else {
            api.set_option_string(m_mpv, "vo", "libmpv");
        }
        api.set_option_string(m_mpv, "scale", "spline36");
        api.set_option_string(m_mpv, "cscale", "spline36");
        api.set_option_string(m_mpv, "dscale", "mitchell");
        api.set_option_string(m_mpv, "keep-open", "yes");
        api.set_option_string(m_mpv, "terminal", "no");
        api.set_option_string(m_mpv, "msg-level", "all=warn");
        api.set_option_string(m_mpv, "ytdl", "no");
        api.set_option_string(m_mpv, "config", "no");
        api.set_option_string(m_mpv, "load-scripts", "no");
        api.set_option_string(m_mpv, "osc", "no");
        api.set_option_string(m_mpv, "osd-level", "0");
        api.set_option_string(m_mpv, "osd-bar", "no");
        api.set_option_string(m_mpv, "script-opts", "osc-idle_screen=no");

        // User-Agent, Referer, and Origin headers prevent YouTube CDN bandwidth throttling
        api.set_option_string(m_mpv, "user-agent", DEFAULT_USER_AGENT);
        api.set_option_string(m_mpv, "referrer", "https://www.youtube.com/");
        api.set_option_string(m_mpv, "http-header-fields", "Origin: https://www.youtube.com");
        // Decision 21: Direct googlevideo.com URLs are pre-signed; passing browser session
        // cookies causes signature mismatches and 403 Forbidden errors on subsequent chunks.

        // Anti-throttling responsive streaming buffer and cache settings
        auto bufOpts = PlayerController::getStreamingBufferOptions();
        api.set_option_string(m_mpv, "cache", bufOpts.cache);
        api.set_option_string(m_mpv, "cache-pause-initial", bufOpts.cachePauseInitial);
        api.set_option_string(m_mpv, "cache-pause-wait", bufOpts.cachePauseWait);
        api.set_option_string(m_mpv, "cache-secs", bufOpts.cacheSecs);
        api.set_option_string(m_mpv, "demuxer-max-bytes", bufOpts.demuxerMaxBytes);
        api.set_option_string(m_mpv, "demuxer-max-back-bytes", bufOpts.demuxerMaxBackBytes);
        api.set_option_string(m_mpv, "demuxer-readahead-secs", bufOpts.demuxerReadaheadSecs);
        api.set_option_string(m_mpv, "demuxer-cache-wait", bufOpts.demuxerCacheWait);
        api.set_option_string(m_mpv, "force-seekable", "yes");
        api.set_option_string(m_mpv, "stream-buffer-size", bufOpts.streamBufferSize);
        api.set_option_string(m_mpv, "demuxer-donate-buffer", "yes");
    }

    if (api.request_log_messages != nullptr) {
        api.request_log_messages(m_mpv, "warn");
    }

    if (api.initialize(m_mpv) < 0) {
        api.destroy(m_mpv);
        m_mpv = nullptr;
        return false;
    }

    if (m_controller) {
        setPropertyDouble("volume", static_cast<double>(m_controller->getVolume()));
    }

    // Observe playback properties
    if (api.observe_property != nullptr) {
        api.observe_property(m_mpv, 1, "time-pos", MPV_FORMAT_DOUBLE);
        api.observe_property(m_mpv, 2, "duration", MPV_FORMAT_DOUBLE);
        api.observe_property(m_mpv, 3, "pause", MPV_FORMAT_FLAG);
        api.observe_property(m_mpv, 4, "paused-for-cache", MPV_FORMAT_FLAG);
        api.observe_property(m_mpv, 5, "demuxer-cache-duration", MPV_FORMAT_DOUBLE);
        api.observe_property(m_mpv, 6, "eof-reached", MPV_FORMAT_FLAG);
    }

    m_running = true;
    m_eventThread = std::thread([this]() { eventLoop(); });
    LOG_INFO("[PlayerController] Initialized using in-process libmpv.");
    return true;
}

void LibMpvBackend::stop() {
    {
        std::lock_guard lock(m_audioMutex);
        m_pendingAudioUrl.clear();
    }
    auto &api = getMpvApi();
    if (m_mpv != nullptr && api.command != nullptr) {
        const char *cmd[] = {"stop", nullptr};
        api.command(m_mpv, cmd);
    }
}

void LibMpvBackend::loadFile(const std::string &url, const std::string &audioUrl, double startPos) {
    if (m_mpv == nullptr)
        return;

    auto &api = getMpvApi();
    // mpv 0.38+ compatibility: set the 'start' property before loadfile
    if (api.set_property_string != nullptr) {
        if (startPos > 0.0) {
            api.set_property_string(m_mpv, "start", std::to_string(startPos).c_str());
        } else {
            api.set_property_string(m_mpv, "start", "none");
        }
    }

    if (!audioUrl.empty()) {
        {
            std::lock_guard lock(m_audioMutex);
            m_pendingAudioUrl.clear();
        }
        // Use mpv length-prefixed option syntax (%len%val) so commas in googlevideo URLs are not split
        std::string opt = "audio-file=%" + std::to_string(audioUrl.size()) + "%" + audioUrl;
        const char *cmd[] = {"loadfile", url.c_str(), "replace", "-1", opt.c_str(), nullptr};
        int ret = -1;
        if (api.command != nullptr) {
            ret = api.command(m_mpv, cmd);
            if (ret < 0) {
                // mpv < 0.38 compatibility: 4th argument is options directly (without index)
                const char *legacyCmd[] = {"loadfile", url.c_str(), "replace", opt.c_str(), nullptr};
                ret = api.command(m_mpv, legacyCmd);
            }
        }
        if (ret < 0) {
            LOG_WARN("[PlayerController] loadfile with audio-file option returned {}, falling back to audio-add", ret);
            std::lock_guard lock(m_audioMutex);
            m_pendingAudioUrl = audioUrl;
            const char *fallbackCmd[] = {"loadfile", url.c_str(), "replace", nullptr};
            if (api.command != nullptr) {
                api.command(m_mpv, fallbackCmd);
            }
        }
    } else {
        {
            std::lock_guard lock(m_audioMutex);
            m_pendingAudioUrl.clear();
        }
        const char *cmd[] = {"loadfile", url.c_str(), "replace", nullptr};
        if (api.command != nullptr) {
            api.command(m_mpv, cmd);
        }
    }
}

void LibMpvBackend::command(const std::vector<std::string> &args) {
    auto &api = getMpvApi();
    if (m_mpv == nullptr || api.command == nullptr)
        return;
    std::vector<const char *> cArgs;
    cArgs.reserve(args.size() + 1);
    std::ranges::transform(args, std::back_inserter(cArgs), [](const auto &a) { return a.c_str(); });
    cArgs.push_back(nullptr);
    api.command(m_mpv, cArgs.data());
}

void LibMpvBackend::setPropertyString(std::string_view name, std::string_view val) {
    auto &api = getMpvApi();
    if (m_mpv != nullptr && api.set_property_string != nullptr) {
        api.set_property_string(m_mpv, std::string(name).c_str(), std::string(val).c_str());
    }
}

void LibMpvBackend::setPropertyDouble(std::string_view name, double val) {
    auto &api = getMpvApi();
    if (m_mpv != nullptr && api.set_property != nullptr) {
        api.set_property(m_mpv, std::string(name).c_str(), MPV_FORMAT_DOUBLE, &val);
    }
}

void LibMpvBackend::setPropertyFlag(std::string_view name, bool val) {
    auto &api = getMpvApi();
    if (m_mpv != nullptr && api.set_property != nullptr) {
        int flag = val ? 1 : 0;
        api.set_property(m_mpv, std::string(name).c_str(), MPV_FORMAT_FLAG, &flag);
    }
}

void LibMpvBackend::setVolume(int volume) {
    setPropertyDouble("volume", static_cast<double>(volume));
}

void LibMpvBackend::seek(double seconds, bool relative) {
    std::vector<std::string> args = {"seek", std::to_string(seconds), relative ? "relative" : "absolute"};
    command(args);
}

std::string LibMpvBackend::getPropertyString(std::string_view name) const {
    auto &api = getMpvApi();
    if (m_mpv == nullptr || api.get_property == nullptr) {
        return {};
    }
    char *str = nullptr;
    if (api.get_property(m_mpv, std::string(name).c_str(), MPV_FORMAT_STRING, &str) >= 0 && str != nullptr) {
        std::string result(str);
        if (api.mpv_free != nullptr) {
            api.mpv_free(str);
        }
        return result;
    }
    return {};
}

double LibMpvBackend::getPropertyDouble(std::string_view name, double defaultVal) const {
    auto &api = getMpvApi();
    if (m_mpv == nullptr || api.get_property == nullptr) {
        return defaultVal;
    }
    double val = defaultVal;
    if (api.get_property(m_mpv, std::string(name).c_str(), MPV_FORMAT_DOUBLE, &val) >= 0) {
        return val;
    }
    return defaultVal;
}

int64_t LibMpvBackend::getPropertyInt(std::string_view name, int64_t defaultVal) const {
    auto &api = getMpvApi();
    if (m_mpv == nullptr || api.get_property == nullptr) {
        return defaultVal;
    }
    int64_t val = defaultVal;
    if (api.get_property(m_mpv, std::string(name).c_str(), MPV_FORMAT_INT64, &val) >= 0) {
        return val;
    }
    return defaultVal;
}

void LibMpvBackend::attachPendingAudio() {
    std::string audio;
    {
        std::lock_guard lock(m_audioMutex);
        audio = std::move(m_pendingAudioUrl);
        m_pendingAudioUrl.clear();
    }
    auto &api = getMpvApi();
    if (!audio.empty() && m_mpv != nullptr && (api.command_async != nullptr || api.command != nullptr)) {
        LOG_INFO("[PlayerController] Media loaded. Attaching audio stream...");
        const char *audioArgs[] = {"audio-add", audio.c_str(), "select", nullptr};
        if (api.command_async != nullptr) {
            api.command_async(m_mpv, 0, audioArgs);
        } else {
            int ret = api.command(m_mpv, audioArgs);
            if (ret < 0) {
                const char *errStr = (api.error_string != nullptr) ? api.error_string(ret) : "unknown";
                LOG_WARN("[PlayerController] audio-add failed: {}", errStr);
            } else {
                LOG_INFO("[PlayerController] Audio stream attached successfully.");
            }
        }
    }
}

void LibMpvBackend::stopInternal() {
    m_running = false;
    {
        std::lock_guard lock(m_audioMutex);
        m_pendingAudioUrl.clear();
    }
    auto &api = getMpvApi();
    if (m_mpv != nullptr) {
        if (api.command != nullptr) {
            const char *cmd[] = {"stop", nullptr};
            api.command(m_mpv, cmd);
        }
        // Wake up blocking mpv_wait_event(-1) immediately
        if (api.wakeup != nullptr) {
            api.wakeup(m_mpv);
        }
    }
    if (m_eventThread.joinable()) {
        m_eventThread.join();
    }
    if (m_mpv != nullptr) {
        if (api.terminate_destroy != nullptr) {
            api.terminate_destroy(m_mpv);
        } else if (api.destroy != nullptr) {
            api.destroy(m_mpv);
        }
        m_mpv = nullptr;
    }
}

void LibMpvBackend::eventLoop() {
    auto &api = getMpvApi();
    // Blocks indefinitely until an event occurs or mpv_wakeup is triggered (0% idle CPU)
    while (m_running && m_mpv != nullptr && api.wait_event != nullptr) {
        mpv_event *event = api.wait_event(m_mpv, -1.0);
        if (!m_running || event == nullptr || event->event_id == MPV_EVENT_NONE)
            continue;

        switch (event->event_id) {
        case MPV_EVENT_FILE_LOADED: {
            attachPendingAudio();
            if (m_controller && m_controller->getDuration() <= 0.0 && api.get_property != nullptr) {
                double queriedDur = 0.0;
                if (api.get_property(m_mpv, "duration", MPV_FORMAT_DOUBLE, &queriedDur) >= 0 && queriedDur > 0.0) {
                    m_controller->notifyTime(m_controller->getPosition(), queriedDur);
                }
            }
            break;
        }
        case MPV_EVENT_PROPERTY_CHANGE: {
            auto *prop = static_cast<mpv_event_property *>(event->data);
            if (prop == nullptr || prop->name == nullptr)
                break;

            std::string_view name = prop->name;
            if (name == "time-pos" && prop->format == MPV_FORMAT_DOUBLE && prop->data != nullptr) {
                double pos = *static_cast<double *>(prop->data);
                double dur = m_controller->getDuration();
                if (dur <= 0.0 && api.get_property != nullptr) {
                    double queriedDur = 0.0;
                    if (api.get_property(m_mpv, "duration", MPV_FORMAT_DOUBLE, &queriedDur) >= 0 && queriedDur > 0.0) {
                        dur = queriedDur;
                    }
                }
                m_controller->notifyTime(pos, dur);
            } else if (name == "duration" && prop->format == MPV_FORMAT_DOUBLE && prop->data != nullptr) {
                double dur = *static_cast<double *>(prop->data);
                m_controller->notifyTime(m_controller->getPosition(), dur);
            } else if (name == "pause" && prop->format == MPV_FORMAT_FLAG && prop->data != nullptr) {
                bool paused = (*static_cast<int *>(prop->data) != 0);
                m_controller->notifyPause(paused);
            } else if (name == "paused-for-cache" && prop->format == MPV_FORMAT_FLAG && prop->data != nullptr) {
                bool buffering = (*static_cast<int *>(prop->data) != 0);
                if (buffering) {
                    m_controller->setState(PlaybackState::Buffering);
                } else if (!m_controller->isPaused()) {
                    m_controller->setState(PlaybackState::Playing);
                }
            } else if (name == "demuxer-cache-duration" && prop->format == MPV_FORMAT_DOUBLE && prop->data != nullptr) {
                double cached = *static_cast<double *>(prop->data);
                m_controller->notifyCache(cached);
            } else if (name == "eof-reached" && prop->format == MPV_FORMAT_FLAG && prop->data != nullptr) {
                bool eofReached = (*static_cast<int *>(prop->data) != 0);
                if (eofReached) {
                    m_controller->setState(PlaybackState::Ended);
                }
            }
            break;
        }
        case MPV_EVENT_PLAYBACK_RESTART:
            attachPendingAudio();
            if (m_controller) {
                if (m_controller->getDuration() <= 0.0 && api.get_property != nullptr) {
                    double queriedDur = 0.0;
                    if (api.get_property(m_mpv, "duration", MPV_FORMAT_DOUBLE, &queriedDur) >= 0 && queriedDur > 0.0) {
                        m_controller->notifyTime(m_controller->getPosition(), queriedDur);
                    }
                }
                // Only transition to Playing if not currently paused
                if (!m_controller->isPaused()) {
                    m_controller->setState(PlaybackState::Playing);
                }
            }
            break;
        case MPV_EVENT_LOG_MESSAGE: {
            auto *msg = static_cast<mpv_event_log_message *>(event->data);
            if (msg != nullptr && msg->text != nullptr) {
                std::string text = msg->text;
                while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
                    text.pop_back();
                }
                if (msg->log_level <= 20) {  // MPV_LOG_LEVEL_WARN or MPV_LOG_LEVEL_ERROR
                    LOG_WARN("[mpv:{}] {}", msg->prefix ? msg->prefix : "core", text);
                } else {
                    LOG_DEBUG("[mpv:{}] {}", msg->prefix ? msg->prefix : "core", text);
                }
            }
            break;
        }
        case MPV_EVENT_END_FILE: {
            auto *ef = static_cast<mpv_event_end_file *>(event->data);
            if (ef != nullptr) {
                if (ef->reason == MPV_END_FILE_REASON_EOF) {
                    m_controller->setState(PlaybackState::Ended);
                } else if (ef->reason == MPV_END_FILE_REASON_ERROR) {
                    m_controller->setState(PlaybackState::Error);
                    const char *errStr =
                        (api.error_string != nullptr && ef->error < 0) ? api.error_string(ef->error) : "Playback error";
                    LOG_ERROR("[PlayerController] mpv playback error: {} (code: {})", errStr, ef->error);
                    m_controller->notifyError(errStr);
                }
            }
            break;
        }
        default:
            break;
        }
    }
}

}  // namespace lightyt
