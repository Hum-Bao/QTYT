#include "../src/ui/FeedView.h"
#include "../src/ui/LeftNavDrawer.h"
#include "../src/ui/NavSidebar.h"
#include "../src/ui/PlaylistPageWidget.h"
#include "../src/ui/VideoCardDelegate.h"
#include "../src/ui/YouTubeIcons.h"
#include "../src/utils/AppSettings.h"

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QToolButton>
#include <QWidget>

#include <cstdlib>
#include <iostream>

namespace {

int g_passCount = 0;
int g_failCount = 0;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            std::cerr << "  [FAIL] " << __FILE__ << ":" << __LINE__ << ": " #cond << "\n";                             \
            ++g_failCount;                                                                                             \
        } else {                                                                                                       \
            ++g_passCount;                                                                                             \
        }                                                                                                              \
    } while (0)

#define CHECK_EQ(a, b)                                                                                                \
    do {                                                                                                               \
        if ((a) != (b)) {                                                                                              \
            std::cerr << "  [FAIL] " << __FILE__ << ":" << __LINE__ << ": " #a " != " #b "\n";                         \
            ++g_failCount;                                                                                             \
        } else {                                                                                                       \
            ++g_passCount;                                                                                             \
        }                                                                                                              \
    } while (0)

void testProceduralIcons() {
    std::cout << "[RUN ] testProceduralIcons..." << std::endl;

    // Test Menu icon
    QIcon menuIcon = lightyt::ui::icons::menu(QColor(0xf1, 0xf1, 0xf1), 24);
    CHECK(!menuIcon.isNull());
    QPixmap menuPix = menuIcon.pixmap(24, 24);
    CHECK(!menuPix.isNull());
    CHECK_EQ(menuPix.width(), 24);
    CHECK_EQ(menuPix.height(), 24);

    // Test History icon
    QIcon historyIcon = lightyt::ui::icons::history(QColor(0xf1, 0xf1, 0xf1), 24);
    CHECK(!historyIcon.isNull());
    QPixmap histPix = historyIcon.pixmap(24, 24);
    CHECK(!histPix.isNull());

    // Test Playlists icon
    QIcon playlistsIcon = lightyt::ui::icons::playlists(QColor(0xf1, 0xf1, 0xf1), 24);
    CHECK(!playlistsIcon.isNull());
    QPixmap playPix = playlistsIcon.pixmap(24, 24);
    CHECK(!playPix.isNull());

    // Test Watch Later icon
    QIcon watchLaterIcon = lightyt::ui::icons::watchLater(QColor(0xf1, 0xf1, 0xf1), 24);
    CHECK(!watchLaterIcon.isNull());
    QPixmap wlPix = watchLaterIcon.pixmap(24, 24);
    CHECK(!wlPix.isNull());

    // Test Shuffle icon
    QIcon shuffleIcon = lightyt::ui::icons::shuffle(QColor(0xf1, 0xf1, 0xf1), 24);
    CHECK(!shuffleIcon.isNull());
    QPixmap shPix = shuffleIcon.pixmap(24, 24);
    CHECK(!shPix.isNull());
    CHECK_EQ(shPix.width(), 24);
    CHECK_EQ(shPix.height(), 24);

    // Verify caching
    QIcon menuCached = lightyt::ui::icons::menu(QColor(0xf1, 0xf1, 0xf1), 24);
    CHECK(!menuCached.isNull());

    std::cout << "[PASS] testProceduralIcons\n";
}

void testPlaylistDelegateAndButtonRect() {
    std::cout << "[RUN ] testPlaylistDelegateAndButtonRect..." << std::endl;
    QRect itemRect(0, 0, 320, 300);
    QRect btnRect = VideoCardDelegate::getPlaylistButtonRect(itemRect);
    CHECK(itemRect.contains(btnRect));
    CHECK_EQ(btnRect.height(), 26);
    CHECK(btnRect.width() > 50);

    VideoCardDelegate delegate;
    delegate.setLayoutMode(CardLayoutMode::Playlist);
    QSize hint = delegate.sizeHint(QStyleOptionViewItem{}, QModelIndex{});
    CHECK_EQ(hint.height(), 128);
    std::cout << "[PASS] testPlaylistDelegateAndButtonRect\n";
}

