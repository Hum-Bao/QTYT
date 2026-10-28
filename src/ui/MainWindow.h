#pragma once

#include "../core/CookieManager.h"
#include "../core/YouTubeClient.h"
#include "CookieDialog.h"
#include "FeedView.h"
#include "PlayerWidget.h"

#include <QComboBox>
#include <QFutureWatcher>
#include <QLabel>
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

struct FeedTaskResult {
    FeedResult result;
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

protected:
    void closeEvent(QCloseEvent *event) override;

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
    enum class NavTab { Home, Subscriptions, Search };
    NavTab m_activeNavTab = NavTab::Home;

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
    QLineEdit *m_searchInput = nullptr;
    QPushButton *m_searchButton = nullptr;
    QPushButton *m_homeButton = nullptr;
    QPushButton *m_subsButton = nullptr;
    QPushButton *m_shortsFilterButton = nullptr;
    QPushButton *m_authButton = nullptr;

    QStackedWidget *m_stackedWidget = nullptr;
    FeedView *m_feedView = nullptr;
    PlayerWidget *m_playerWidget = nullptr;

    void setupUi();
    void setStatusText(const QString &msg);
    void updateAuthButtonUi();
    void updateNavTabs(NavTab tab);
    void loadHomeFeedAsync();
    void loadFeedAsync(const std::string &browseId, const QString &modeName);
    void executeSearchAsync(const QString &query);
};
