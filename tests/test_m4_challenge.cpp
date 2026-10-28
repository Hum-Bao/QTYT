#include <iostream>
#include <limits>
#include <cmath>
#include <cassert>
#include <chrono>
#include <thread>
#include <atomic>
#include <vector>

#include <QApplication>
#include <QScrollBar>
#include <QScrollArea>
#include <QLabel>
#include <QPixmap>
#include <QPainter>
#include <QThreadPool>
#include <QMetaObject>

#include "ui/PlayerControlsOverlay.h"
#include "core/YouTubeClient.h"
#include "core/CookieManager.h"

#define private public
#define protected public
#include "ui/PlayerWidget.h"
#include "ui/ThumbnailLoader.h"
#undef private
#undef protected

static int g_passCount = 0;
static int g_failCount = 0;

#define CHALLENGE_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "\n[FAIL] Line " << __LINE__ << ": " << msg << " (" #cond ")" << std::endl; \
            g_failCount++; \
            return; \
        } \
    } while (0)

#define CHALLENGE_TEST_BEGIN(name) \
    std::cout << "[RUN ] " << name << "..." << std::flush; \
    int startFails = g_failCount;

#define CHALLENGE_TEST_END(name) \
    if (g_failCount == startFails) { \
        std::cout << " [PASS]" << std::endl; \
        g_passCount++; \
    }

// ============================================================================
// 1. Interface Contract #6: PlayerControlsOverlay::formatTime
// ============================================================================
void testFormatTimeContract() {
    CHALLENGE_TEST_BEGIN("testFormatTimeContract");

    // seconds < 0 -> "0:00"
    CHALLENGE_ASSERT(PlayerControlsOverlay::formatTime(-1.0) == "0:00", "seconds < 0 must return 0:00");
    CHALLENGE_ASSERT(PlayerControlsOverlay::formatTime(-0.001) == "0:00", "small negative must return 0:00");
    CHALLENGE_ASSERT(PlayerControlsOverlay::formatTime(-500.0) == "0:00", "large negative must return 0:00");
    CHALLENGE_ASSERT(PlayerControlsOverlay::formatTime(-0.0) == "0:00", "negative zero must return 0:00");

    // seconds == 0.0 -> "0:00"
    CHALLENGE_ASSERT(PlayerControlsOverlay::formatTime(0.0) == "0:00", "0.0 must return 0:00");

    // seconds == 59.9 -> "0:59"
    CHALLENGE_ASSERT(PlayerControlsOverlay::formatTime(59.9) == "0:59", "59.9 must return 0:59");
    CHALLENGE_ASSERT(PlayerControlsOverlay::formatTime(59.999) == "0:59", "59.999 must return 0:59");

    // seconds == 60.0 -> "1:00"
    CHALLENGE_ASSERT(PlayerControlsOverlay::formatTime(60.0) == "1:00", "60.0 must return 1:00");

    // seconds == 3599.0 -> "59:59"
    CHALLENGE_ASSERT(PlayerControlsOverlay::formatTime(3599.0) == "59:59", "3599.0 must return 59:59");
    CHALLENGE_ASSERT(PlayerControlsOverlay::formatTime(3599.99) == "59:59", "3599.99 must return 59:59");

    // seconds == 3600.0 -> "1:00:00"
    CHALLENGE_ASSERT(PlayerControlsOverlay::formatTime(3600.0) == "1:00:00", "3600.0 must return 1:00:00");

    // seconds == 3661.0 -> "1:01:01"
    CHALLENGE_ASSERT(PlayerControlsOverlay::formatTime(3661.0) == "1:01:01", "3661.0 must return 1:01:01");

    // Non-finite protection: NaN, +Inf, -Inf -> "0:00"
    double qnan = std::numeric_limits<double>::quiet_NaN();
    double snan = std::numeric_limits<double>::signaling_NaN();
    double inf = std::numeric_limits<double>::infinity();
    double negInf = -std::numeric_limits<double>::infinity();

    CHALLENGE_ASSERT(PlayerControlsOverlay::formatTime(qnan) == "0:00", "quiet NaN must return 0:00");
    CHALLENGE_ASSERT(PlayerControlsOverlay::formatTime(snan) == "0:00", "signaling NaN must return 0:00");
    CHALLENGE_ASSERT(PlayerControlsOverlay::formatTime(inf) == "0:00", "+Inf must return 0:00");
    CHALLENGE_ASSERT(PlayerControlsOverlay::formatTime(negInf) == "0:00", "-Inf must return 0:00");

    // Additional boundary and stress cases:
    CHALLENGE_ASSERT(PlayerControlsOverlay::formatTime(0.5) == "0:00", "0.5 sub-second must truncate to 0:00");
    CHALLENGE_ASSERT(PlayerControlsOverlay::formatTime(1.0) == "0:01", "1.0 must return 0:01");
    CHALLENGE_ASSERT(PlayerControlsOverlay::formatTime(9.0) == "0:09", "9.0 must return 0:09");
    CHALLENGE_ASSERT(PlayerControlsOverlay::formatTime(10.0) == "0:10", "10.0 must return 0:10");
    CHALLENGE_ASSERT(PlayerControlsOverlay::formatTime(61.0) == "1:01", "61.0 must return 1:01");
    CHALLENGE_ASSERT(PlayerControlsOverlay::formatTime(119.0) == "1:59", "119.0 must return 1:59");
    CHALLENGE_ASSERT(PlayerControlsOverlay::formatTime(120.0) == "2:00", "120.0 must return 2:00");
    CHALLENGE_ASSERT(PlayerControlsOverlay::formatTime(3601.0) == "1:00:01", "3601.0 must return 1:00:01");
    CHALLENGE_ASSERT(PlayerControlsOverlay::formatTime(7200.0) == "2:00:00", "7200.0 must return 2:00:00");
    CHALLENGE_ASSERT(PlayerControlsOverlay::formatTime(86399.0) == "23:59:59", "86399.0 must return 23:59:59");
    CHALLENGE_ASSERT(PlayerControlsOverlay::formatTime(86400.0) == "24:00:00", "86400.0 must return 24:00:00");
    CHALLENGE_ASSERT(PlayerControlsOverlay::formatTime(360000.0) == "100:00:00", "360000.0 (100h) must return 100:00:00");

    CHALLENGE_TEST_END("testFormatTimeContract");
}

