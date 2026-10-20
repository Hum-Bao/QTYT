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
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QScreen>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <iostream>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

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
        m_cookieManager->setCookieFilePath(chosenCookiePath.string());
        LOG_INFO("[MainWindow] Loaded authenticated cookies.txt successfully ({})", chosenCookiePath.string());
    } else if (loaded) {
        m_cookieManager->setCookieFilePath(chosenCookiePath.string());
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
        m_client->setSelectedChannel(savedPageId.toStdString(), savedAuthUser, savedChannelName.toStdString());
        LOG_INFO("[MainWindow] Restored active channel profile: '{}' (pageId: '{}', authUser: {})",
                 savedChannelName.toStdString(), savedPageId.toStdString(), savedAuthUser);
    }

    setupUi();

    // Restore window geometry, state, and player preferences from AppSettings
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

    QTimer::singleShot(0, this, &MainWindow::loadHomeFeedAsync);
}

MainWindow::~MainWindow() {
    ++m_feedGeneration;
    if (m_playerWidget) {
        m_playerWidget->stopPlayback();
    }
    m_asyncPool.waitForDone();
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
    m_stackedWidget = new QStackedWidget(this);

    m_feedView = new FeedView(this);
    m_feedView->setHideShorts(hideShorts);
    connect(m_feedView, &FeedView::videoSelected, this, &MainWindow::onVideoSelected);
    connect(m_feedView, &FeedView::videoHovered, this, &MainWindow::onVideoHovered);
    connect(m_feedView, &FeedView::requestMoreVideos, this, &MainWindow::onLoadMoreVideosRequested);
    m_stackedWidget->addWidget(m_feedView);

    m_playerWidget = new PlayerWidget(this);
    m_playerWidget->setYouTubeClient(m_client.get());
    connect(m_playerWidget, &PlayerWidget::backRequested, this, &MainWindow::onBackToFeedRequested);
    connect(m_playerWidget, &PlayerWidget::fullscreenToggleRequested, this, &MainWindow::onFullscreenToggleRequested);
    m_stackedWidget->addWidget(m_playerWidget);

    mainLayout->addWidget(m_stackedWidget, 1);

    statusBar()->setStyleSheet(
        "QStatusBar { background-color: #0f0f0f; color: #717171; border-top: 1px solid #1f1f1f; font-size: 11px; }");
    setStatusText("Ready");
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

    if (tab == NavTab::Home) {
        m_homeButton->setIcon(lightyt::ui::icons::home(QColor(0x0f, 0x0f, 0x0f), 18));
        m_subsButton->setIcon(lightyt::ui::icons::subscriptions(QColor(0xf1, 0xf1, 0xf1), 18));
        m_homeButton->setStyleSheet(activeChip);
        m_subsButton->setStyleSheet(inactiveChip);
    } else if (tab == NavTab::Subscriptions) {
        m_homeButton->setIcon(lightyt::ui::icons::home(QColor(0xf1, 0xf1, 0xf1), 18));
        m_subsButton->setIcon(lightyt::ui::icons::subscriptions(QColor(0x0f, 0x0f, 0x0f), 18));
        m_homeButton->setStyleSheet(inactiveChip);
        m_subsButton->setStyleSheet(activeChip);
    } else {
        m_homeButton->setIcon(lightyt::ui::icons::home(QColor(0xf1, 0xf1, 0xf1), 18));
        m_subsButton->setIcon(lightyt::ui::icons::subscriptions(QColor(0xf1, 0xf1, 0xf1), 18));
        m_homeButton->setStyleSheet(inactiveChip);
        m_subsButton->setStyleSheet(inactiveChip);
    }
}

void MainWindow::setStatusText(const QString &msg) {
    statusBar()->showMessage(msg);
}

void MainWindow::loadHomeFeedAsync() {
    updateNavTabs(NavTab::Home);
    loadFeedAsync("FEwhat_to_watch", "Home Feed");
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
    dialog->exec();
}

void MainWindow::onChannelChanged(const QString &pageId, int authUserIndex, const QString &channelName) {
    if (m_client) {
        m_client->setSelectedChannel(pageId.toStdString(), authUserIndex, channelName.toStdString());
    }
    AppSettings::instance().setSelectedChannel(pageId, authUserIndex, channelName);
    AppSettings::instance().sync();
    updateAuthButtonUi();
    loadHomeFeedAsync();
}

void MainWindow::onCookiesUpdated() {
    updateAuthButtonUi();
    if (m_cookieManager && m_cookieManager->hasAuth()) {
        if (!m_cookieManager->saveToCanonicalAppData()) {
            LOG_WARN("[MainWindow] Failed to save updated cookies to AppData");
        }
    }
    loadHomeFeedAsync();
}

void MainWindow::onSubscriptionsClicked() {
    if (!m_cookieManager || !m_cookieManager->hasAuth()) {
        QMessageBox::information(
            this, "Authentication Required",
            "Viewing Subscriptions requires logging into YouTube with your cookies.txt.\n\n"
            "Please click the 'Guest Mode' button in the top right to paste or load your cookies.");
        return;
    }
    if (m_stackedWidget && m_stackedWidget->currentIndex() == 1 && m_activeNavTab == NavTab::Subscriptions) {
        onBackToFeedRequested();
        return;
    }
    updateNavTabs(NavTab::Subscriptions);
    loadFeedAsync("FEsubscriptions", "Subscriptions Feed");
}

