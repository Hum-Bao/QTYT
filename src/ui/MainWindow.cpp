#include "MainWindow.h"

#include "../core/StreamResolver.h"
#include "../utils/AppPaths.h"
#include "../utils/AppSettings.h"
#include "../utils/Logger.h"
#include "Theme.h"
#include "YouTubeIcons.h"

#include <QCloseEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QResizeEvent>
#include <QScreen>
#include <QShortcut>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <iostream>
#include <vector>

#include "../utils/StringUtils.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), m_cookieManager(std::make_shared<CookieManager>()),
      m_client(std::make_unique<YouTubeClient>(m_cookieManager)) {
    // Master AppData cookies.txt
    auto appData = lightyt::paths::getAppDataDir();
    auto canonicalAppDataCookies = appData / "cookies.txt";

    std::filesystem::path chosenCookiePath;
    std::error_code ec;
    bool loaded = false;

    // Check canonical AppData
    if (std::filesystem::is_regular_file(canonicalAppDataCookies, ec)) {
        if (m_cookieManager->loadFromFile(canonicalAppDataCookies)) {
            chosenCookiePath = canonicalAppDataCookies;
            loaded = true;
        }
    }

    if (loaded && m_cookieManager->hasAuth()) {
        m_cookieManager->setCookieFilePath(lightyt::utils::pathToUtf8(chosenCookiePath));
        LOG_INFO("[MainWindow] Loaded authenticated cookies.txt successfully ({})",
                 lightyt::utils::pathToUtf8(chosenCookiePath));
    } else if (loaded) {
        m_cookieManager->setCookieFilePath(lightyt::utils::pathToUtf8(chosenCookiePath));
        LOG_INFO("[MainWindow] Loaded cookies.txt (guest session)");
    } else {
        LOG_INFO("[MainWindow] No cookies.txt found. Starting in Guest Mode");
    }

    // Restore active channel profile from centralized settings
    QString savedPageId = AppSettings::instance().getSelectedChannelPageId();
    int savedAuthUser = AppSettings::instance().getSelectedAuthUserIndex();
    QString savedChannelName = AppSettings::instance().getSelectedChannelName();
    if (!savedPageId.isEmpty() && savedAuthUser < 0) {
        savedAuthUser = 0;
    }
    if (!savedPageId.isEmpty() || (savedAuthUser >= 0 && !savedChannelName.isEmpty())) {
        QByteArray pageIdBytes = savedPageId.toUtf8();
        QByteArray nameBytes = savedChannelName.toUtf8();
        m_client->setSelectedChannel(std::string_view(pageIdBytes.constData(), pageIdBytes.size()), savedAuthUser,
                                     std::string_view(nameBytes.constData(), nameBytes.size()));
        LOG_INFO("[MainWindow] Restored active channel profile: '{}' (pageId: '{}', authUser: {})",
                 std::string_view(nameBytes.constData(), nameBytes.size()),
                 std::string_view(pageIdBytes.constData(), pageIdBytes.size()), savedAuthUser);
    }

    StreamResolver::setCookieManager(m_cookieManager);

    setupUi();

    // Restore window geometry, state, and player preferences from AppSettings
    if (!AppSettings::instance().containsKey("allowAv1") && StreamResolver::hasJsRuntime()) {
        AppSettings::instance().setAllowAv1(true);
        LOG_INFO("[MainWindow] JavaScript runtime detected: enabling AV1/VP9 codecs by default");
    }

    auto geom = AppSettings::instance().getGeometry();
    if (!geom.isEmpty()) {
        restoreGeometry(geom);
    } else {
        resize(1240, 820);
    }
    auto state = AppSettings::instance().getWindowState();
    if (!state.isEmpty()) {
        restoreState(state);
    }

    QString savedQuality = AppSettings::instance().getQuality();
    m_playerWidget->setQuality(savedQuality);
    m_playerWidget->setVolume(AppSettings::instance().getVolume());

    QTimer::singleShot(0, this, [this]() { loadHomeFeedAsync(); });
}

MainWindow::~MainWindow() {
    ++m_feedGeneration;
    if (m_playerWidget) {
        m_playerWidget->stopPlayback();
    }
    m_asyncPool.waitForDone();
    if (m_cookieManager) {
        m_cookieManager->flushIfDirty();
    }
    AppSettings::instance().sync();
}