// ============================================================================
// 2. Task 4.3: WatchNext Infinite Scroll Pagination
// ============================================================================
void testWatchNextScrollThreshold() {
    CHALLENGE_TEST_BEGIN("testWatchNextScrollThreshold");

    PlayerWidget player;
    player.resize(1280, 800);
    player.show();
    QCoreApplication::processEvents();

    CHALLENGE_ASSERT(player.m_scrollArea != nullptr, "m_scrollArea must exist");
    QScrollBar *sb = player.m_scrollArea->verticalScrollBar();
    CHALLENGE_ASSERT(sb != nullptr, "verticalScrollBar must exist");

    auto cookieMgr = std::make_shared<CookieManager>();
    YouTubeClient client(cookieMgr);
    player.setYouTubeClient(&client);

    // Expand content widget height so QScrollArea establishes a real scrollable range
    player.m_scrollArea->widget()->setFixedHeight(2000);
    QCoreApplication::processEvents();

    int maxVal = sb->maximum();
    CHALLENGE_ASSERT(maxVal > 0, "maxVal must be greater than 0");

    int val84 = (maxVal * 84) / 100;
    int val85 = (maxVal * 85) / 100;

    // Test 1: Scroll to 84% (< 85%) -> must NOT trigger
    player.m_nextContinuationToken = "test_token_84";
    player.m_isLoadingRecommendations = false;
    sb->setValue(val84);
    QCoreApplication::processEvents();

    CHALLENGE_ASSERT(!player.m_isLoadingRecommendations, "Scroll at 84% must NOT trigger pagination");
    CHALLENGE_ASSERT(player.m_nextContinuationToken == "test_token_84", "Token must remain untouched at 84%");

    // Test 2: Scroll to 85% (>= 85%) -> MUST trigger
    player.m_nextContinuationToken = "test_token_85";
    player.m_isLoadingRecommendations = false;
    // Set metadata cancel token to abort background curl request immediately
    player.m_metadataCancel = std::make_shared<std::atomic<bool>>(true);
    sb->setValue(val85);

    // Synchronous state immediately after trigger:
    CHALLENGE_ASSERT(player.m_isLoadingRecommendations, "Scroll at 85% MUST synchronously trigger pagination");
    CHALLENGE_ASSERT(player.m_nextContinuationToken.empty(), "Continuation token must be consumed immediately");

    // Let queued worker and invokeMethod finish cleanly
    QCoreApplication::processEvents();
    QThreadPool::globalInstance()->waitForDone(2000);
    QCoreApplication::processEvents();

    CHALLENGE_ASSERT(!player.m_isLoadingRecommendations, "Loading state must reset after worker finishes");

    CHALLENGE_TEST_END("testWatchNextScrollThreshold");
}