void testPlaylistPageWidget() {
    std::cout << "[RUN ] testPlaylistPageWidget..." << std::endl;
    auto *widget = new PlaylistPageWidget();
    widget->resize(1200, 800);

    VideoItem plItem;
    plItem.id = "PL123";
    plItem.title = "Synthwave Beats";
    plItem.author = "Lofi Channel";
    plItem.duration = "25 videos";
    plItem.publishedTime = "Public";
    plItem.description = "Chill retro synthwave music.";
    plItem.isPlaylist = true;

    widget->setPlaylist(plItem);
    CHECK_EQ(widget->currentPlaylist().id, "PL123");
    CHECK_EQ(widget->currentPlaylist().title, "Synthwave Beats");

    VideoItem vid1;
    vid1.id = "vid_001";
    vid1.title = "Track One";
    vid1.author = "Lofi Channel";
    vid1.duration = "03:45";

    VideoItem vid2;
    vid2.id = "vid_002";
    vid2.title = "Track Two";
    vid2.author = "Lofi Channel";
    vid2.duration = "04:12";

    widget->setVideos({vid1, vid2});
    CHECK_EQ(widget->videos().size(), 2);
    CHECK_EQ(widget->videos()[0].id, "vid_001");
    CHECK_EQ(widget->videos()[1].id, "vid_002");

    int backCount = 0;
    QObject::connect(widget, &PlaylistPageWidget::backRequested, [&backCount]() {
        ++backCount;
    });
    emit widget->backRequested();
    CHECK_EQ(backCount, 1);

    int playAllCount = 0;
    QObject::connect(widget, &PlaylistPageWidget::playAllRequested, [&playAllCount]() {
        ++playAllCount;
    });
    emit widget->playAllRequested();
    CHECK_EQ(playAllCount, 1);

    int shuffleCount = 0;
    QObject::connect(widget, &PlaylistPageWidget::shuffleRequested, [&shuffleCount]() {
        ++shuffleCount;
    });
    emit widget->shuffleRequested();
    CHECK_EQ(shuffleCount, 1);

    // Test PlaylistItem details update
    PlaylistItem details;
    details.id = "PL123";
    details.title = "Synthwave Beats (Updated)";
    details.author = "Lofi Records";
    details.videoCountText = "30 videos";
    details.viewCountText = "50K views";
    details.updatedTime = "Updated today";
    widget->setPlaylistDetails(details);
    CHECK_EQ(widget->currentPlaylist().title, "Synthwave Beats (Updated)");
    CHECK_EQ(widget->currentPlaylist().author, "Lofi Records");

    delete widget;
    std::cout << "[PASS] testPlaylistPageWidget\n";
}

