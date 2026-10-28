#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <vector>
#include <memory>
#include <shared_mutex>
#include <cassert>
#include <cmath>

#include <QApplication>
#include <QCoreApplication>
#include <QThreadPool>
#include <QPointer>
#include <QScrollBar>
#include <QScrollArea>

#include "core/YouTubeClient.h"
#include "core/CookieManager.h"
#include "ui/PlayerControlsOverlay.h"

#define private public
#define protected public
#include "ui/PlayerWidget.h"
#undef private
#undef protected

static int g_passCount = 0;
static int g_failCount = 0;

#define STRESS_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "\n[FAIL] Line " << __LINE__ << ": " << msg << " (" #cond ")" << std::endl; \
            g_failCount++; \
            return; \
        } \
    } while (0)

#define STRESS_TEST_BEGIN(name) \
    std::cout << "[RUN ] " << name << "..." << std::flush; \
    int startFails = g_failCount;

#define STRESS_TEST_END(name) \
    if (g_failCount == startFails) { \
        std::cout << " [PASS]" << std::endl; \
        g_passCount++; \
    }

// ============================================================================
// 1. AsyncClientGuard & Concurrency UAF Stress Harness
// ============================================================================
void testAsyncClientGuardUafRace() {
    STRESS_TEST_BEGIN("testAsyncClientGuardUafRace");

    // We stress-test the exact concurrency model used by PlayerWidget:
    // AsyncClientGuard holds std::shared_mutex and YouTubeClient*
    // Workers acquire std::shared_lock<std::shared_mutex>, check cancelToken and guard->client.
    // Destructor signals cancelToken, acquires std::unique_lock<std::shared_mutex>,
    // nulls guard->client, and immediately DELETES the underlying YouTubeClient object.
    // If any worker accesses deleted memory, a crash or memory corruption occurs.

    constexpr int kIterations = 200;
    constexpr int kWorkerThreads = 8;

    for (int iter = 0; iter < kIterations; ++iter) {
        struct AsyncGuard {
            std::shared_mutex mutex;
            YouTubeClient *client = nullptr;
        };

        auto guard = std::make_shared<AsyncGuard>();
        auto cancelToken = std::make_shared<std::atomic<bool>>(false);
        auto cookieMgr = std::make_shared<CookieManager>();
        auto *rawClient = new YouTubeClient(cookieMgr);
        guard->client = rawClient;

        std::atomic<bool> startWorkers{false};
        std::atomic<int> completedReads{0};
        std::atomic<int> safelyAborted{0};

        std::vector<std::thread> workers;
        workers.reserve(kWorkerThreads);

        for (int w = 0; w < kWorkerThreads; ++w) {
            workers.emplace_back([guard, cancelToken, &startWorkers, &completedReads, &safelyAborted]() {
                while (!startWorkers.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }

                for (int rep = 0; rep < 50; ++rep) {
                    std::shared_lock<std::shared_mutex> lock(guard->mutex);
                    if (!guard->client || cancelToken->load()) {
                        safelyAborted.fetch_add(1, std::memory_order_relaxed);
                        continue;
                    }

                    // Exercise the client safely under shared lock
                    auto cm = guard->client->getCookieManager();
                    if (cm) {
                        (void)cm->hasAuth();
                        completedReads.fetch_add(1, std::memory_order_relaxed);
                    }
                    std::this_thread::yield();
                }
            });
        }

        // Start all workers simultaneously
        startWorkers.store(true, std::memory_order_release);

        // Allow workers to start reading
        std::this_thread::sleep_for(std::chrono::microseconds(50));

        // Simulate PlayerWidget destruction:
        // 1. Signal cancel token
        cancelToken->store(true, std::memory_order_relaxed);

        // 2. Detach under unique lock
        {
            std::unique_lock<std::shared_mutex> lock(guard->mutex);
            guard->client = nullptr;
        }

        // 3. Immediately delete the raw client!
        delete rawClient;
        rawClient = nullptr;

        // Join workers
        for (auto &t : workers) {
            t.join();
        }

        STRESS_ASSERT(completedReads.load() + safelyAborted.load() > 0, "Workers must have executed iterations");
    }

    STRESS_TEST_END("testAsyncClientGuardUafRace");
}