void MainWindow::loadFeedAsync(const std::string &browseId, const QString &modeName) {
    if (m_playerWidget) {
        m_playerWidget->stopPlayback();
    }

    m_isLoadingFeed.store(true);
    setStatusText(QString("Fetching %1...").arg(modeName));
    m_stackedWidget->setCurrentIndex(0);

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
    if (m_playerWidget) {
        m_playerWidget->stopPlayback();
    }
    updateNavTabs(NavTab::Search);

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
    if (m_feedView) {
        m_feedView->cancelPendingThumbnails();
    }
    m_stackedWidget->setCurrentIndex(1);
    if (m_client) {
        m_client->recordWatchHistory(item.id);
    }
    // Pass target quality directly into playVideo
    m_playerWidget->playVideo(item, AppSettings::instance().getQuality(),
                              m_cookieManager ? m_cookieManager->getCookieFilePath() : "",
                              AppSettings::instance().getAllowAv1());
    setStatusText(QString("Playing: %1").arg(QString::fromUtf8(item.title.c_str())));
}

void MainWindow::onBackToFeedRequested() {
    if (m_isFullscreen) {
        onFullscreenToggleRequested();
    }
    if (m_playerWidget) {
        m_playerWidget->stopPlayback();
    }
    m_stackedWidget->setCurrentIndex(0);
    setStatusText("Ready");
}

void MainWindow::onFullscreenToggleRequested() {
    m_isFullscreen = !m_isFullscreen;
    if (m_isFullscreen) {
        m_wasMaximizedBeforeFullscreen = isMaximized();
        m_savedWindowGeometry = geometry();
        m_topBarWidget->hide();
        if (statusBar())
            statusBar()->hide();

#ifdef _WIN32
        HWND hwnd = reinterpret_cast<HWND>(winId());
        m_savedWinStyle = GetWindowLongPtr(hwnd, GWL_STYLE);
        LONG_PTR style = m_savedWinStyle;
        style &= ~(WS_OVERLAPPEDWINDOW | WS_MAXIMIZE | WS_MINIMIZE);
        style |= WS_POPUP;
        SetWindowLongPtr(hwnd, GWL_STYLE, style);

        QScreen *targetScreen = screen() ? screen() : QGuiApplication::primaryScreen();
        QRect screenGeom = targetScreen ? targetScreen->geometry() : geometry();

        // Setting the height to screenGeom.height() + 1 intentionally prevents Windows DWM
        // from classifying the window as an exclusive DirectFlip / Independent Flip target.
        // This completely eliminates the 2-3 second display blackout / mode renegotiation stall!
        int fsW = screenGeom.width();
        int fsH = screenGeom.height() + 1;

        SetWindowPos(hwnd, HWND_TOP, screenGeom.x(), screenGeom.y(), fsW, fsH, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        setGeometry(QRect(screenGeom.x(), screenGeom.y(), fsW, fsH));
#else
        setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
        QScreen *targetScreen = screen() ? screen() : QGuiApplication::primaryScreen();
        if (targetScreen) {
            QRect g = targetScreen->geometry();
            setGeometry(g.x(), g.y(), g.width(), g.height() + 1);
        }
        show();
#endif
    } else {
#ifdef _WIN32
        HWND hwnd = reinterpret_cast<HWND>(winId());
        SetWindowLongPtr(hwnd, GWL_STYLE, m_savedWinStyle);
        SetWindowPos(hwnd, nullptr, m_savedWindowGeometry.x(), m_savedWindowGeometry.y(), m_savedWindowGeometry.width(),
                     m_savedWindowGeometry.height(), SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        setGeometry(m_savedWindowGeometry);
        if (m_wasMaximizedBeforeFullscreen) {
            showMaximized();
        } else {
            showNormal();
        }
#else
        setWindowFlags(windowFlags() & ~Qt::FramelessWindowHint);
        if (m_wasMaximizedBeforeFullscreen) {
            showMaximized();
        } else {
            setGeometry(m_savedWindowGeometry);
            showNormal();
        }
#endif
        m_topBarWidget->show();
        if (statusBar())
            statusBar()->show();
    }
    if (m_playerWidget) {
        m_playerWidget->setFullscreen(m_isFullscreen);
    }
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (m_isFullscreen) {
        onFullscreenToggleRequested();
    }
    ++m_feedGeneration;  // Cancel any in-flight async watchers
    AppSettings::instance().setGeometry(saveGeometry());
    AppSettings::instance().setWindowState(saveState());
    if (m_playerWidget) {
        AppSettings::instance().setQuality(m_playerWidget->getQuality());
    }
    if (m_playerWidget) {
        AppSettings::instance().setVolume(m_playerWidget->getVolume());
    }
    AppSettings::instance().sync();
    QMainWindow::closeEvent(event);
}