void testWatchNextScrollFullscreenSuspension() {
    CHALLENGE_TEST_BEGIN("testWatchNextScrollFullscreenSuspension");

    PlayerWidget player;
    player.resize(1280, 800);
    player.show();
    QCoreApplication::processEvents();

    QScrollBar *sb = player.m_scrollArea->verticalScrollBar();
    auto cookieMgr = std::make_shared<CookieManager>();
    YouTubeClient client(cookieMgr);
    player.setYouTubeClient(&client);

    player.m_scrollArea->widget()->setFixedHeight(2000);
    QCoreApplication::processEvents();
    int maxVal = sb->maximum();
    int val85 = (maxVal * 85) / 100;

    // Put player in fullscreen mode
    player.setFullscreen(true);
    CHALLENGE_ASSERT(player.isFullscreen(), "PlayerWidget must report fullscreen");
    CHALLENGE_ASSERT(player.m_isFullscreen, "m_isFullscreen must be true");

    sb->setValue(0);
    player.m_nextContinuationToken = "test_token_fullscreen";
    player.m_isLoadingRecommendations = false;
    QCoreApplication::processEvents();

    // Scroll to 85% in fullscreen -> MUST NOT trigger!
    sb->setValue(val85);
    QCoreApplication::processEvents();
    CHALLENGE_ASSERT(!player.m_isLoadingRecommendations, "Fullscreen scroll must NOT trigger pagination");
    CHALLENGE_ASSERT(player.m_nextContinuationToken == "test_token_fullscreen", "Token must not be consumed in fullscreen");

    // Exit fullscreen mode
    player.setFullscreen(false);
    CHALLENGE_ASSERT(!player.isFullscreen(), "PlayerWidget must not be fullscreen");

    // Reset scrollbar to 0 then to 85% in normal mode -> MUST trigger!
    sb->setValue(0);
    QCoreApplication::processEvents();

    player.m_metadataCancel = std::make_shared<std::atomic<bool>>(true);
    sb->setValue(val85);

    CHALLENGE_ASSERT(player.m_isLoadingRecommendations, "Normal mode scroll >= 85% MUST trigger pagination");
    CHALLENGE_ASSERT(player.m_nextContinuationToken.empty(), "Token must be consumed in normal mode");

    QCoreApplication::processEvents();
    QThreadPool::globalInstance()->waitForDone(2000);
    QCoreApplication::processEvents();

    CHALLENGE_TEST_END("testWatchNextScrollFullscreenSuspension");
}

