#include <iostream>
#include <chrono>
#include <cassert>
#include <memory>
#include <thread>
#include <atomic>

#include <QApplication>
#include <QThreadPool>
#include <QShortcut>
#include <QLineEdit>
#include <QTextEdit>
#include <QStackedWidget>
#include <QStatusBar>
#include <QMetaObject>

#include "ui/MainWindow.h"
#include "ui/CookieDialog.h"
#include "ui/ThumbnailLoader.h"
#include "ui/FeedView.h"
#include "ui/VideoCardDelegate.h"
#include "ui/PlayerWidget.h"
#include "utils/AppSettings.h"

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

void testMainWindowFullscreenNormalTransition() {
    CHALLENGE_TEST_BEGIN("testMainWindowFullscreenNormalTransition");

    MainWindow mw;
    mw.setGeometry(120, 120, 800, 600);
    mw.showNormal();
    QCoreApplication::processEvents();

    CHALLENGE_ASSERT(!mw.isFullScreen(), "Window should not start in fullscreen");
    CHALLENGE_ASSERT(!mw.isMaximized(), "Window should not start maximized");

    const QRect originalGeom = mw.geometry();
    auto *statusBar = mw.statusBar();
    auto *playerWidget = mw.findChild<PlayerWidget *>();
    CHALLENGE_ASSERT(playerWidget != nullptr, "PlayerWidget must exist in MainWindow");

    // Toggle fullscreen ON via QMetaObject invoking private slot onFullscreenToggleRequested
    bool invoked = QMetaObject::invokeMethod(&mw, "onFullscreenToggleRequested");
    CHALLENGE_ASSERT(invoked, "onFullscreenToggleRequested slot must be invocable");
    QCoreApplication::processEvents();

    CHALLENGE_ASSERT(mw.windowState().testFlag(Qt::WindowFullScreen), "Window state must include WindowFullScreen");
    CHALLENGE_ASSERT(playerWidget->isFullscreen(), "PlayerWidget must be in fullscreen mode");
    if (statusBar) {
        CHALLENGE_ASSERT(!statusBar->isVisible(), "Status bar must be hidden in fullscreen");
    }

    // Toggle fullscreen OFF
    invoked = QMetaObject::invokeMethod(&mw, "onFullscreenToggleRequested");
    CHALLENGE_ASSERT(invoked, "onFullscreenToggleRequested slot must be invocable");
    QCoreApplication::processEvents();

    CHALLENGE_ASSERT(!mw.windowState().testFlag(Qt::WindowFullScreen), "Window should no longer be fullscreen");
    CHALLENGE_ASSERT(!playerWidget->isFullscreen(), "PlayerWidget must not be in fullscreen mode");
    if (statusBar) {
        CHALLENGE_ASSERT(statusBar->isVisible(), "Status bar must be visible after exiting fullscreen");
    }
    CHALLENGE_ASSERT(!mw.isMaximized(), "Window should not be maximized");
    CHALLENGE_ASSERT(mw.geometry() == originalGeom, "Restored geometry must match original normal geometry exactly");

    CHALLENGE_TEST_END("testMainWindowFullscreenNormalTransition");
}