void MainWindow::setupUi() {
    setWindowTitle("QTYT");
    resize(1240, 820);
    setStyleSheet("QMainWindow { background-color: #0f0f0f; }");

    auto *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    auto *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Top Header & Navigation Masthead (56px height matching YouTube masthead)
    m_topBarWidget = new QWidget(this);
    m_topBarWidget->setObjectName("topBarWidget");
    m_topBarWidget->setAttribute(Qt::WA_StyledBackground, true);
    m_topBarWidget->setFixedHeight(56);
    m_topBarWidget->setStyleSheet("#topBarWidget { background-color: #0f0f0f; border: none; }");
    auto *topBarLayout = new QHBoxLayout(m_topBarWidget);
    topBarLayout->setContentsMargins(16, 0, 16, 0);
    topBarLayout->setSpacing(10);

    // 0. Hamburger Menu Button (sits to the left of QTYT logo)
    m_menuButton = new QPushButton(m_topBarWidget);
    m_menuButton->setObjectName("menuButton");
    m_menuButton->setFixedSize(40, 40);
    m_menuButton->setIcon(lightyt::ui::icons::menu(QColor(0xf1, 0xf1, 0xf1), 20));
    m_menuButton->setIconSize(QSize(20, 20));
    m_menuButton->setCursor(Qt::PointingHandCursor);
    m_menuButton->setToolTip("Guide");
    m_menuButton->setStyleSheet(
        "QPushButton {"
        "  background-color: transparent; border: none; border-radius: 20px; padding: 0px;"
        "}"
        "QPushButton:hover { background-color: rgba(255, 255, 255, 0.1); }"
        "QPushButton:pressed { background-color: rgba(255, 255, 255, 0.2); }");
    connect(m_menuButton, &QPushButton::clicked, this, &MainWindow::onMenuButtonClicked);
    topBarLayout->addWidget(m_menuButton);

    // 1. QTYT Brand Button (Text-only brand pill button navigating to Home)
    auto *brandButton = new QPushButton("QTYT", m_topBarWidget);
    brandButton->setFixedHeight(36);
    brandButton->setCursor(Qt::PointingHandCursor);
    brandButton->setStyleSheet(
        "QPushButton {"
        "  background-color: transparent; color: #f1f1f1; border: none; border-radius: 8px;"
        "  padding: 0px 12px; min-height: 36px; max-height: 36px; font-size: 18px; font-weight: 700;"
        "}"
        "QPushButton:hover { background-color: rgba(255, 255, 255, 0.08); }");
    connect(brandButton, &QPushButton::clicked, this, &MainWindow::onHomeRefreshClicked);
    topBarLayout->addWidget(brandButton);

    topBarLayout->addSpacing(4);

    // 2. Navigation Chips (Home & Subscriptions)
    m_homeButton = new QPushButton("Home", m_topBarWidget);
    m_homeButton->setIconSize(QSize(18, 18));
    m_homeButton->setFixedHeight(36);
    m_homeButton->setCursor(Qt::PointingHandCursor);
    connect(m_homeButton, &QPushButton::clicked, this, &MainWindow::onHomeRefreshClicked);
    topBarLayout->addWidget(m_homeButton);

    m_subsButton = new QPushButton("Subscriptions", m_topBarWidget);
    m_subsButton->setIconSize(QSize(18, 18));
    m_subsButton->setFixedHeight(36);
    m_subsButton->setCursor(Qt::PointingHandCursor);
    connect(m_subsButton, &QPushButton::clicked, this, &MainWindow::onSubscriptionsClicked);
    topBarLayout->addWidget(m_subsButton);

    updateNavTabs(NavTab::Home);

    topBarLayout->addStretch(1);

    // 3. Center Pill Search Bar (matching yt-searchbox)
    auto *searchPill = new QFrame(m_topBarWidget);
    searchPill->setObjectName("searchPill");
    searchPill->setAttribute(Qt::WA_StyledBackground, true);
    searchPill->setFixedHeight(40);
    searchPill->setMinimumWidth(280);
    searchPill->setMaximumWidth(600);
    searchPill->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    searchPill->setStyleSheet("#searchPill {"
                              "  background-color: #121212; border: 1px solid #303030; border-radius: 20px;"
                              "}");
    auto *searchPillLayout = new QHBoxLayout(searchPill);
    searchPillLayout->setContentsMargins(14, 0, 0, 0);
    searchPillLayout->setSpacing(0);

    m_searchInput = new QLineEdit(searchPill);
    m_searchInput->setPlaceholderText("Search");
    m_searchInput->setFixedHeight(38);
    m_searchInput->setStyleSheet(
        "QLineEdit {"
        "  background: transparent; color: #f1f1f1; border: none; padding: 0px 8px 0px 2px; font-size: 14px;"
        "}");
    connect(m_searchInput, &QLineEdit::returnPressed, this, &MainWindow::onSearchSubmitted);
    searchPillLayout->addWidget(m_searchInput, 1);

    m_searchButton = new QPushButton(searchPill);
    m_searchButton->setFocusPolicy(Qt::NoFocus);
    m_searchButton->setIcon(lightyt::ui::icons::search(QColor(0xf1, 0xf1, 0xf1), 20));
    m_searchButton->setIconSize(QSize(20, 20));
    m_searchButton->setToolTip("Search");
    m_searchButton->setCursor(Qt::PointingHandCursor);
    m_searchButton->setFixedSize(64, 38);
    m_searchButton->setStyleSheet("QPushButton {"
                                  "  background-color: #222222; border: none; border-left: 1px solid #303030;"
                                  "  border-top-left-radius: 0px; border-bottom-left-radius: 0px;"
                                  "  border-top-right-radius: 19px; border-bottom-right-radius: 19px;"
                                  "  padding: 0px; min-height: 38px; max-height: 38px; outline: none;"
                                  "}"
                                  "QPushButton:hover {"
                                  "  background-color: #303030;"
                                  "  border-top-left-radius: 0px; border-bottom-left-radius: 0px;"
                                  "  border-top-right-radius: 19px; border-bottom-right-radius: 19px;"
                                  "  outline: none;"
                                  "}"
                                  "QPushButton:pressed {"
                                  "  background-color: #3f3f3f;"
                                  "  border-top-left-radius: 0px; border-bottom-left-radius: 0px;"
                                  "  border-top-right-radius: 19px; border-bottom-right-radius: 19px;"
                                  "  outline: none;"
                                  "}"
                                  "QPushButton:focus { outline: none; }");
    connect(m_searchButton, &QPushButton::clicked, this, &MainWindow::onSearchSubmitted);
    searchPillLayout->addWidget(m_searchButton);

    topBarLayout->addWidget(searchPill, 3);

    topBarLayout->addStretch(1);

    // 4. Shorts Filter Toggle Chip
    bool hideShorts = AppSettings::instance().getHideShorts();
    m_shortsFilterButton = new QPushButton(m_topBarWidget);
    m_shortsFilterButton->setCheckable(true);
    m_shortsFilterButton->setChecked(hideShorts);
    m_shortsFilterButton->setIconSize(QSize(18, 18));
    m_shortsFilterButton->setFixedHeight(36);
    m_shortsFilterButton->setCursor(Qt::PointingHandCursor);
    updateShortsButtonUi();
    connect(m_shortsFilterButton, &QPushButton::clicked, this, &MainWindow::onShortsFilterToggled);
    topBarLayout->addWidget(m_shortsFilterButton);

    // 5. User Profile / Sign-In Pill
    m_authButton = new QPushButton(m_topBarWidget);
    m_authButton->setIconSize(QSize(18, 18));
    m_authButton->setFixedHeight(36);
    m_authButton->setCursor(Qt::PointingHandCursor);
    updateAuthButtonUi();
    connect(m_authButton, &QPushButton::clicked, this, &MainWindow::onAuthButtonClicked);
    topBarLayout->addWidget(m_authButton);

    mainLayout->addWidget(m_topBarWidget);

    // Center Stacked Area: Feed (Index 0) vs. Player (Index 1)
    auto *bodyContainer = new QWidget(centralWidget);
    auto *bodyLayout = new QHBoxLayout(bodyContainer);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    // Persistent Navigation Sidebar (Docked on left of content area)
    m_persistentSidebar = new lightyt::ui::NavSidebar(bodyContainer);
    m_persistentSidebar->addEntry("home", "Home", lightyt::ui::icons::home(QColor(0xf1, 0xf1, 0xf1), 20),
                                  lightyt::ui::NavSectionId::Primary);
    m_persistentSidebar->addEntry("subscriptions", "Subscriptions",
                                  lightyt::ui::icons::subscriptions(QColor(0xf1, 0xf1, 0xf1), 20),
                                  lightyt::ui::NavSectionId::Primary);
    m_persistentSidebar->addEntry("history", "History", lightyt::ui::icons::history(QColor(0xf1, 0xf1, 0xf1), 20),
                                  lightyt::ui::NavSectionId::You);
    m_persistentSidebar->addEntry("playlists", "Playlists", lightyt::ui::icons::playlists(QColor(0xf1, 0xf1, 0xf1), 20),
                                  lightyt::ui::NavSectionId::You);
    m_persistentSidebar->addEntry("watch_later", "Watch later",
                                  lightyt::ui::icons::watchLater(QColor(0xf1, 0xf1, 0xf1), 20),
                                  lightyt::ui::NavSectionId::You);
    m_persistentSidebar->addEntry("liked_videos", "Liked videos",
                                  lightyt::ui::icons::thumbUp(QColor(0xf1, 0xf1, 0xf1), false, 20),
                                  lightyt::ui::NavSectionId::You);
    m_persistentSidebar->setActiveEntry("home");
    m_persistentSidebar->setMinimal(AppSettings::instance().getSidebarMinimal());
    connect(m_persistentSidebar, &lightyt::ui::NavSidebar::entryClicked, this, &MainWindow::onDrawerEntryClicked);
    bodyLayout->addWidget(m_persistentSidebar);

    m_stackedWidget = new QStackedWidget(bodyContainer);

    m_feedContainer = new QWidget(this);
    auto *feedLayout = new QVBoxLayout(m_feedContainer);
    feedLayout->setContentsMargins(0, 0, 0, 0);
    feedLayout->setSpacing(0);

    // Contextual Playlist Navigation Header Bar (appears when viewing a playlist's videos)
    m_playlistHeaderWidget = new QWidget(m_feedContainer);
    m_playlistHeaderWidget->setObjectName("playlistHeaderWidget");
    m_playlistHeaderWidget->setFixedHeight(52);
    m_playlistHeaderWidget->setStyleSheet(
        "#playlistHeaderWidget { background-color: #0f0f0f; border-bottom: 1px solid #222222; }");
    auto *playlistHeaderLayout = new QHBoxLayout(m_playlistHeaderWidget);
    playlistHeaderLayout->setContentsMargins(16, 0, 16, 0);
    playlistHeaderLayout->setSpacing(14);

    m_playlistBackButton = new QPushButton("← Back to Playlists", m_playlistHeaderWidget);
    m_playlistBackButton->setFixedHeight(34);
    m_playlistBackButton->setCursor(Qt::PointingHandCursor);
    m_playlistBackButton->setStyleSheet(
        "QPushButton {"
        "  background-color: #272727; color: #f1f1f1; border: 1px solid rgba(255, 255, 255, 0.1);"
        "  border-radius: 17px; padding: 0px 16px; font-size: 13px; font-weight: 500;"
        "}"
        "QPushButton:hover { background-color: #3f3f3f; color: #ffffff; }"
        "QPushButton:pressed { background-color: #4f4f4f; }");
    connect(m_playlistBackButton, &QPushButton::clicked, this, &MainWindow::onBackToPlaylistsClicked);
    playlistHeaderLayout->addWidget(m_playlistBackButton);

    m_playlistTitleLabel = new QLabel(m_playlistHeaderWidget);
    m_playlistTitleLabel->setStyleSheet(
        "color: #f1f1f1; font-size: 16px; font-weight: 600; border: none; background: transparent;");
    playlistHeaderLayout->addWidget(m_playlistTitleLabel);
    playlistHeaderLayout->addStretch(1);

    m_playlistHeaderWidget->hide();
    feedLayout->addWidget(m_playlistHeaderWidget);

    m_feedView = new FeedView(m_feedContainer);
    m_feedView->setHideShorts(hideShorts);
    connect(m_feedView, &FeedView::videoSelected, this, &MainWindow::onVideoSelected);
    connect(m_feedView, &FeedView::videoHovered, this, &MainWindow::onVideoHovered);
    connect(m_feedView, &FeedView::requestMoreVideos, this, &MainWindow::onLoadMoreVideosRequested);
    feedLayout->addWidget(m_feedView, 1);

    m_stackedWidget->addWidget(m_feedContainer);

    m_playerWidget = new PlayerWidget(this);
    m_playerWidget->setYouTubeClient(m_client.get());
    connect(m_playerWidget, &PlayerWidget::backRequested, this, &MainWindow::onBackToFeedRequested);
    connect(m_playerWidget, &PlayerWidget::fullscreenToggleRequested, this, &MainWindow::onFullscreenToggleRequested);
    m_stackedWidget->addWidget(m_playerWidget);

    m_playlistPageWidget = new PlaylistPageWidget(this);
    connect(m_playlistPageWidget, &PlaylistPageWidget::backRequested, this, &MainWindow::onPlaylistPageBackRequested);
    connect(m_playlistPageWidget, &PlaylistPageWidget::videoSelected, this, &MainWindow::onPlaylistVideoSelected);
    connect(m_playlistPageWidget, &PlaylistPageWidget::requestMoreVideos, this, &MainWindow::onPlaylistRequestMoreVideos);
    m_stackedWidget->addWidget(m_playlistPageWidget);

    connect(m_feedView, &FeedView::viewFullPlaylistRequested, this, &MainWindow::openPlaylistPage);

    bodyLayout->addWidget(m_stackedWidget, 1);
    mainLayout->addWidget(bodyContainer, 1);

    // Window-scoped keyboard shortcuts with text input and active page guards
    auto *shortcutTheater = new QShortcut(QKeySequence(Qt::Key_T), this);
    shortcutTheater->setContext(Qt::WindowShortcut);
    connect(shortcutTheater, &QShortcut::activated, this, [this]() {
        if (m_stackedWidget->currentWidget() != m_playerWidget || isTextInputFocused()) {
            return;
        }
        if (m_playerWidget) {
            m_playerWidget->toggleTheaterMode();
        }
    });

    auto *shortcutNext = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_N), this);
    shortcutNext->setContext(Qt::WindowShortcut);
    connect(shortcutNext, &QShortcut::activated, this, [this]() {
        if (m_stackedWidget->currentWidget() != m_playerWidget || isTextInputFocused()) {
            return;
        }
        if (m_playerWidget) {
            m_playerWidget->playNextVideo();
        }
    });

    auto *shortcutF11 = new QShortcut(QKeySequence(Qt::Key_F11), this);
    shortcutF11->setContext(Qt::WindowShortcut);
    connect(shortcutF11, &QShortcut::activated, this, &MainWindow::onFullscreenToggleRequested);

    auto *shortcutSearch = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_F), this);
    shortcutSearch->setContext(Qt::WindowShortcut);
    connect(shortcutSearch, &QShortcut::activated, this, [this]() {
        if (m_searchInput) {
            m_searchInput->setFocus();
            m_searchInput->selectAll();
        }
    });

    // 6. Sliding Left Navigation Drawer (240px overlay with dimmed backdrop)
    m_leftDrawer = new lightyt::ui::LeftNavDrawer(this);
    m_leftDrawer->addEntry("home", "Home", lightyt::ui::icons::home(QColor(0xf1, 0xf1, 0xf1), 20),
                           lightyt::ui::NavSectionId::Primary);
    m_leftDrawer->addEntry("subscriptions", "Subscriptions",
                           lightyt::ui::icons::subscriptions(QColor(0xf1, 0xf1, 0xf1), 20),
                           lightyt::ui::NavSectionId::Primary);
    m_leftDrawer->addEntry("history", "History", lightyt::ui::icons::history(QColor(0xf1, 0xf1, 0xf1), 20),
                           lightyt::ui::NavSectionId::You);
    m_leftDrawer->addEntry("playlists", "Playlists", lightyt::ui::icons::playlists(QColor(0xf1, 0xf1, 0xf1), 20),
                           lightyt::ui::NavSectionId::You);
    m_leftDrawer->addEntry("watch_later", "Watch later",
                           lightyt::ui::icons::watchLater(QColor(0xf1, 0xf1, 0xf1), 20),
                           lightyt::ui::NavSectionId::You);
    m_leftDrawer->addEntry("liked_videos", "Liked videos",
                           lightyt::ui::icons::thumbUp(QColor(0xf1, 0xf1, 0xf1), false, 20),
                           lightyt::ui::NavSectionId::You);
    m_leftDrawer->setActiveEntry("home");

    connect(m_leftDrawer, &lightyt::ui::LeftNavDrawer::entryClicked, this, &MainWindow::onDrawerEntryClicked);

    statusBar()->setStyleSheet(
        "QStatusBar { background-color: #0f0f0f; color: #717171; border-top: 1px solid #1f1f1f; font-size: 11px; }");
    setStatusText("Ready");
}

