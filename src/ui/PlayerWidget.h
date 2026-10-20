#pragma once

#include "../core/VideoItem.h"
#include "../player/PlayerController.h"

#include <QCache>
#include <QKeyEvent>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QShowEvent>
#include <QSlider>
#include <QWidget>
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

class YouTubeClient;
class MpvGlWidget;
class QScrollArea;
class PlayerControlsOverlay;

class PlayerWidget : public QWidget {
    Q_OBJECT
public:
    explicit PlayerWidget(QWidget *parent = nullptr);
    ~PlayerWidget() override;

    void playVideo(const VideoItem &item, const QString &quality = "", const std::string &cookieFilePath = "",
                   bool allowAv1 = false);
    void setQuality(const QString &quality);
    [[nodiscard]] QString getQuality() const noexcept { return m_quality; }

    void setPlaybackSpeed(double speed);
    [[nodiscard]] double getPlaybackSpeed() const noexcept { return m_playbackSpeed; }

    void stopPlayback();

    void setYouTubeClient(YouTubeClient *client) noexcept { m_client = client; }

    [[nodiscard]] int getVolume() const;
    void setVolume(int value);

    void setFullscreen(bool fullscreen);

signals:
    void backRequested();
    void fullscreenToggleRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private slots:
    void onPlayPauseClicked();
    void onSeekRequested(double seconds);
    void onVolumeChanged(int value);
    void onQualityChanged(const QString &quality);
    void onPlaybackSpeedChanged(double speed);
    void onLikeClicked();
    void onDislikeClicked();
    void onShareClicked();
    void onSubscribeClicked();
    void onToggleDescriptionClicked();

private:
    QWidget *m_surfaceContainer = nullptr;
    MpvGlWidget *m_videoSurface = nullptr;
    QLabel *m_errorBanner = nullptr;
    PlayerControlsOverlay *m_controlsOverlay = nullptr;
    QWidget *m_metadataContainer = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_authorLabel = nullptr;
    QLabel *m_subscriberCountLabel = nullptr;
    QLabel *m_channelAvatarLabel = nullptr;
    QPushButton *m_subscribeBtn = nullptr;

    QPushButton *m_likeBtn = nullptr;
    QPushButton *m_dislikeBtn = nullptr;
    QPushButton *m_shareBtn = nullptr;

    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_descriptionCard = nullptr;
    QLabel *m_descriptionMetaLabel = nullptr;
    QLabel *m_descriptionTextLabel = nullptr;
    QPushButton *m_toggleDescriptionBtn = nullptr;
    QString m_quality{"1080p"};
    double m_playbackSpeed = 1.0;

    bool m_userIsSeeking = false;
    bool m_isFullscreen = false;
    int m_preMuteVolume = 100;
    double m_fallbackDuration = 0.0;
    std::chrono::steady_clock::time_point m_lastSeekTime{};
    std::atomic<uint64_t> m_generation{0};
    std::atomic<bool> m_destroying{false};
    std::shared_ptr<std::atomic<bool>> m_cancelToken;
    std::thread m_workerThread;
    std::thread m_metadataThread;
    std::thread m_avatarThread;

    QCache<QString, QPixmap> m_avatarCache{64};
    std::string m_loadedAvatarUrl;

    VideoItem m_currentItem;
    std::string m_cookieFilePath;
    bool m_allowAv1 = false;
    std::unique_ptr<PlayerController> m_player;
    YouTubeClient *m_client = nullptr;
    double m_lastReportedWatchTime = 0.0;

    bool m_isLiked = false;
    bool m_isDisliked = false;
    bool m_initialLiked = false;
    bool m_initialDisliked = false;
    bool m_isSubscribed = false;
    int64_t m_baseLikes = 0;
    bool m_descriptionExpanded = false;
    std::string m_fullDescription;

    void setupUi();
    void setupEvents();
    void updatePlayerLayout();
    void updateLikeDislikeUi();
    void updateDescriptionUi();
    void loadChannelAvatar(const std::string &avatarUrl);
    void setChannelAvatarPixmap(const QPixmap &pix);
    [[nodiscard]] double effectiveDuration() const noexcept;
    [[nodiscard]] static double parseDurationString(std::string_view durStr) noexcept;
    [[nodiscard]] static QString formatTime(double seconds);
    [[nodiscard]] static QString formatCount(int64_t count);
};
