#pragma once

#include "IMpvBackend.h"
#include "core/StreamResolver.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

enum class PlaybackState : uint8_t { Idle, Resolving, Buffering, Playing, Paused, Ended, Error };

struct PlayerPlaybackStats {
    int videoWidth = 0;
    int videoHeight = 0;
    double fps = 0.0;
    int64_t droppedFrames = 0;
    std::string videoCodec;
    std::string audioCodec;
    std::string colorMatrix;
    std::string colorPrimaries;
    int64_t videoBitrate = 0;
    int64_t audioBitrate = 0;
    double bufferSeconds = 0.0;
    int64_t cacheSpeedBytes = 0;
};

struct PlayerEvents {
    std::function<void(PlaybackState)> onState;
    std::function<void(double position, double duration)> onTime;
    std::function<void(bool isPaused)> onPause;
    std::function<void(double cachedSeconds)> onCache;
    std::function<void(const std::string &message)> onError;
};

class PlayerController {
public:
    PlayerController();
    ~PlayerController();

    PlayerController(const PlayerController &) = delete;
    PlayerController &operator=(const PlayerController &) = delete;
    PlayerController(PlayerController &&) = delete;
    PlayerController &operator=(PlayerController &&) = delete;

    // Attach event callbacks (thread-safe, protects against callback reassignment data race)
    void setEvents(PlayerEvents events);

    // Initialize player and embed into native window handle (HWND on Windows, Window ID on Linux)
    [[nodiscard]] bool initialize(int64_t windowId);

    // Direct stream playback
    void playDirectStream(std::string_view videoUrl, std::string_view audioUrl = "", double startPosition = 0.0);

    // Playback controls
    void togglePause();
    void pause();
    void resume();
    void stop();
    void seekRelative(double seconds);
    void seekAbsolute(double seconds);

    // Volume (0 - 100)
    void setVolume(int volumePercent);
    [[nodiscard]] int getVolume() const noexcept { return m_volume.load(); }

    // Playback speed (0.25 - 4.0, default 1.0)
    void setPlaybackSpeed(double speed);
    [[nodiscard]] double getPlaybackSpeed() const noexcept { return m_playbackSpeed.load(); }

    // Position and duration in seconds (event-cached, non-blocking)
    [[nodiscard]] double getPosition() const noexcept { return m_position.load(); }
    [[nodiscard]] double getDuration() const noexcept { return m_duration.load(); }
    [[nodiscard]] bool isPaused() const noexcept { return m_isPaused.load(); }

    // Playback state
    [[nodiscard]] PlaybackState getState() const noexcept { return m_state.load(); }
    void setState(PlaybackState state);

    // Status
    [[nodiscard]] bool isInitialized() const noexcept;
    [[nodiscard]] std::string getInitError() const;
    [[nodiscard]] mpv_handle *getMpvHandle() const noexcept;
    [[nodiscard]] bool hasRenderContext() const noexcept;
    [[nodiscard]] PlayerPlaybackStats getPlaybackStats() const;

    // Helper to map UI quality string to yt-dlp format string
    [[nodiscard]] static std::string qualityToFormatString(std::string_view quality, bool allowAv1 = false) {
        return StreamResolver::qualityToFormatString(quality, allowAv1);
    }

    // Anti-throttling request headers for googlevideo CDN streams
    [[nodiscard]] static constexpr std::string_view getAntiThrottlingHeaders() noexcept {
        return "Origin: https://www.youtube.com\r\nReferer: https://www.youtube.com/\r\n";
    }

    // Anti-throttling responsive streaming buffer configuration
    struct StreamingBufferOptions {
        const char *cache = "yes";
        const char *cachePauseInitial = "yes";
        const char *cachePauseWait = "2";
        const char *cacheSecs = "120";
        const char *demuxerMaxBytes = "150MiB";
        const char *demuxerMaxBackBytes = "50MiB";
        const char *demuxerReadaheadSecs = "60";
        const char *demuxerCacheWait = "no";
        const char *streamBufferSize = "2MiB";
        const char *curlMaxRequestSize = "10485760";
        const char *streamLavfOpts =
            "request_size=10485760,multiple_requests=1,reconnect=1,reconnect_streamed=1,reconnect_delay_max=4";
    };
    [[nodiscard]] static constexpr StreamingBufferOptions getStreamingBufferOptions() noexcept {
        return StreamingBufferOptions{};
    }

    // Thread-safe notification dispatch helpers called by backends
    void notifyState(PlaybackState state);
    void notifyTime(double position, double duration);
    void notifyPause(bool isPaused);
    void notifyCache(double cachedSeconds);
    void notifyError(const std::string &message);

private:
    std::unique_ptr<IMpvBackend> m_backend;
    mutable std::mutex m_eventsMutex;
    PlayerEvents m_events;

    int64_t m_windowId = 0;
    mutable std::mutex m_errorMutex;
    std::string m_initError;

    std::atomic<PlaybackState> m_state{PlaybackState::Idle};
    std::atomic<double> m_position{0.0};
    std::atomic<double> m_duration{0.0};
    std::atomic<bool> m_isPaused{false};
    std::atomic<int> m_volume{100};
    std::atomic<double> m_playbackSpeed{1.0};

    std::atomic<double> m_lastDispatchedPos{-1.0};
    std::atomic<double> m_lastDispatchedDur{-1.0};
    std::atomic<int64_t> m_lastDispatchMs{0};
};