void testLeftNavDrawerLifecycleAndSignals() {
    std::cout << "[RUN ] testLeftNavDrawerLifecycleAndSignals..." << std::endl;

    auto *parentWidget = new QWidget();
    parentWidget->resize(1200, 800);

    auto *drawer = new lightyt::ui::LeftNavDrawer(parentWidget);
    CHECK(!drawer->isOpen());
    CHECK_EQ(drawer->slideProgress(), 0.0);

    // Add entries across different sections
    drawer->addEntry("home", "Home", lightyt::ui::icons::home(), lightyt::ui::NavSectionId::Primary);
    drawer->addEntry("subscriptions", "Subscriptions", lightyt::ui::icons::subscriptions(),
                     lightyt::ui::NavSectionId::Primary);
    drawer->addEntry("history", "History", lightyt::ui::icons::history(), lightyt::ui::NavSectionId::You);
    drawer->addEntry("playlists", "Playlists", lightyt::ui::icons::playlists(), lightyt::ui::NavSectionId::You);
    drawer->addEntry("watch_later", "Watch later", lightyt::ui::icons::watchLater(), lightyt::ui::NavSectionId::You);
    drawer->addEntry("liked_videos", "Liked videos", lightyt::ui::icons::thumbUp(), lightyt::ui::NavSectionId::You);

    // Verify active entry tracking
    drawer->setActiveEntry("history");
    CHECK_EQ(drawer->activeEntry(), "history");

    drawer->setActiveEntry("home");
    CHECK_EQ(drawer->activeEntry(), "home");

    // Test open and toggle
    int openCount = 0;
    QObject::connect(drawer, &lightyt::ui::LeftNavDrawer::drawerOpened, [&openCount]() {
        ++openCount;
    });
    drawer->openDrawer();
    CHECK(drawer->isOpen());
    CHECK_EQ(openCount, 1);

    // Test toggle when open -> closes
    drawer->toggleDrawer();
    CHECK(!drawer->isOpen());

    // Test toggle when closed -> opens
    drawer->toggleDrawer();
    CHECK(drawer->isOpen());

    // Test entryClicked signal emission
    int clickCount = 0;
    QString lastClickedId;
    QObject::connect(drawer, &lightyt::ui::LeftNavDrawer::entryClicked, [&clickCount, &lastClickedId](const QString &id) {
        ++clickCount;
        lastClickedId = id;
    });
    // Emit entry click directly by calling slot simulation
    emit drawer->entryClicked("history");
    CHECK_EQ(clickCount, 1);
    CHECK_EQ(lastClickedId, "history");

    // Test Escape key event dismisses drawer
    drawer->openDrawer();
    CHECK(drawer->isOpen());
    QKeyEvent escEvent(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(drawer, &escEvent);
    CHECK(!drawer->isOpen());

    // Test mouse click on backdrop dismisses drawer
    drawer->openDrawer();
    CHECK(drawer->isOpen());
    // Click outside 240px drawer panel (e.g. at x=500, y=300)
    QMouseEvent clickBackdrop(QEvent::MouseButtonPress, QPointF(500, 300), QPointF(500, 300),
                             Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(drawer, &clickBackdrop);
    CHECK(!drawer->isOpen());

    delete parentWidget;
    std::cout << "[PASS] testLeftNavDrawerLifecycleAndSignals\n";
}

void testAppSettingsSidebarMinimal() {
    std::cout << "[RUN ] testAppSettingsSidebarMinimal..." << std::endl;
    auto &settings = AppSettings::instance();
    bool original = settings.getSidebarMinimal();

    settings.setSidebarMinimal(true);
    CHECK_EQ(settings.getSidebarMinimal(), true);
    CHECK(settings.containsKey("SidebarMinimal"));

    settings.setSidebarMinimal(false);
    CHECK_EQ(settings.getSidebarMinimal(), false);

    // Restore original state
    settings.setSidebarMinimal(original);
    std::cout << "[PASS] testAppSettingsSidebarMinimal\n";
}

void testNavSidebarModesAndWidth() {
    std::cout << "[RUN ] testNavSidebarModesAndWidth..." << std::endl;
    auto *sidebar = new lightyt::ui::NavSidebar();

    // Default mode: expanded (240px)
    CHECK(!sidebar->isMinimal());
    CHECK_EQ(sidebar->width(), 240);
    CHECK(sidebar->expandedWidget() != nullptr);
    CHECK(sidebar->minimalWidget() != nullptr);
    CHECK(!sidebar->expandedWidget()->isHidden());
    CHECK(sidebar->minimalWidget()->isHidden());

    // Switch to minimal mode (72px)
    sidebar->setMinimal(true);
    CHECK(sidebar->isMinimal());
    CHECK_EQ(sidebar->width(), 72);
    CHECK(sidebar->expandedWidget()->isHidden());
    CHECK(!sidebar->minimalWidget()->isHidden());

    // Toggle back to expanded (240px)
    sidebar->toggleMinimal();
    CHECK(!sidebar->isMinimal());
    CHECK_EQ(sidebar->width(), 240);
    CHECK(!sidebar->expandedWidget()->isHidden());
    CHECK(sidebar->minimalWidget()->isHidden());

    // Toggle to minimal (72px)
    sidebar->toggleMinimal();
    CHECK(sidebar->isMinimal());
    CHECK_EQ(sidebar->width(), 72);
    CHECK(sidebar->expandedWidget()->isHidden());
    CHECK(!sidebar->minimalWidget()->isHidden());

    delete sidebar;
    std::cout << "[PASS] testNavSidebarModesAndWidth\n";
}

void testNavSidebarActiveEntry() {
    std::cout << "[RUN ] testNavSidebarActiveEntry..." << std::endl;
    auto *sidebar = new lightyt::ui::NavSidebar();

    sidebar->addEntry("home", "Home", lightyt::ui::icons::home(), lightyt::ui::NavSectionId::Primary);
    sidebar->addEntry("subscriptions", "Subscriptions", lightyt::ui::icons::subscriptions(),
                     lightyt::ui::NavSectionId::Primary);
    sidebar->addEntry("history", "History", lightyt::ui::icons::history(), lightyt::ui::NavSectionId::You);
    sidebar->addEntry("playlists", "Playlists", lightyt::ui::icons::playlists(), lightyt::ui::NavSectionId::You);
    sidebar->addEntry("watch_later", "Watch later", lightyt::ui::icons::watchLater(), lightyt::ui::NavSectionId::You);
    sidebar->addEntry("liked_videos", "Liked videos", lightyt::ui::icons::thumbUp(), lightyt::ui::NavSectionId::You);

    // Set active entry to "playlists"
    sidebar->setActiveEntry("playlists");
    CHECK_EQ(sidebar->activeEntry(), "playlists");

    // Check buttons exist
    auto *expBtn = sidebar->expandedButton("playlists");
    auto *minBtn = sidebar->minimalButton("playlists");
    CHECK(expBtn != nullptr);
    CHECK(minBtn != nullptr);

    // Check active styles applied
    CHECK(expBtn->styleSheet().contains("#272727"));
    CHECK(minBtn->styleSheet().contains("#272727"));

    // Check other buttons have inactive styles
    auto *homeExpBtn = sidebar->expandedButton("home");
    auto *homeMinBtn = sidebar->minimalButton("home");
    CHECK(homeExpBtn != nullptr);
    CHECK(homeMinBtn != nullptr);
    CHECK(homeExpBtn->styleSheet().contains("transparent"));
    CHECK(homeMinBtn->styleSheet().contains("transparent"));

    // Verify minimal button text for liked_videos was formatted to "Liked"
    auto *likedMinBtn = sidebar->minimalButton("liked_videos");
    CHECK(likedMinBtn != nullptr);
    CHECK_EQ(likedMinBtn->text(), "Liked");

    // Switch active entry to "home"
    sidebar->setActiveEntry("home");
    CHECK_EQ(sidebar->activeEntry(), "home");
    CHECK(homeExpBtn->styleSheet().contains("#272727"));
    CHECK(homeMinBtn->styleSheet().contains("#272727"));
    CHECK(expBtn->styleSheet().contains("transparent"));
    CHECK(minBtn->styleSheet().contains("transparent"));

    delete sidebar;
    std::cout << "[PASS] testNavSidebarActiveEntry\n";
}

void testNavSidebarSignals() {
    std::cout << "[RUN ] testNavSidebarSignals..." << std::endl;
    auto *sidebar = new lightyt::ui::NavSidebar();

    sidebar->addEntry("home", "Home", lightyt::ui::icons::home(), lightyt::ui::NavSectionId::Primary);
    sidebar->addEntry("subscriptions", "Subscriptions", lightyt::ui::icons::subscriptions(),
                     lightyt::ui::NavSectionId::Primary);
    sidebar->addEntry("history", "History", lightyt::ui::icons::history(), lightyt::ui::NavSectionId::You);

    int clickCount = 0;
    QString lastClickedId;
    QObject::connect(sidebar, &lightyt::ui::NavSidebar::entryClicked,
                     [&clickCount, &lastClickedId](const QString &id) {
                         ++clickCount;
                         lastClickedId = id;
                     });

    // 1. Click expanded button
    auto *expSubBtn = sidebar->expandedButton("subscriptions");
    CHECK(expSubBtn != nullptr);
    expSubBtn->click();
    CHECK_EQ(clickCount, 1);
    CHECK_EQ(lastClickedId, "subscriptions");
    CHECK_EQ(sidebar->activeEntry(), "subscriptions");

    // 2. Switch to minimal mode and click minimal button
    sidebar->setMinimal(true);
    auto *minHistBtn = sidebar->minimalButton("history");
    CHECK(minHistBtn != nullptr);
    minHistBtn->click();
    CHECK_EQ(clickCount, 2);
    CHECK_EQ(lastClickedId, "history");
    CHECK_EQ(sidebar->activeEntry(), "history");

    delete sidebar;
    std::cout << "[PASS] testNavSidebarSignals\n";
}

}  // namespace

int main(int argc, char *argv[]) {
    qputenv("QT_PLUGIN_PATH", "C:/Qt/6.8.0/mingw_64/plugins");
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    std::cout << "Running UI Unit Tests...\n";
    testProceduralIcons();
    testPlaylistDelegateAndButtonRect();
    testPlaylistPageWidget();
    testLeftNavDrawerLifecycleAndSignals();
    testAppSettingsSidebarMinimal();
    testNavSidebarModesAndWidth();
    testNavSidebarActiveEntry();
    testNavSidebarSignals();

    std::cout << "\nResults: " << g_passCount << " passed, " << g_failCount << " failed.\n";
    return (g_failCount == 0) ? 0 : 1;
}