void testMainWindowFullscreenMaximizedTransition() {
    CHALLENGE_TEST_BEGIN("testMainWindowFullscreenMaximizedTransition");

    MainWindow mw;
    mw.setGeometry(150, 150, 750, 550);
    mw.showMaximized();
    QCoreApplication::processEvents();

    CHALLENGE_ASSERT(mw.isMaximized(), "Window should be maximized");
    QRect normalGeomBefore = mw.normalGeometry();

    // Toggle fullscreen ON from maximized state
    bool invoked = QMetaObject::invokeMethod(&mw, "onFullscreenToggleRequested");
    CHALLENGE_ASSERT(invoked, "onFullscreenToggleRequested slot must be invocable");
    QCoreApplication::processEvents();

    CHALLENGE_ASSERT(mw.windowState().testFlag(Qt::WindowFullScreen), "Window should be fullscreen");

    // Toggle fullscreen OFF
    invoked = QMetaObject::invokeMethod(&mw, "onFullscreenToggleRequested");
    CHALLENGE_ASSERT(invoked, "onFullscreenToggleRequested slot must be invocable");
    QCoreApplication::processEvents();

    CHALLENGE_ASSERT(!mw.windowState().testFlag(Qt::WindowFullScreen), "Window should not be fullscreen");
    CHALLENGE_ASSERT(mw.isMaximized(), "Window must restore to maximized state");

    // Unmaximize to verify normal restore geometry was preserved and not corrupted
    mw.showNormal();
    QCoreApplication::processEvents();

    CHALLENGE_ASSERT(!mw.isMaximized(), "Window should be in normal state after showNormal()");
    // Normal restore bounds should be preserved (matching original normal geometry dimensions)
    CHALLENGE_ASSERT(mw.geometry().size() == normalGeomBefore.size() || mw.normalGeometry().size() == normalGeomBefore.size(),
                     "Unmaximized normal geometry size must match original normal bounds");

    CHALLENGE_TEST_END("testMainWindowFullscreenMaximizedTransition");
}

void testMainWindowShortcutScoping() {
    CHALLENGE_TEST_BEGIN("testMainWindowShortcutScoping");

    MainWindow mw;
    mw.show();
    QCoreApplication::processEvents();

    // Locate QShortcut instances
    QList<QShortcut *> shortcuts = mw.findChildren<QShortcut *>();
    QShortcut *shortcutTheater = nullptr;
    QShortcut *shortcutNext = nullptr;
    for (auto *sc : shortcuts) {
        if (sc->key() == QKeySequence(Qt::Key_T)) {
            shortcutTheater = sc;
        } else if (sc->key() == QKeySequence(Qt::SHIFT | Qt::Key_N)) {
            shortcutNext = sc;
        }
    }
    CHALLENGE_ASSERT(shortcutTheater != nullptr, "Shortcut 'T' must exist");
    CHALLENGE_ASSERT(shortcutNext != nullptr, "Shortcut 'Shift+N' must exist");
    CHALLENGE_ASSERT(shortcutTheater->context() == Qt::WindowShortcut, "Shortcut 'T' must be WindowShortcut scoped");
    CHALLENGE_ASSERT(shortcutNext->context() == Qt::WindowShortcut, "Shortcut 'Shift+N' must be WindowShortcut scoped");

    auto *stackedWidget = mw.findChild<QStackedWidget *>();
    auto *playerWidget = mw.findChild<PlayerWidget *>();
    auto *searchInput = mw.findChild<QLineEdit *>();
    CHALLENGE_ASSERT(stackedWidget != nullptr, "QStackedWidget must exist");
    CHALLENGE_ASSERT(playerWidget != nullptr, "PlayerWidget must exist");
    CHALLENGE_ASSERT(searchInput != nullptr, "Search QLineEdit must exist");

    // Case 1: Stacked widget is on feed view (index 0)
    stackedWidget->setCurrentIndex(0);
    QCoreApplication::processEvents();

    CHALLENGE_ASSERT(stackedWidget->currentIndex() == 0, "Active widget must be feed view (index 0)");
    CHALLENGE_ASSERT(!playerWidget->isTheaterMode(), "Player should start in non-theater mode");

    // Activate 'T' while on Feed view
    emit shortcutTheater->activated();
    QCoreApplication::processEvents();
    CHALLENGE_ASSERT(!playerWidget->isTheaterMode(), "'T' shortcut MUST NOT trigger theater mode when on feed view");

    // Activate 'Shift+N' while on Feed view
    emit shortcutNext->activated();
    QCoreApplication::processEvents();
    // Does not crash or initiate feed navigation

    // Case 2: Switch to Player Widget (index 1)
    stackedWidget->setCurrentIndex(1);
    QCoreApplication::processEvents();
    CHALLENGE_ASSERT(stackedWidget->currentIndex() == 1, "Active widget must be player widget (index 1)");

    // Subcase 2A: Focus is in search line edit
    searchInput->setFocus();
    QCoreApplication::processEvents();
    CHALLENGE_ASSERT(searchInput->hasFocus(), "searchInput must have focus");

    emit shortcutTheater->activated();
    QCoreApplication::processEvents();
    CHALLENGE_ASSERT(!playerWidget->isTheaterMode(), "'T' shortcut MUST NOT trigger theater mode when QLineEdit has focus");

    emit shortcutNext->activated();
    QCoreApplication::processEvents();

    // Subcase 2B: Focus is in another child text edit
    auto *extraTextEdit = new QTextEdit(&mw);
    extraTextEdit->show();
    extraTextEdit->setFocus();
    QCoreApplication::processEvents();
    CHALLENGE_ASSERT(extraTextEdit->hasFocus(), "QTextEdit must have focus");

    emit shortcutTheater->activated();
    QCoreApplication::processEvents();
    CHALLENGE_ASSERT(!playerWidget->isTheaterMode(), "'T' shortcut MUST NOT trigger theater mode when QTextEdit has focus");
    extraTextEdit->deleteLater();

    // Subcase 2C: Focus is NOT in text input (e.g. player widget has focus)
    playerWidget->setFocus();
    QCoreApplication::processEvents();
    CHALLENGE_ASSERT(!searchInput->hasFocus(), "searchInput must not have focus");

    emit shortcutTheater->activated();
    QCoreApplication::processEvents();
    CHALLENGE_ASSERT(playerWidget->isTheaterMode(), "'T' shortcut MUST toggle theater mode when player is active and no text input focused");

    // Toggle theater mode back to false
    emit shortcutTheater->activated();
    QCoreApplication::processEvents();
    CHALLENGE_ASSERT(!playerWidget->isTheaterMode(), "'T' shortcut must toggle theater mode off");

    CHALLENGE_TEST_END("testMainWindowShortcutScoping");
}