// ============================================================================
// 2. Scoped Cancellation Tokens Signaled on Destruction
// ============================================================================
void testCancellationTokensSignaledOnDestruct() {
    STRESS_TEST_BEGIN("testCancellationTokensSignaledOnDestruct");

    std::shared_ptr<std::atomic<bool>> lifetimeCancel;
    std::shared_ptr<std::atomic<bool>> resolveCancel;
    std::shared_ptr<std::atomic<bool>> metadataCancel;

    {
        auto *player = new PlayerWidget();
        lifetimeCancel = player->m_lifetimeCancel;
        STRESS_ASSERT(lifetimeCancel != nullptr, "m_lifetimeCancel must be non-null");
        STRESS_ASSERT(!lifetimeCancel->load(), "m_lifetimeCancel must start false");

        // Manually set tokens to simulate active work
        player->m_resolveCancel = std::make_shared<std::atomic<bool>>(false);
        player->m_metadataCancel = std::make_shared<std::atomic<bool>>(false);
        resolveCancel = player->m_resolveCancel;
        metadataCancel = player->m_metadataCancel;

        delete player;
    }

    STRESS_ASSERT(lifetimeCancel->load() == true, "m_lifetimeCancel must be true after destruction");
    STRESS_ASSERT(resolveCancel->load() == true, "m_resolveCancel must be true after destruction");
    STRESS_ASSERT(metadataCancel->load() == true, "m_metadataCancel must be true after destruction");

    STRESS_TEST_END("testCancellationTokensSignaledOnDestruct");
}

// ============================================================================
// 3. Measure PlayerWidget Destruction Latency (< 50ms)
// ============================================================================
void testDestructionLatency() {
    STRESS_TEST_BEGIN("testDestructionLatency");

    constexpr int kRuns = 25;
    double maxLatencyMs = 0.0;
    double sumLatencyMs = 0.0;

    auto cookieMgr = std::make_shared<CookieManager>();
    YouTubeClient client(cookieMgr);

    for (int i = 0; i < kRuns; ++i) {
        auto *player = new PlayerWidget();
        player->resize(1280, 800);
        player->setYouTubeClient(&client);

        // Pre-allocate tokens and active flags
        player->m_resolveCancel = std::make_shared<std::atomic<bool>>(false);
        player->m_metadataCancel = std::make_shared<std::atomic<bool>>(false);

        // Measure destruction time
        auto t0 = std::chrono::steady_clock::now();
        delete player;
        auto t1 = std::chrono::steady_clock::now();

        double elapsedMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        if (elapsedMs > maxLatencyMs) {
            maxLatencyMs = elapsedMs;
        }
        sumLatencyMs += elapsedMs;
    }

    double avgLatencyMs = sumLatencyMs / kRuns;
    std::cout << " (avg=" << avgLatencyMs << "ms, max=" << maxLatencyMs << "ms) " << std::flush;

    STRESS_ASSERT(maxLatencyMs < 50.0, "PlayerWidget destruction latency must be strictly < 50ms");

    STRESS_TEST_END("testDestructionLatency");
}

// ============================================================================
// 4. In-Flight User Action Destruction Latency & Safety
// ============================================================================
void testInFlightActionDestruction() {
    STRESS_TEST_BEGIN("testInFlightActionDestruction");

    auto cookieMgr = std::make_shared<CookieManager>();
    YouTubeClient client(cookieMgr);

    for (int rep = 0; rep < 20; ++rep) {
        auto *player = new PlayerWidget();
        player->resize(1280, 800);
        player->setYouTubeClient(&client);

        VideoItem item;
        item.id = "test_action_vid_" + std::to_string(rep);
        item.channelId = "test_channel_" + std::to_string(rep);
        player->m_currentItem = item;

        // Trigger user actions that dispatch QThreadPool tasks
        player->onLikeClicked();
        player->onDislikeClicked();
        player->onSubscribeClicked();

        // Immediately destroy player while actions are in thread pool
        auto t0 = std::chrono::steady_clock::now();
        delete player;
        auto t1 = std::chrono::steady_clock::now();

        double elapsedMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        STRESS_ASSERT(elapsedMs < 50.0, "Destruction with in-flight actions must be < 50ms");

        QCoreApplication::processEvents();
    }

    QThreadPool::globalInstance()->waitForDone(2000);
    QCoreApplication::processEvents();

    STRESS_TEST_END("testInFlightActionDestruction");
}