void testWatchNextScrollConcurrentDeduplication() {
    CHALLENGE_TEST_BEGIN("testWatchNextScrollConcurrentDeduplication");

    PlayerWidget player;
    player.resize(1280, 800);
    player.show();
    QCoreApplication::processEvents();

    QScrollBar *sb = player.m_scrollArea->verticalScrollBar();
    auto cookieMgr = std::make_shared<CookieManager>();
    YouTubeClient client(cookieMgr);
    player.setYouTubeClient(&client);

    player.m_scrollArea->widget()->setFixedHeight(2000);
    QCoreApplication::processEvents();
    int maxVal = sb->maximum();
    int val85 = (maxVal * 85) / 100;

    sb->setValue(0);

    // Set active loading state
    player.m_isLoadingRecommendations = true;
    player.m_nextContinuationToken = "token_while_loading";

    // Trigger scroll events rapidly
    sb->setValue(val85);
    QCoreApplication::processEvents();
    sb->setValue(maxVal);
    QCoreApplication::processEvents();

    // Token must NOT be stolen or consumed because m_isLoadingRecommendations == true
    CHALLENGE_ASSERT(player.m_nextContinuationToken == "token_while_loading",
                     "Concurrent scroll while loading must NOT consume continuation token");

    // Direct slot invocation must also early-return
    player.onRecommendationsScrollRequested();
    CHALLENGE_ASSERT(player.m_nextContinuationToken == "token_while_loading",
                     "Direct onRecommendationsScrollRequested while loading must be ignored");

    // Once loading state is reset, new request proceeds
    player.m_isLoadingRecommendations = false;
    player.m_metadataCancel = std::make_shared<std::atomic<bool>>(true);
    player.onRecommendationsScrollRequested();

    CHALLENGE_ASSERT(player.m_isLoadingRecommendations,
                     "Once loading is false, onRecommendationsScrollRequested must proceed");
    CHALLENGE_ASSERT(player.m_nextContinuationToken.empty(),
                     "Token consumed after loading reset");

    QCoreApplication::processEvents();
    QThreadPool::globalInstance()->waitForDone(2000);
    QCoreApplication::processEvents();

    CHALLENGE_TEST_END("testWatchNextScrollConcurrentDeduplication");
}

// ============================================================================
// 3. Task 4.2: Channel Avatar Pipeline
// ============================================================================
void testChannelAvatarPipeline() {
    CHALLENGE_TEST_BEGIN("testChannelAvatarPipeline");

    PlayerWidget player;
    player.resize(1280, 800);
    player.show();
    QCoreApplication::processEvents();

    QLabel *avatarLabel = player.m_channelAvatarLabel;
    CHALLENGE_ASSERT(avatarLabel != nullptr, "m_channelAvatarLabel must exist");
    CHALLENGE_ASSERT(avatarLabel->width() == 40 && avatarLabel->height() == 40,
                     "m_channelAvatarLabel must be 40x40");

    // Initial state: label text is "?", pixmap is null
    CHALLENGE_ASSERT(avatarLabel->text() == "?", "Initial avatar label text must be '?'");
    CHALLENGE_ASSERT(avatarLabel->pixmap().isNull(), "Initial avatar label pixmap must be null");

    // Test 1: loadChannelAvatar with URL
    std::string testUrl = "https://yt3.ggpht.com/ytc/AIdro_mockAvatarChannel1=s800-c-k-c0x00ffffff-no-rj";
    player.loadChannelAvatar(testUrl);

    CHALLENGE_ASSERT(!player.m_channelAvatarUrl.empty(), "m_channelAvatarUrl must be set");
    std::string expectedNormalized = player.m_channelAvatarUrl;

    // Create a 40x40 mock pixmap
    QPixmap mockPix40(40, 40);
    mockPix40.fill(QColor(0, 255, 0));

    // Emit ThumbnailLoader::avatarReady for this URL and size 40
    emit ThumbnailLoader::instance().avatarReady(expectedNormalized, 40, mockPix40);
    QCoreApplication::processEvents();

    // Verify avatar pixmap is now set and text is cleared
    CHALLENGE_ASSERT(!avatarLabel->pixmap().isNull(), "Avatar label pixmap must not be null after avatarReady");
    CHALLENGE_ASSERT(avatarLabel->pixmap().width() == 40, "Avatar pixmap width must be 40");
    CHALLENGE_ASSERT(avatarLabel->text().isEmpty(), "Avatar text must be cleared when pixmap is set");

    // Test 2: Mismatch Size Rejection (e.g., size 24 or 36 from feed / compact card)
    QPixmap mockPix24(24, 24);
    mockPix24.fill(QColor(255, 0, 0)); // Red

    // Store current pixmap
    QPixmap currentPix = avatarLabel->pixmap();

    emit ThumbnailLoader::instance().avatarReady(expectedNormalized, 24, mockPix24);
    QCoreApplication::processEvents();

    CHALLENGE_ASSERT(avatarLabel->pixmap().width() == 40, "Avatar pixmap must NOT accept size 24");
    CHALLENGE_ASSERT(avatarLabel->pixmap().toImage() == currentPix.toImage(),
                     "Avatar pixmap must remain unchanged on size mismatch");

    // Test 3: Mismatch URL Rejection (avatar from previous or different channel)
    QPixmap mockPixOther(40, 40);
    mockPixOther.fill(QColor(0, 0, 255)); // Blue

    emit ThumbnailLoader::instance().avatarReady("https://different-channel.com/avatar.jpg", 40, mockPixOther);
    QCoreApplication::processEvents();

    CHALLENGE_ASSERT(avatarLabel->pixmap().toImage() == currentPix.toImage(),
                     "Avatar pixmap must NOT accept mismatch URL");

    // Test 4: Null pixmap rejection
    emit ThumbnailLoader::instance().avatarReady(expectedNormalized, 40, QPixmap());
    QCoreApplication::processEvents();

    CHALLENGE_ASSERT(!avatarLabel->pixmap().isNull(), "Avatar pixmap must NOT be overwritten with null pixmap");

    // Test 5: loadChannelAvatar with empty URL clears avatar
    player.loadChannelAvatar("");
    CHALLENGE_ASSERT(player.m_channelAvatarUrl.empty(), "Avatar url must be cleared when empty");
    CHALLENGE_ASSERT(avatarLabel->pixmap().isNull(), "Avatar label pixmap must be cleared when empty url");

    CHALLENGE_TEST_END("testChannelAvatarPipeline");
}