void testCookieDialogNonBlockingRapidDestruction() {
    CHALLENGE_TEST_BEGIN("testCookieDialogNonBlockingRapidDestruction");

    auto cookieMgr = std::make_shared<CookieManager>();
    YouTubeClient client(cookieMgr);

    // 1. Measure pure destruction time across 20 dialogs
    int64_t totalDeleteUs = 0;
    for (int i = 0; i < 20; ++i) {
        auto *dialog = new CookieDialog(cookieMgr, &client);
        auto t0 = std::chrono::steady_clock::now();
        delete dialog;
        auto t1 = std::chrono::steady_clock::now();
        totalDeleteUs += std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    }
    double avgDeleteMs = (totalDeleteUs / 20.0) / 1000.0;
    std::cout << " [avg delete: " << avgDeleteMs << "ms] " << std::flush;
    CHALLENGE_ASSERT(avgDeleteMs < 10.0, "Average CookieDialog destruction must be <10ms (actual: " + std::to_string(avgDeleteMs) + "ms)");

    // 2. Measure destruction latency specifically while a worker is running in the background
    cookieMgr->setCookie("SAPISID", "test_token_auth");
    for (int i = 0; i < 5; ++i) {
        auto *dialog = new CookieDialog(cookieMgr, &client);
        dialog->show();
        QCoreApplication::processEvents();
        QMetaObject::invokeMethod(dialog, "onDetectChannelsClicked");
        
        auto t0 = std::chrono::steady_clock::now();
        delete dialog; // Destructor must cancel worker and return immediately without .join()
        auto t1 = std::chrono::steady_clock::now();
        auto deleteMs = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        
        CHALLENGE_ASSERT(deleteMs < 50, "CookieDialog destruction with active worker must return in <50ms without blocking on thread (actual: " + std::to_string(deleteMs) + "ms)");
        QCoreApplication::processEvents();
    }
    QThreadPool::globalInstance()->waitForDone(3000);

    CHALLENGE_TEST_END("testCookieDialogNonBlockingRapidDestruction");
}