bool MainWindow::isTextInputFocused() const {
    QWidget *fw = focusWidget();
    if (!fw) {
        return false;
    }
    return fw->inherits("QLineEdit") || fw->inherits("QTextEdit") || fw->inherits("QPlainTextEdit");
}

void MainWindow::updateShortsButtonUi() {
    bool hide = m_shortsFilterButton->isChecked();
    if (hide) {
        m_shortsFilterButton->setIcon(lightyt::ui::icons::shorts(QColor(0xaa, 0xaa, 0xaa), 18));
        m_shortsFilterButton->setText("Shorts: Off");
        m_shortsFilterButton->setStyleSheet(
            "QPushButton {"
            "  background-color: #181818; color: #aaaaaa; border: 1px solid #303030; font-weight: 500;"
            "  padding: 0px 14px; border-radius: 18px; min-height: 34px; max-height: 34px; font-size: 13px;"
            "}"
            "QPushButton:hover { background-color: #272727; color: #f1f1f1; }");
    } else {
        m_shortsFilterButton->setIcon(lightyt::ui::icons::shorts(QColor(0xff, 0x00, 0x00), 18));
        m_shortsFilterButton->setText("Shorts");
        m_shortsFilterButton->setStyleSheet(
            "QPushButton {"
            "  background-color: #272727; color: #f1f1f1; border: 1px solid rgba(255, 255, 255, 0.1); font-weight: 500;"
            "  padding: 0px 14px; border-radius: 18px; min-height: 34px; max-height: 34px; font-size: 13px;"
            "}"
            "QPushButton:hover { background-color: #3f3f3f; }");
    }
}

