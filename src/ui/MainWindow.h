#pragma once

#include "../core/CookieManager.h"
#include "../core/YouTubeClient.h"
#include "CookieDialog.h"
#include "FeedView.h"
#include "LeftNavDrawer.h"
#include "NavSidebar.h"
#include "PlayerWidget.h"
#include "PlaylistPageWidget.h"

#include <QFutureWatcher>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QStackedWidget>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrent>
#include <atomic>
#include <functional>
#include <memory>
#include <string>

class QKeyEvent;
class QLabel;
class QResizeEvent;

struct FeedTaskResult {
    FeedResult result;
    std::string error;
};

struct PlaylistsTaskResult {
    PlaylistsResult result;
    std::string error;
};

struct PlaylistFeedTaskResult {
    PlaylistFeedResult result;
    std::string error;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onSearchSubmitted();
    void onHomeRefreshClicked();
    void onSubscriptionsClicked();
    void onAuthButtonClicked();
    void onVideoSelected(const VideoItem &item);
    void onVideoHovered(const VideoItem &item);
    void onBackToFeedRequested();
    void onCookiesUpdated();
    void onChannelChanged(const QString &pageId, int authUserIndex, const QString &channelName);
    void onLoadMoreVideosRequested();
    void onFullscreenToggleRequested();
    void onShortsFilterToggled();
    void onMenuButtonClicked();
    void onDrawerEntryClicked(const QString &id);
    void onBackToPlaylistsClicked();
    void onPlaylistPageBackRequested();
    void onPlaylistVideoSelected(const VideoItem &item);
    void onPlaylistRequestMoreVideos();

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    template<typename T, typename Func>
    void runAsync(Func &&work, std::function<void(T)> onFinished) {
        uint64_t gen = ++m_feedGeneration;
        auto *watcher = new QFutureWatcher<T>(this);
        connect(watcher, &QFutureWatcher<T>::finished, this, [this, watcher, gen, onFinished]() {
            if (gen == m_feedGeneration) {
                onFinished(watcher->result());
            }
            watcher->deleteLater();
        });
        watcher->setFuture(QtConcurrent::run(&m_asyncPool, std::forward<Func>(work)));
    }

    bool isTextInputFocused() const;
    void updateShortsButtonUi();
    [[nodiscard]] bool requireAuthForFeature(const QString &featureName);
    enum class NavTab { Home, Subscriptions, History, Playlists, WatchLater, LikedVideos, Search };
    enum class ViewSource { Feed, Playlist };

    NavTab m_activeNavTab = NavTab::Home;
    ViewSource m_lastViewSource = ViewSource::Feed;

    std::shared_ptr<CookieManager> m_cookieManager;
    std::unique_ptr<YouTubeClient> m_client;
    QThreadPool m_asyncPool;

    uint64_t m_feedGeneration = 0;
    std::string m_currentContinuationToken;
    std::atomic<bool> m_isLoadingFeed{false};
    std::atomic<bool> m_isLoadingContinuation{false};
    bool m_isFullscreen = false;
    QRect m_prevGeometry;
    bool m_wasMaximizedBeforeFullscreen = false;

    QWidget *m_topBarWidget = nullptr;
    QPushButton *m_menuButton = nullptr;
    QLineEdit *m_searchInput = nullptr;
    QPushButton *m_searchButton = nullptr;
    QPushButton *m_homeButton = nullptr;
    QPushButton *m_subsButton = nullptr;
    QPushButton *m_shortsFilterButton = nullptr;
    QPushButton *m_authButton = nullptr;

    QStackedWidget *m_stackedWidget = nullptr;
    QWidget *m_feedContainer = nullptr;
    QWidget *m_playlistHeaderWidget = nullptr;
    QPushButton *m_playlistBackButton = nullptr;
    QLabel *m_playlistTitleLabel = nullptr;
    FeedView *m_feedView = nullptr;
    PlayerWidget *m_playerWidget = nullptr;
    PlaylistPageWidget *m_playlistPageWidget = nullptr;
    lightyt::ui::LeftNavDrawer *m_leftDrawer = nullptr;
    lightyt::ui::NavSidebar *m_persistentSidebar = nullptr;

    std::vector<PlaylistItem> m_currentPlaylists;
    std::string m_playlistsContinuationToken;

    void setupUi();
    void setStatusText(const QString &msg);
    void updateAuthButtonUi();
    void updateNavTabs(NavTab tab);
    void loadHomeFeedAsync();
    void loadHomeFeedAsync(bool switchToFeed);
    void loadPlaylistsAsync();
    void loadFeedAsync(const std::string &browseId, const QString &modeName, bool switchToFeed = true);
    void executeSearchAsync(const QString &query);
    void openPlaylistPage(const VideoItem &item);
};