void testCookieDialogActiveDetectionCancellationStress() {
    CHALLENGE_TEST_BEGIN("testCookieDialogActiveDetectionCancellationStress");

    auto cookieMgr = std::make_shared<CookieManager>();
    // Set a mock SAPISID so hasAuth() returns true and onDetectChannelsClicked actually launches worker
    cookieMgr->setCookie("SAPISID", "mock_sapisid_hash_token_12345");
    CHALLENGE_ASSERT(cookieMgr->hasAuth(), "CookieManager must report hasAuth true with SAPISID");

    YouTubeClient client(cookieMgr);

    // Stress test: 50 iterations of starting background detection and immediately destroying or cancelling
    for (int i = 0; i < 50; ++i) {
        auto *dialog = new CookieDialog(cookieMgr, &client);
        dialog->show();
        QCoreApplication::processEvents();

        // Trigger detection via invokeMethod
        bool ok = QMetaObject::invokeMethod(dialog, "onDetectChannelsClicked");
        CHALLENGE_ASSERT(ok, "onDetectChannelsClicked must be invocable");

        if (i % 2 == 0) {
            // Test cancel via closeEvent/done
            dialog->done(0);
            delete dialog;
        } else {
            // Test immediate destruction while worker is in flight
            delete dialog;
        }

        QCoreApplication::processEvents();
    }

    // Wait for all QThreadPool tasks to complete
    bool poolDone = QThreadPool::globalInstance()->waitForDone(5000);
    CHALLENGE_ASSERT(poolDone, "QThreadPool should cleanly complete all cancelled tasks within 5s");

    CHALLENGE_TEST_END("testCookieDialogActiveDetectionCancellationStress");
}

void testThumbnailLoaderContractAndInFlightKey() {
    CHALLENGE_TEST_BEGIN("testThumbnailLoaderContractAndInFlightKey");

    // Verify avatar key collision fix: different sizes must have different keys
    AvatarKey key1{"https://example.com/avatar.jpg", 36};
    AvatarKey key2{"https://example.com/avatar.jpg", 40};
    CHALLENGE_ASSERT(!(key1 == key2), "Avatar keys with different sizes must not be equal");
    CHALLENGE_ASSERT(std::hash<AvatarKey>{}(key1) != std::hash<AvatarKey>{}(key2), "Avatar key hashes must differ");

    // Verify WebP format sniffing: RIFF + WEBP
    // 12 bytes: 'R','I','F','F', 0,0,0,0, 'W','E','B','P'
    uint8_t webpHeader[12] = {'R', 'I', 'F', 'F', 0x20, 0, 0, 0, 'W', 'E', 'B', 'P'};
    QImage imgWebp = ThumbnailLoader::decodeImageFromBytes(webpHeader, sizeof(webpHeader));
    // Header alone cannot decode into a full image, but sniffing handled it without crashing
    (void)imgWebp;

    // Verify non-WebP RIFF (e.g. AVI or WAV: 'R','I','F','F', ... 'W','A','V','E')
    uint8_t wavHeader[12] = {'R', 'I', 'F', 'F', 0x20, 0, 0, 0, 'W', 'A', 'V', 'E'};
    QImage imgWav = ThumbnailLoader::decodeImageFromBytes(wavHeader, sizeof(wavHeader));
    CHALLENGE_ASSERT(imgWav.isNull(), "WAV RIFF must not decode as image");

    CHALLENGE_TEST_END("testThumbnailLoaderContractAndInFlightKey");
}

void testMainWindowRapidFullscreenToggles() {
    CHALLENGE_TEST_BEGIN("testMainWindowRapidFullscreenToggles");

    MainWindow mw;
    mw.setGeometry(100, 100, 800, 600);
    mw.showNormal();
    QCoreApplication::processEvents();
    const QRect initialGeom = mw.geometry();

    // 10 rapid toggles back-and-forth
    for (int i = 0; i < 10; ++i) {
        QMetaObject::invokeMethod(&mw, "onFullscreenToggleRequested");
        QCoreApplication::processEvents();
    }

    // 10 toggles from normal should land back in normal state (even count)
    CHALLENGE_ASSERT(!mw.windowState().testFlag(Qt::WindowFullScreen), "Window should not be in fullscreen after even number of toggles");
    CHALLENGE_ASSERT(!mw.isMaximized(), "Window should not be maximized");
    CHALLENGE_ASSERT(mw.geometry() == initialGeom, "Restored geometry must match initial geometry");

    CHALLENGE_TEST_END("testMainWindowRapidFullscreenToggles");
}