void MainWindow::updateAuthButtonUi() {
    bool hasAuth = m_cookieManager && m_cookieManager->hasAuth();
    if (hasAuth) {
        m_authButton->setIcon(lightyt::ui::icons::userAvatar(QColor(0x3e, 0xa6, 0xff), 18));
        std::string activeName = m_client ? m_client->getSelectedChannelName() : "";
        if (!activeName.empty()) {
            m_authButton->setText(QString::fromUtf8(activeName.c_str()));
        } else {
            m_authButton->setText("Account");
        }
        m_authButton->setStyleSheet(
            "QPushButton {"
            "  background-color: #272727; color: #f1f1f1; border: 1px solid rgba(62, 166, 255, 0.45);"
            "  font-weight: 600; padding: 0px 14px; border-radius: 18px; min-height: 34px; max-height: 34px; "
            "font-size: 13px;"
            "}"
            "QPushButton:hover { background-color: #3f3f3f; border-color: #3ea6ff; }");
    } else {
        m_authButton->setIcon(lightyt::ui::icons::userAvatar(QColor(0x3e, 0xa6, 0xff), 18));
        m_authButton->setText("Sign in");
        m_authButton->setStyleSheet(
            "QPushButton {"
            "  background-color: transparent; color: #3ea6ff; border: 1px solid rgba(255, 255, 255, 0.22);"
            "  font-weight: 600; padding: 0px 14px; border-radius: 18px; min-height: 34px; max-height: 34px; "
            "font-size: 13px;"
            "}"
            "QPushButton:hover { background-color: rgba(62, 166, 255, 0.18); border-color: rgba(62, 166, 255, 0.35); "
            "}");
    }
}