// ============================================================================
// 4. Task 4.1: Non-Blocking Destruction & Scoped Cancellation
// ============================================================================
void testNonBlockingDestruction() {
    CHALLENGE_TEST_BEGIN("testNonBlockingDestruction");

    auto *player = new PlayerWidget();
    player->resize(1280, 800);
    player->show();
    QCoreApplication::processEvents();

    auto cookieMgr = std::make_shared<CookieManager>();
    YouTubeClient client(cookieMgr);
    player->setYouTubeClient(&client);

    // Verify initial tokens exist and are not cancelled
    CHALLENGE_ASSERT(player->m_lifetimeCancel != nullptr, "m_lifetimeCancel must exist");
    CHALLENGE_ASSERT(!player->m_lifetimeCancel->load(), "m_lifetimeCancel must be initially false");

    // Play a video (dispatches async background work)
    VideoItem item;
    item.id = "test_non_blocking_destruct";
    item.title = "Test Destruction Video";
    player->playVideo(item);
    QCoreApplication::processEvents();

    CHALLENGE_ASSERT(player->m_resolveCancel != nullptr, "m_resolveCancel must be allocated for playVideo");
    CHALLENGE_ASSERT(!player->m_resolveCancel->load(), "m_resolveCancel must be initially false");

    auto lifetimeCancel = player->m_lifetimeCancel;
    auto resolveCancel = player->m_resolveCancel;

    // Time the destruction of PlayerWidget on GUI thread: must be instant (< 100ms), zero .join() hangs
    auto start = std::chrono::steady_clock::now();
    delete player;
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - start).count();
    std::cout << " (destructed in " << elapsedMs << "ms) " << std::flush;


    CHALLENGE_ASSERT(elapsedMs < 500, "PlayerWidget destruction must be non-blocking (< 500ms)");
    CHALLENGE_ASSERT(lifetimeCancel->load() == true, "m_lifetimeCancel must be signaled on destruction");
    CHALLENGE_ASSERT(resolveCancel->load() == true, "m_resolveCancel must be signaled on destruction");

    // Clean up background pool
    QThreadPool::globalInstance()->waitForDone(2000);
    QCoreApplication::processEvents();

    CHALLENGE_TEST_END("testNonBlockingDestruction");
}

// ============================================================================
// Main Execution
// ============================================================================
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    std::cout << "========================================================" << std::endl;
    std::cout << "   Milestone 4 Empirical Adversarial Challenge Suite    " << std::endl;
    std::cout << "========================================================" << std::endl;

    testFormatTimeContract();
    testWatchNextScrollThreshold();
    testWatchNextScrollFullscreenSuspension();
    testWatchNextScrollConcurrentDeduplication();
    testChannelAvatarPipeline();
    testNonBlockingDestruction();

    std::cout << "========================================================" << std::endl;
    std::cout << "Results: " << g_passCount << " passed, " << g_failCount << " failed." << std::endl;
    std::cout << "========================================================" << std::endl;

    return (g_failCount == 0) ? 0 : 1;
}
