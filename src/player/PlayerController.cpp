#include "PlayerController.h"

#include "IpcMpvBackend.h"
#include "LibMpvBackend.h"
#include "utils/Logger.h"

#include <algorithm>
#include <chrono>
#include <cmath>

PlayerController::PlayerController() = default;

PlayerController::~PlayerController() {
    stop();
}

void PlayerController::setEvents(PlayerEvents events) {
    std::lock_guard lock(m_eventsMutex);
    m_events = std::move(events);
}

bool PlayerController::isInitialized() const noexcept {
    return m_backend != nullptr && m_backend->isRunning();
}

std::string PlayerController::getInitError() const {
    std::lock_guard lock(m_errorMutex);
    return m_initError;
}

bool PlayerController::initialize(int64_t windowId) {
    m_windowId = windowId;

    // 1. Try in-process libmpv backend first (mpv-2.dll / libmpv-2.dll / libmpv.so)
    // Instant startup, 0% idle CPU, no IPC latency or child process management
    auto libBackend = std::make_unique<lightyt::LibMpvBackend>(this);
    if (libBackend->start(windowId)) {
        m_backend = std::move(libBackend);
        m_backend->setVolume(m_volume.load());
        std::lock_guard lock(m_errorMutex);
        m_initError.clear();
        return true;
    }

    // 2. Fallback to standalone mpv executable with IPC pipe/socket
    auto ipcBackend = std::make_unique<lightyt::IpcMpvBackend>(this);
    if (ipcBackend->start(windowId)) {
        m_backend = std::move(ipcBackend);
        m_backend->setVolume(m_volume.load());
        std::lock_guard lock(m_errorMutex);
        m_initError.clear();
        return true;
    }

    {
        std::lock_guard lock(m_errorMutex);
        m_initError = "Media player engine (mpv) was not found on your system.\n"
                      "Please install mpv or place mpv-2.dll / mpv.exe in the app directory.";
    }
    LOG_WARN("[PlayerController] {}", getInitError());
    return false;
}

void PlayerController::playDirectStream(std::string_view videoUrl, std::string_view audioUrl, double startPosition) {
    if (!isInitialized()) {
        if (!initialize(m_windowId)) {
            LOG_WARN("[PlayerController] Failed to initialize backend in playDirectStream");
            return;
        }
    }

    m_position.store(startPosition);
    m_duration.store(0.0);
    m_isPaused.store(false);
    m_lastDispatchedPos.store(-1.0);
    m_lastDispatchedDur.store(-1.0);
    notifyState(PlaybackState::Buffering);
    m_backend->setVolume(m_volume.load());
    m_backend->setPropertyDouble("speed", m_playbackSpeed.load());
    m_backend->setPropertyFlag("pause", false);
    m_backend->loadFile(std::string(videoUrl), std::string(audioUrl), startPosition);
}

void PlayerController::togglePause() {
    if (m_backend) {
        m_backend->command({"cycle", "pause"});
    }
}

void PlayerController::pause() {
    if (m_backend) {
        m_backend->setPropertyFlag("pause", true);
    }
}

void PlayerController::resume() {
    if (m_backend) {
        m_backend->setPropertyFlag("pause", false);
    }
}

void PlayerController::stop() {
    notifyState(PlaybackState::Idle);
    m_position.store(0.0);
    m_duration.store(0.0);
    m_lastDispatchedPos.store(-1.0);
    m_lastDispatchedDur.store(-1.0);
    m_playbackSpeed.store(1.0);
    if (m_backend) {
        m_backend->stop();
    }
}

void PlayerController::seekRelative(double seconds) {
    m_lastDispatchedPos.store(-1.0);
    if (m_backend) {
        m_backend->seek(seconds, true);
    }
}

void PlayerController::seekAbsolute(double seconds) {
    m_lastDispatchedPos.store(-1.0);
    if (m_backend) {
        m_backend->seek(seconds, false);
    }
}

void PlayerController::setVolume(int volumePercent) {
    m_volume.store(std::clamp(volumePercent, 0, 100));
    if (m_backend) {
        m_backend->setVolume(m_volume.load());
    }
}

void PlayerController::setPlaybackSpeed(double speed) {
    double clamped = std::clamp(speed, 0.25, 4.0);
    m_playbackSpeed.store(clamped);
    if (m_backend) {
        m_backend->setPropertyDouble("speed", clamped);
    }
}

void PlayerController::setState(PlaybackState state) {
    notifyState(state);
}

void PlayerController::notifyState(PlaybackState state) {
    m_state.store(state);
    std::function<void(PlaybackState)> cb;
    {
        std::lock_guard lock(m_eventsMutex);
        cb = m_events.onState;
    }
    if (cb) {
        cb(state);
    }
}

void PlayerController::notifyTime(double position, double duration) {
    m_position.store(position);
    m_duration.store(duration);

    const auto nowMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
    const double lastPos = m_lastDispatchedPos.load(std::memory_order_relaxed);
    const double lastDur = m_lastDispatchedDur.load(std::memory_order_relaxed);
    const int64_t lastMs = m_lastDispatchMs.load(std::memory_order_relaxed);

    const bool firstOrReset = (lastPos < 0.0);
    const bool durChanged = (std::abs(duration - lastDur) > 1e-3);
    const bool secChanged = (static_cast<int64_t>(std::floor(position)) != static_cast<int64_t>(std::floor(lastPos)));
    const bool elapsedEnough = ((nowMs - lastMs) >= 250);

    if (!firstOrReset && !durChanged && !secChanged && !elapsedEnough) {
        return;
    }

    m_lastDispatchedPos.store(position, std::memory_order_relaxed);
    m_lastDispatchedDur.store(duration, std::memory_order_relaxed);
    m_lastDispatchMs.store(nowMs, std::memory_order_relaxed);

    std::function<void(double, double)> cb;
    {
        std::lock_guard lock(m_eventsMutex);
        cb = m_events.onTime;
    }
    if (cb) {
        cb(position, duration);
    }
}

void PlayerController::notifyPause(bool isPaused) {
    m_isPaused.store(isPaused);
    notifyState(isPaused ? PlaybackState::Paused : PlaybackState::Playing);
    std::function<void(bool)> cb;
    {
        std::lock_guard lock(m_eventsMutex);
        cb = m_events.onPause;
    }
    if (cb) {
        cb(isPaused);
    }
}

void PlayerController::notifyCache(double cachedSeconds) {
    std::function<void(double)> cb;
    {
        std::lock_guard lock(m_eventsMutex);
        cb = m_events.onCache;
    }
    if (cb) {
        cb(cachedSeconds);
    }
}

void PlayerController::notifyError(const std::string &message) {
    LOG_ERROR("[PlayerController] Playback error: {}", message);
    std::function<void(const std::string &)> cb;
    {
        std::lock_guard lock(m_eventsMutex);
        cb = m_events.onError;
    }
    if (cb) {
        cb(message);
    }
}

mpv_handle *PlayerController::getMpvHandle() const noexcept {
    return m_backend ? m_backend->getMpvHandle() : nullptr;
}

bool PlayerController::hasRenderContext() const noexcept {
    return m_backend ? m_backend->hasRenderContext() : false;
}