void MainWindow::updateNavTabs(NavTab tab) {
    m_activeNavTab = tab;
    const QString activeChip =
        "QPushButton {"
        "  background-color: #f1f1f1; color: #0f0f0f; border: 1px solid #f1f1f1; font-weight: 600;"
        "  padding: 0px 14px; border-radius: 18px; min-height: 34px; max-height: 34px; font-size: 13px;"
        "}"
        "QPushButton:hover { background-color: #ffffff; }";
    const QString inactiveChip =
        "QPushButton {"
        "  background-color: #272727; color: #f1f1f1; border: 1px solid rgba(255, 255, 255, 0.08); font-weight: 500;"
        "  padding: 0px 14px; border-radius: 18px; min-height: 34px; max-height: 34px; font-size: 13px;"
        "}"
        "QPushButton:hover { background-color: #3f3f3f; }";

    m_activeNavTab = tab;
    QString entryId;
    if (tab == NavTab::Home) {
        entryId = "home";
        m_homeButton->setIcon(lightyt::ui::icons::home(QColor(0x0f, 0x0f, 0x0f), 18));
        m_subsButton->setIcon(lightyt::ui::icons::subscriptions(QColor(0xf1, 0xf1, 0xf1), 18));
        m_homeButton->setStyleSheet(activeChip);
        m_subsButton->setStyleSheet(inactiveChip);
    } else if (tab == NavTab::Subscriptions) {
        entryId = "subscriptions";
        m_homeButton->setIcon(lightyt::ui::icons::home(QColor(0xf1, 0xf1, 0xf1), 18));
        m_subsButton->setIcon(lightyt::ui::icons::subscriptions(QColor(0x0f, 0x0f, 0x0f), 18));
        m_homeButton->setStyleSheet(inactiveChip);
        m_subsButton->setStyleSheet(activeChip);
    } else {
        if (tab == NavTab::History) {
            entryId = "history";
        } else if (tab == NavTab::Playlists) {
            entryId = "playlists";
        } else if (tab == NavTab::WatchLater) {
            entryId = "watch_later";
        } else if (tab == NavTab::LikedVideos) {
            entryId = "liked_videos";
        } else {
            entryId = "";
        }
        m_homeButton->setIcon(lightyt::ui::icons::home(QColor(0xf1, 0xf1, 0xf1), 18));
        m_subsButton->setIcon(lightyt::ui::icons::subscriptions(QColor(0xf1, 0xf1, 0xf1), 18));
        m_homeButton->setStyleSheet(inactiveChip);
        m_subsButton->setStyleSheet(inactiveChip);
    }

    if (m_persistentSidebar) {
        m_persistentSidebar->setActiveEntry(entryId);
    }
    if (m_leftDrawer) {
        m_leftDrawer->setActiveEntry(entryId);
    }
}

void MainWindow::setStatusText(const QString &msg) {
    statusBar()->showMessage(msg);
}

void MainWindow::loadHomeFeedAsync() {
    loadHomeFeedAsync(true);
}

void MainWindow::loadHomeFeedAsync(bool switchToFeed) {
    if (m_playlistHeaderWidget) {
        m_playlistHeaderWidget->hide();
    }
    if (switchToFeed) {
        updateNavTabs(NavTab::Home);
    }
    loadFeedAsync("FEwhat_to_watch", "Home Feed", switchToFeed);
}

void MainWindow::onShortsFilterToggled() {
    bool hide = m_shortsFilterButton->isChecked();
    if (m_feedView) {
        m_feedView->setHideShorts(hide);
    }
    updateShortsButtonUi();
    AppSettings::instance().setHideShorts(hide);
}

void MainWindow::onAuthButtonClicked() {
    auto *dialog = new CookieDialog(m_cookieManager, m_client.get(), this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &CookieDialog::cookiesUpdated, this, &MainWindow::onCookiesUpdated);
    connect(dialog, &CookieDialog::channelChanged, this, &MainWindow::onChannelChanged);
    connect(dialog, &CookieDialog::allowAv1Changed, this, [this](bool allow) {
        if (m_playerWidget) {
            m_playerWidget->setAllowAv1(allow);
        }
    });
    connect(dialog, &CookieDialog::defaultQualityChanged, this, [this](const QString &quality) {
        if (m_playerWidget) {
            m_playerWidget->setDefaultQualityPreference(quality);
        }
    });
    dialog->exec();
}

void MainWindow::onChannelChanged(const QString &pageId, int authUserIndex, const QString &channelName) {
    if (m_client) {
        QByteArray pageIdBytes = pageId.toUtf8();
        QByteArray nameBytes = channelName.toUtf8();
        m_client->setSelectedChannel(std::string_view(pageIdBytes.constData(), pageIdBytes.size()), authUserIndex,
                                     std::string_view(nameBytes.constData(), nameBytes.size()));
    }
    AppSettings::instance().setSelectedChannel(pageId, authUserIndex, channelName);
    AppSettings::instance().sync();
    updateAuthButtonUi();

    bool isWatchingVideo = (m_stackedWidget && m_stackedWidget->currentWidget() == m_playerWidget);
    loadHomeFeedAsync(/*switchToFeed=*/!isWatchingVideo);
}

void MainWindow::onCookiesUpdated() {
    updateAuthButtonUi();
    if (m_cookieManager && m_cookieManager->hasAuth()) {
        if (!m_cookieManager->saveToCanonicalAppData()) {
            LOG_WARN("[MainWindow] Failed to save updated cookies to AppData");
        }
    }
    bool isWatchingVideo = (m_stackedWidget && m_stackedWidget->currentWidget() == m_playerWidget);
    loadHomeFeedAsync(/*switchToFeed=*/!isWatchingVideo);
}

bool MainWindow::requireAuthForFeature(const QString &featureName) {
    if (!m_cookieManager || !m_cookieManager->hasAuth()) {
        QMessageBox::information(
            this, "Authentication Required",
            QString("Viewing %1 requires logging into YouTube with your cookies.txt.\n\n"
                    "Please click the 'Sign in' button in the top right to paste or load your cookies.")
                .arg(featureName));
        return false;
    }
    return true;
}

void MainWindow::onSubscriptionsClicked() {
    if (!requireAuthForFeature("Subscriptions")) {
        return;
    }
    if (m_playlistHeaderWidget) {
        m_playlistHeaderWidget->hide();
    }
    if (m_stackedWidget && m_stackedWidget->currentIndex() == 1 && m_activeNavTab == NavTab::Subscriptions) {
        onBackToFeedRequested();
        return;
    }
    updateNavTabs(NavTab::Subscriptions);
    loadFeedAsync("FEsubscriptions", "Subscriptions Feed");
}

void MainWindow::loadPlaylistsAsync() {
    if (m_playlistHeaderWidget) {
        m_playlistHeaderWidget->hide();
    }
    if (m_leftDrawer) {
        m_leftDrawer->closeDrawer();
    }
    if (m_persistentSidebar) {
        m_persistentSidebar->show();
    }
    if (m_playerWidget) {
        m_playerWidget->stopPlayback();
    }
    m_lastViewSource = ViewSource::Feed;
    m_stackedWidget->setCurrentWidget(m_feedContainer);

    updateNavTabs(NavTab::Playlists);
    m_isLoadingFeed.store(true);
    setStatusText("Fetching Playlists...");

    runAsync<PlaylistsTaskResult>(
        [this]() {
            std::string err;
            auto plRes = m_client->fetchPlaylistsWithContinuation(err);
            return PlaylistsTaskResult{std::move(plRes), std::move(err)};
        },
        [this](PlaylistsTaskResult res) {
            m_isLoadingFeed.store(false);
            if (!res.error.empty()) {
                setStatusText(QString("Error: %1").arg(QString::fromUtf8(res.error.c_str())));
                LOG_WARN("[MainWindow] Playlists error: {}", res.error);
            } else {
                m_currentPlaylists = res.result.playlists;
                m_playlistsContinuationToken = res.result.continuationToken;
                m_currentContinuationToken = res.result.continuationToken;

                std::vector<VideoItem> items;
                items.reserve(res.result.playlists.size());
                for (const auto &pl : res.result.playlists) {
                    items.push_back(pl.toVideoItem());
                }

                if (m_feedView) {
                    m_feedView->setVideos(items);
                }
                setStatusText(QString("Loaded %1 playlists").arg(items.size()));
            }
        });
}