void testMainWindowBackToFeedFromFullscreen() {
    CHALLENGE_TEST_BEGIN("testMainWindowBackToFeedFromFullscreen");

    MainWindow mw;
    mw.setGeometry(100, 100, 800, 600);
    mw.showNormal();
    QCoreApplication::processEvents();

    auto *stackedWidget = mw.findChild<QStackedWidget *>();
    auto *playerWidget = mw.findChild<PlayerWidget *>();
    CHALLENGE_ASSERT(stackedWidget != nullptr, "QStackedWidget must exist");
    CHALLENGE_ASSERT(playerWidget != nullptr, "PlayerWidget must exist");

    // Switch to player widget
    stackedWidget->setCurrentIndex(1);
    QCoreApplication::processEvents();

    // Enter fullscreen
    QMetaObject::invokeMethod(&mw, "onFullscreenToggleRequested");
    QCoreApplication::processEvents();
    CHALLENGE_ASSERT(mw.windowState().testFlag(Qt::WindowFullScreen), "Window must be fullscreen");

    // Trigger back to feed
    QMetaObject::invokeMethod(&mw, "onBackToFeedRequested");
    QCoreApplication::processEvents();

    CHALLENGE_ASSERT(!mw.windowState().testFlag(Qt::WindowFullScreen), "Window must exit fullscreen on backToFeed");
    CHALLENGE_ASSERT(stackedWidget->currentIndex() == 0, "Active widget must be feed view (index 0)");
    CHALLENGE_ASSERT(!playerWidget->isFullscreen(), "PlayerWidget must not be in fullscreen mode");

    CHALLENGE_TEST_END("testMainWindowBackToFeedFromFullscreen");
}

void testCookieDialogClearCancelsActiveDetection() {
    CHALLENGE_TEST_BEGIN("testCookieDialogClearCancelsActiveDetection");

    auto cookieMgr = std::make_shared<CookieManager>();
    cookieMgr->setCookie("SAPISID", "test_mock_token_for_clear");
    YouTubeClient client(cookieMgr);

    auto *dialog = new CookieDialog(cookieMgr, &client);
    dialog->show();
    QCoreApplication::processEvents();

    // Start detection
    QMetaObject::invokeMethod(dialog, "onDetectChannelsClicked");
    CHALLENGE_ASSERT(cookieMgr->hasAuth(), "CookieManager had auth");

    // Now immediately call onClearClicked while detection worker is in-flight
    QMetaObject::invokeMethod(dialog, "onClearClicked");
    QCoreApplication::processEvents();

    CHALLENGE_ASSERT(!cookieMgr->hasAuth(), "Cookies must be cleared");

    // Wait for worker task to settle in background pool
    bool poolDone = QThreadPool::globalInstance()->waitForDone(5000);
    CHALLENGE_ASSERT(poolDone, "Thread pool must settle");

    delete dialog;
    QCoreApplication::processEvents();

    CHALLENGE_TEST_END("testCookieDialogClearCancelsActiveDetection");
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    std::cout << "==========================================" << std::endl;
    std::cout << "    Milestone 3 Empirical Challenge Suite  " << std::endl;
    std::cout << "==========================================" << std::endl;

    testMainWindowFullscreenNormalTransition();
    testMainWindowFullscreenMaximizedTransition();
    testMainWindowShortcutScoping();
    testMainWindowRapidFullscreenToggles();
    testMainWindowBackToFeedFromFullscreen();
    testCookieDialogNonBlockingRapidDestruction();
    testCookieDialogActiveDetectionCancellationStress();
    testCookieDialogClearCancelsActiveDetection();
    testThumbnailLoaderContractAndInFlightKey();

    std::cout << "==========================================" << std::endl;
    std::cout << "Results: " << g_passCount << " passed, " << g_failCount << " failed." << std::endl;
    std::cout << "==========================================" << std::endl;

    return (g_failCount == 0) ? 0 : 1;
}