// ============================================================================
// 5. WatchNext Continuous Pagination Boundary & Edge Cases
// ============================================================================
void testWatchNextEdgeCases() {
    STRESS_TEST_BEGIN("testWatchNextEdgeCases");

    PlayerWidget player;
    player.resize(1280, 800);
    player.show();
    QCoreApplication::processEvents();

    auto cookieMgr = std::make_shared<CookieManager>();
    YouTubeClient client(cookieMgr);
    player.setYouTubeClient(&client);

    QScrollBar *sb = player.m_scrollArea->verticalScrollBar();
    player.m_scrollArea->widget()->setFixedHeight(3000);
    QCoreApplication::processEvents();

    int maxVal = sb->maximum();
    STRESS_ASSERT(maxVal > 0, "maxVal must be positive");

    // Case 1: Empty continuation token -> slot must early return without setting loading
    player.m_nextContinuationToken = "";
    player.m_isLoadingRecommendations = false;
    sb->setValue((maxVal * 90) / 100);
    QCoreApplication::processEvents();
    STRESS_ASSERT(!player.m_isLoadingRecommendations, "Empty token must not trigger loading");

    // Case 2: Null YouTubeClient -> slot must early return without setting loading
    player.setYouTubeClient(nullptr);
    player.m_nextContinuationToken = "valid_token";
    player.m_isLoadingRecommendations = false;
    sb->setValue((maxVal * 95) / 100);
    QCoreApplication::processEvents();
    STRESS_ASSERT(!player.m_isLoadingRecommendations, "Null client must not trigger loading");
    STRESS_ASSERT(player.m_nextContinuationToken == "valid_token", "Token must remain untouched with null client");

    // Case 3: Restore client and provide valid token
    player.setYouTubeClient(&client);
    player.m_metadataCancel = std::make_shared<std::atomic<bool>>(true); // abort fast
    sb->setValue((maxVal * 86) / 100);
    STRESS_ASSERT(player.m_isLoadingRecommendations, "Valid client + token must trigger loading");
    STRESS_ASSERT(player.m_nextContinuationToken.empty(), "Token must be moved/consumed");

    QCoreApplication::processEvents();
    QThreadPool::globalInstance()->waitForDone(2000);
    QCoreApplication::processEvents();

    STRESS_TEST_END("testWatchNextEdgeCases");
}

// ============================================================================
// 6. Pill Style Helper Verification
// ============================================================================
void testPillStyleHelper() {
    STRESS_TEST_BEGIN("testPillStyleHelper");

    PlayerWidget player;
    player.m_isLiked = false;
    player.m_isDisliked = false;
    player.updateLikeDislikeUi();

    QString leftInactive = player.m_likeBtn->styleSheet();
    QString rightInactive = player.m_dislikeBtn->styleSheet();

    STRESS_ASSERT(leftInactive.contains("border-top-left-radius: 18px"), "Left pill must have 18px top-left");
    STRESS_ASSERT(leftInactive.contains("border-top-right-radius: 0px"), "Left pill must have 0px top-right");
    STRESS_ASSERT(rightInactive.contains("border-top-left-radius: 0px"), "Right pill must have 0px top-left");
    STRESS_ASSERT(rightInactive.contains("border-top-right-radius: 18px"), "Right pill must have 18px top-right");

    // Activate like
    player.m_isLiked = true;
    player.updateLikeDislikeUi();
    QString leftActive = player.m_likeBtn->styleSheet();
    STRESS_ASSERT(leftActive.contains("background-color: #f1f1f1"), "Active like pill must have light background");
    STRESS_ASSERT(leftActive.contains("color: #0f0f0f"), "Active like pill must have dark text");

    STRESS_TEST_END("testPillStyleHelper");
}

// ============================================================================
// Main
// ============================================================================
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    std::cout << "========================================================" << std::endl;
    std::cout << " Milestone 4 In-Depth Empirical Stress Verification    " << std::endl;
    std::cout << "========================================================" << std::endl;

    testAsyncClientGuardUafRace();
    testCancellationTokensSignaledOnDestruct();
    testDestructionLatency();
    testInFlightActionDestruction();
    testWatchNextEdgeCases();
    testPillStyleHelper();

    std::cout << "========================================================" << std::endl;
    std::cout << "Results: " << g_passCount << " passed, " << g_failCount << " failed." << std::endl;
    std::cout << "========================================================" << std::endl;

    return (g_failCount == 0) ? 0 : 1;
}