void MainWindow::onBackToPlaylistsClicked() {
    if (m_playlistHeaderWidget) {
        m_playlistHeaderWidget->hide();
    }
    updateNavTabs(NavTab::Playlists);

    std::vector<VideoItem> items;
    items.reserve(m_currentPlaylists.size());
    for (const auto &pl : m_currentPlaylists) {
        items.push_back(pl.toVideoItem());
    }

    m_currentContinuationToken = m_playlistsContinuationToken;
    if (m_feedView) {
        m_feedView->setVideos(items);
    }
    setStatusText(QString("Loaded %1 playlists").arg(items.size()));
}

void MainWindow::loadFeedAsync(const std::string &browseId, const QString &modeName, bool switchToFeed) {
    if (switchToFeed) {
        if (m_leftDrawer) {
            m_leftDrawer->closeDrawer();
        }
        if (m_persistentSidebar) {
            m_persistentSidebar->show();
        }
        if (m_playerWidget) {
            m_playerWidget->stopPlayback();
        }
        m_stackedWidget->setCurrentIndex(0);
    }

    m_isLoadingFeed.store(true);
    setStatusText(QString("Fetching %1...").arg(modeName));

    runAsync<FeedTaskResult>(
        [this, browseId]() {
            std::string err;
            auto feedRes = m_client->fetchFeedWithContinuation(browseId, err);
            return FeedTaskResult{std::move(feedRes), std::move(err)};
        },
        [this, modeName](FeedTaskResult res) {
            m_isLoadingFeed.store(false);
            if (!res.error.empty()) {
                setStatusText(QString("Error: %1").arg(QString::fromUtf8(res.error.c_str())));
                LOG_WARN("[MainWindow] Feed error: {}", res.error);
            } else {
                m_currentContinuationToken = res.result.continuationToken;
                if (m_feedView) {
                    m_feedView->setVideos(res.result.videos);
                }
                if (!res.result.videos.empty() && !res.result.videos[0].id.empty()) {
                    StreamResolver::prefetch(res.result.videos[0].id,
                                             AppSettings::instance().getQuality().toStdString(),
                                             m_cookieManager ? m_cookieManager->getCookieFilePath() : "",
                                             AppSettings::instance().getAllowAv1());
                }
                setStatusText(QString("Loaded %1 videos (%2)").arg(res.result.videos.size()).arg(modeName));
            }
        });
}

void MainWindow::onLoadMoreVideosRequested() {
    if (m_currentContinuationToken.empty() || m_isLoadingContinuation.load()) {
        return;
    }

    if (m_activeNavTab == NavTab::Playlists && (!m_playlistHeaderWidget || !m_playlistHeaderWidget->isVisible())) {
        m_isLoadingContinuation.store(true);
        std::string token = m_currentContinuationToken;
        setStatusText("Loading more playlists...");

        uint64_t gen = m_feedGeneration;
        auto *watcher = new QFutureWatcher<PlaylistsTaskResult>(this);
        connect(watcher, &QFutureWatcher<PlaylistsTaskResult>::finished, this, [this, watcher, gen]() {
            m_isLoadingContinuation.store(false);
            if (gen == m_feedGeneration) {
                auto res = watcher->result();
                if (!res.error.empty()) {
                    setStatusText(QString("Error loading more playlists: %1").arg(QString::fromUtf8(res.error.c_str())));
                } else {
                    m_playlistsContinuationToken = res.result.continuationToken;
                    m_currentContinuationToken = res.result.continuationToken;
                    m_currentPlaylists.insert(m_currentPlaylists.end(), res.result.playlists.begin(),
                                              res.result.playlists.end());

                    std::vector<VideoItem> items;
                    items.reserve(res.result.playlists.size());
                    for (const auto &pl : res.result.playlists) {
                        items.push_back(pl.toVideoItem());
                    }
                    if (m_feedView) {
                        m_feedView->appendVideos(items);
                    }
                    setStatusText(QString("Loaded %1 playlists total").arg(m_currentPlaylists.size()));
                }
            }
            watcher->deleteLater();
        });
        watcher->setFuture(QtConcurrent::run(&m_asyncPool, [this, token]() {
            std::string err;
            auto plRes = m_client->fetchPlaylistsContinuation(token, err);
            return PlaylistsTaskResult{std::move(plRes), std::move(err)};
        }));
        return;
    }

    m_isLoadingContinuation.store(true);
    std::string token = m_currentContinuationToken;
    auto kind = (m_activeNavTab == NavTab::Search) ? ContinuationKind::Search : ContinuationKind::Browse;
    setStatusText("Loading more recommendations...");

    uint64_t gen = m_feedGeneration;
    auto *watcher = new QFutureWatcher<FeedTaskResult>(this);
    connect(watcher, &QFutureWatcher<FeedTaskResult>::finished, this, [this, watcher, gen]() {
        m_isLoadingContinuation.store(false);
        if (gen == m_feedGeneration) {
            auto res = watcher->result();
            if (!res.error.empty()) {
                setStatusText(QString("Error loading more: %1").arg(QString::fromUtf8(res.error.c_str())));
            } else {
                m_currentContinuationToken = res.result.continuationToken;
                if (m_feedView) {
                    m_feedView->appendVideos(res.result.videos);
                }
                setStatusText(QString("Loaded %1 additional videos").arg(res.result.videos.size()));
            }
        }
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(&m_asyncPool, [this, token, kind]() {
        std::string err;
        auto feedRes = m_client->fetchContinuation(token, err, kind);
        return FeedTaskResult{std::move(feedRes), std::move(err)};
    }));
}

void MainWindow::onSearchSubmitted() {
    QString query = m_searchInput->text().trimmed();
    if (query.isEmpty())
        return;
    executeSearchAsync(query);
}

void MainWindow::executeSearchAsync(const QString &query) {
    if (m_playlistHeaderWidget) {
        m_playlistHeaderWidget->hide();
    }
    if (m_playerWidget) {
        m_playerWidget->stopPlayback();
    }
    updateNavTabs(NavTab::Search);

    if (m_leftDrawer) {
        m_leftDrawer->closeDrawer();
    }
    if (m_persistentSidebar) {
        m_persistentSidebar->show();
    }
    m_isLoadingFeed.store(true);
    setStatusText(QString("Searching for \"%1\"...").arg(query));
    m_stackedWidget->setCurrentIndex(0);

    std::string q = query.toUtf8().constData();
    runAsync<FeedTaskResult>(
        [this, q]() {
            std::string err;
            auto searchRes = m_client->searchWithContinuation(q, err);
            return FeedTaskResult{std::move(searchRes), std::move(err)};
        },
        [this, query](FeedTaskResult res) {
            m_isLoadingFeed.store(false);
            if (!res.error.empty()) {
                setStatusText(QString("Search error: %1").arg(QString::fromUtf8(res.error.c_str())));
            } else {
                m_currentContinuationToken = res.result.continuationToken;
                if (m_feedView) {
                    m_feedView->setVideos(res.result.videos);
                }
                if (!res.result.videos.empty() && !res.result.videos[0].id.empty()) {
                    StreamResolver::prefetch(res.result.videos[0].id,
                                             AppSettings::instance().getQuality().toStdString(),
                                             m_cookieManager ? m_cookieManager->getCookieFilePath() : "",
                                             AppSettings::instance().getAllowAv1());
                }
                setStatusText(QString("Found %1 results for \"%2\"").arg(res.result.videos.size()).arg(query));
            }
        });
}

void MainWindow::onVideoHovered(const VideoItem &item) {
    if (!item.id.empty()) {
        StreamResolver::prefetch(item.id, AppSettings::instance().getQuality().toStdString(),
                                 m_cookieManager ? m_cookieManager->getCookieFilePath() : "",
                                 AppSettings::instance().getAllowAv1());
    }
}

void MainWindow::onHomeRefreshClicked() {
    if (m_stackedWidget && m_stackedWidget->currentIndex() == 1 && m_activeNavTab == NavTab::Home) {
        onBackToFeedRequested();
        return;
    }
    m_searchInput->clear();
    loadHomeFeedAsync();
}

void MainWindow::onVideoSelected(const VideoItem &item) {
    if (item.isPlaylist) {
        openPlaylistPage(item);
        return;
    }

    m_lastViewSource = ViewSource::Feed;
    if (m_feedView) {
        m_feedView->cancelPendingThumbnails();
    }
    if (m_leftDrawer) {
        m_leftDrawer->closeDrawer();
    }
    if (m_persistentSidebar) {
        m_persistentSidebar->hide();
    }
    m_stackedWidget->setCurrentWidget(m_playerWidget);
    if (m_client) {
        m_client->recordWatchHistory(item.id);
    }
    // Pass target quality directly into playVideo
    m_playerWidget->playVideo(item, AppSettings::instance().getQuality(),
                              m_cookieManager ? m_cookieManager->getCookieFilePath() : "",
                              AppSettings::instance().getAllowAv1());
    setStatusText(QString("Playing: %1").arg(QString::fromUtf8(item.title.c_str())));
}

void MainWindow::openPlaylistPage(const VideoItem &item) {
    if (m_feedView) {
        m_feedView->cancelPendingThumbnails();
    }
    if (m_leftDrawer) {
        m_leftDrawer->closeDrawer();
    }
    if (m_persistentSidebar) {
        m_persistentSidebar->show();
    }
    if (m_playerWidget) {
        m_playerWidget->stopPlayback();
    }
    m_lastViewSource = ViewSource::Playlist;
    if (m_playlistPageWidget) {
        m_playlistPageWidget->setPlaylist(item);
        m_stackedWidget->setCurrentWidget(m_playlistPageWidget);
    }
    setStatusText(QString("Loading playlist: %1...").arg(QString::fromUtf8(item.title.c_str())));

    std::string browseId = item.playlistBrowseId.empty() ? ("VL" + item.id) : item.playlistBrowseId;

    m_isLoadingFeed.store(true);
    runAsync<PlaylistFeedTaskResult>(
        [this, browseId]() {
            PlaylistFeedTaskResult res;
            if (m_client) {
                res.result = m_client->fetchPlaylistFeedWithContinuation(browseId, res.error);
            }
            return res;
        },
        [this, item, browseId](PlaylistFeedTaskResult res) {
            m_isLoadingFeed.store(false);
            if (!res.error.empty()) {
                setStatusText(QString("Playlist error: %1").arg(QString::fromUtf8(res.error.c_str())));
            } else {
                m_currentContinuationToken = res.result.feed.continuationToken;
                if (m_playlistPageWidget) {
                    if (!res.result.playlist.title.empty() || !res.result.playlist.videoCountText.empty()) {
                        m_playlistPageWidget->setPlaylistDetails(res.result.playlist);
                    }
                    m_playlistPageWidget->setVideos(res.result.feed.videos);
                }
                if (!res.result.feed.videos.empty() && !res.result.feed.videos[0].id.empty()) {
                    StreamResolver::prefetch(res.result.feed.videos[0].id,
                                             AppSettings::instance().getQuality().toStdString(),
                                             m_cookieManager ? m_cookieManager->getCookieFilePath() : "",
                                             AppSettings::instance().getAllowAv1());
                }
                setStatusText(QString("Loaded playlist \"%1\" (%2 videos)")
                                  .arg(QString::fromUtf8(item.title.c_str()))
                                  .arg(res.result.feed.videos.size()));
            }
        });
}

void MainWindow::onPlaylistPageBackRequested() {
    if (m_leftDrawer) {
        m_leftDrawer->closeDrawer();
    }
    if (m_persistentSidebar) {
        m_persistentSidebar->show();
    }
    m_stackedWidget->setCurrentWidget(m_feedContainer);
    m_lastViewSource = ViewSource::Feed;
    setStatusText("Ready");
}

void MainWindow::onPlaylistVideoSelected(const VideoItem &item) {
    m_lastViewSource = ViewSource::Playlist;
    if (m_feedView) {
        m_feedView->cancelPendingThumbnails();
    }
    if (m_leftDrawer) {
        m_leftDrawer->closeDrawer();
    }
    if (m_persistentSidebar) {
        m_persistentSidebar->hide();
    }
    m_stackedWidget->setCurrentWidget(m_playerWidget);
    if (m_client) {
        m_client->recordWatchHistory(item.id);
    }
    m_playerWidget->playVideo(item, AppSettings::instance().getQuality(),
                              m_cookieManager ? m_cookieManager->getCookieFilePath() : "",
                              AppSettings::instance().getAllowAv1());
    setStatusText(QString("Playing: %1").arg(QString::fromUtf8(item.title.c_str())));
}

void MainWindow::onPlaylistRequestMoreVideos() {
    if (m_currentContinuationToken.empty() || m_isLoadingContinuation.load())
        return;
    m_isLoadingContinuation.store(true);
    std::string token = m_currentContinuationToken;
    runAsync<FeedTaskResult>(
        [this, token]() {
            FeedTaskResult res;
            if (m_client) {
                res.result = m_client->fetchContinuation(token, ContinuationKind::Browse, res.error);
            }
            return res;
        },
        [this](FeedTaskResult res) {
            m_isLoadingContinuation.store(false);
            if (res.error.empty()) {
                m_currentContinuationToken = res.result.continuationToken;
                if (m_playlistPageWidget) {
                    m_playlistPageWidget->appendVideos(res.result.videos);
                }
            }
        });
}

void MainWindow::onBackToFeedRequested() {
    if (windowState().testFlag(Qt::WindowFullScreen)) {
        onFullscreenToggleRequested();
    }
    if (m_leftDrawer) {
        m_leftDrawer->closeDrawer();
    }
    if (m_persistentSidebar) {
        m_persistentSidebar->show();
    }
    if (m_playerWidget) {
        m_playerWidget->stopPlayback();
    }
    if (m_lastViewSource == ViewSource::Playlist && m_playlistPageWidget) {
        m_stackedWidget->setCurrentWidget(m_playlistPageWidget);
    } else {
        m_stackedWidget->setCurrentWidget(m_feedContainer);
    }
    setStatusText("Ready");
}

void MainWindow::onFullscreenToggleRequested() {
    const bool isCurrentlyFullScreen = windowState().testFlag(Qt::WindowFullScreen);
    if (!isCurrentlyFullScreen) {
        if (m_leftDrawer) {
            m_leftDrawer->closeDrawer();
        }
        if (m_persistentSidebar) {
            m_persistentSidebar->hide();
        }
        m_wasMaximizedBeforeFullscreen = isMaximized();
        m_prevGeometry = m_wasMaximizedBeforeFullscreen ? normalGeometry() : geometry();
        m_isFullscreen = true;

        m_topBarWidget->hide();
        if (statusBar()) {
            statusBar()->hide();
        }
        if (m_playerWidget) {
            m_playerWidget->setFullscreen(true);
        }

        showFullScreen();
    } else {
        m_isFullscreen = false;

        if (m_playerWidget) {
            m_playerWidget->setFullscreen(false);
        }
        m_topBarWidget->show();
        if (statusBar()) {
            statusBar()->show();
        }
        if (m_stackedWidget && m_stackedWidget->currentWidget() != m_playerWidget) {
            if (m_persistentSidebar) {
                m_persistentSidebar->show();
            }
        }

        if (m_wasMaximizedBeforeFullscreen) {
            showMaximized();
        } else {
            showNormal();
            if (m_prevGeometry.isValid()) {
                setGeometry(m_prevGeometry);
            }
        }
    }
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (windowState().testFlag(Qt::WindowFullScreen)) {
        onFullscreenToggleRequested();
    }
    ++m_feedGeneration;  // Cancel any in-flight async watchers
    AppSettings::instance().setGeometry(saveGeometry());
    AppSettings::instance().setWindowState(saveState());
    if (m_playerWidget) {
        AppSettings::instance().setVolume(m_playerWidget->getVolume());
    }
    if (m_cookieManager) {
        m_cookieManager->flushIfDirty();
    }
    AppSettings::instance().sync();
    QMainWindow::closeEvent(event);
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    if (m_leftDrawer) {
        m_leftDrawer->syncGeometry();
    }
}

void MainWindow::onMenuButtonClicked() {
    if (m_stackedWidget && m_stackedWidget->currentWidget() == m_playerWidget) {
        if (m_leftDrawer) {
            m_leftDrawer->toggleDrawer();
        }
    } else if (m_persistentSidebar) {
        m_persistentSidebar->toggleMinimal();
        AppSettings::instance().setSidebarMinimal(m_persistentSidebar->isMinimal());
        AppSettings::instance().sync();
    }
}

void MainWindow::onDrawerEntryClicked(const QString &id) {
    if (m_persistentSidebar) {
        m_persistentSidebar->setActiveEntry(id);
    }
    if (m_leftDrawer) {
        m_leftDrawer->setActiveEntry(id);
    }
    if (id == "home") {
        if (m_playlistHeaderWidget) {
            m_playlistHeaderWidget->hide();
        }
        onHomeRefreshClicked();
    } else if (id == "subscriptions") {
        if (m_playlistHeaderWidget) {
            m_playlistHeaderWidget->hide();
        }
        onSubscriptionsClicked();
    } else if (id == "history") {
        if (m_playlistHeaderWidget) {
            m_playlistHeaderWidget->hide();
        }
        if (!requireAuthForFeature("Watch History")) {
            return;
        }
        if (m_stackedWidget && m_stackedWidget->currentIndex() == 1 && m_activeNavTab == NavTab::History) {
            onBackToFeedRequested();
            return;
        }
        updateNavTabs(NavTab::History);
        loadFeedAsync("FEhistory", "Watch History");
    } else if (id == "playlists") {
        if (!requireAuthForFeature("Playlists")) {
            return;
        }
        if (m_stackedWidget && m_stackedWidget->currentIndex() == 1 && m_activeNavTab == NavTab::Playlists) {
            onBackToFeedRequested();
            return;
        }
        loadPlaylistsAsync();
    } else if (id == "watch_later") {
        if (m_playlistHeaderWidget) {
            m_playlistHeaderWidget->hide();
        }
        if (!requireAuthForFeature("Watch Later")) {
            return;
        }
        if (m_stackedWidget && m_stackedWidget->currentIndex() == 1 && m_activeNavTab == NavTab::WatchLater) {
            onBackToFeedRequested();
            return;
        }
        updateNavTabs(NavTab::WatchLater);
        loadFeedAsync("VLWL", "Watch Later");
    } else if (id == "liked_videos") {
        if (m_playlistHeaderWidget) {
            m_playlistHeaderWidget->hide();
        }
        if (!requireAuthForFeature("Liked Videos")) {
            return;
        }
        if (m_stackedWidget && m_stackedWidget->currentIndex() == 1 && m_activeNavTab == NavTab::LikedVideos) {
            onBackToFeedRequested();
            return;
        }
        updateNavTabs(NavTab::LikedVideos);
        loadFeedAsync("VLLL", "Liked Videos");
    }
}
