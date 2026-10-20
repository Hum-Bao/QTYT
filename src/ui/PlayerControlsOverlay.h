#pragma once

#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QSlider>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <QWidget>
#include <chrono>

class QEnterEvent;
class QHideEvent;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;

// Specialized progress bar with hover time preview tooltip, buffer track, and smooth scrubbing
class PlayerScrubber : public QWidget {
    Q_OBJECT
public:
    explicit PlayerScrubber(QWidget *parent = nullptr);

    void setDuration(double seconds);
    void setPosition(double seconds);
    void setBufferedPosition(double seconds);

    [[nodiscard]] bool isDragging() const noexcept { return m_isDragging; }
    [[nodiscard]] double getPosition() const noexcept { return m_position; }
    [[nodiscard]] double getDuration() const noexcept { return m_duration; }

signals:
    void seekRequested(double seconds);
    void seekLiveMoved(double seconds);
    void seekingChanged(bool isSeeking);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    double m_duration = 0.0;
    double m_position = 0.0;
    double m_bufferedPosition = 0.0;
    double m_hoverPosition = 0.0;
    bool m_isHovered = false;
    bool m_isDragging = false;
    int m_hoverMouseX = 0;

    static QString formatTime(double seconds);
    [[nodiscard]] double secondsFromX(int x) const noexcept;
};

// Specialized clickable volume slider that jumps immediately to clicked position and supports smooth dragging
class VolumeSlider : public QSlider {
    Q_OBJECT
public:
    explicit VolumeSlider(Qt::Orientation orientation, QWidget *parent = nullptr);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void updateValueFromPos(int x);
};

class QStackedWidget;

// YouTube-style tiered popup menu (Main Menu -> Quality / Speed submenus)
class PlayerSettingsPopup : public QWidget {
    Q_OBJECT
public:
    explicit PlayerSettingsPopup(QWidget *parent = nullptr);

    void setQuality(const QString &quality);
    void setAvailableQualities(const QStringList &qualities);
    void setPlaybackSpeed(double speed);
    void showAbove(QWidget *target);

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

signals:
    void qualitySelected(const QString &quality);
    void playbackSpeedSelected(double speed);
    void popupHidden();

protected:
    void hideEvent(QHideEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void setupUi();
    void buildMainMenu();
    void rebuildQualityMenu();
    void rebuildSpeedMenu();
    void reposition();

    QWidget *m_targetBtn = nullptr;
    QStackedWidget *m_stack = nullptr;

    // Main page
    QWidget *m_mainPage = nullptr;
    QLabel *m_qualityValLabel = nullptr;
    QLabel *m_speedValLabel = nullptr;

    // Quality page
    QWidget *m_qualityPage = nullptr;
    QWidget *m_qualityListContainer = nullptr;
    QVBoxLayout *m_qualityListLayout = nullptr;

    // Speed page
    QWidget *m_speedPage = nullptr;
    QWidget *m_speedListContainer = nullptr;
    QVBoxLayout *m_speedListLayout = nullptr;

    QString m_currentQuality{"1080p"};
    QStringList m_availableQualities{"Auto", "1080p", "720p", "480p", "360p", "Audio Only"};
    double m_currentSpeed = 1.0;
};

// YouTube-style bottom options bar overlay with expanding volume slider and smooth opacity fading
class PlayerControlsOverlay : public QWidget {
    Q_OBJECT
public:
    explicit PlayerControlsOverlay(QWidget *parent = nullptr);
    ~PlayerControlsOverlay() override = default;

    void setPlaying(bool playing);
    void setDuration(double seconds);
    void setPosition(double seconds);
    void setBufferedPosition(double seconds);
    void setVolume(int volume);
    void setFullscreen(bool isFullscreen);
    void setQuality(const QString &quality);
    void setAvailableQualities(const QStringList &qualities);
    void setPlaybackSpeed(double speed);

    void showControlsAnimated();
    void hideControlsAnimated();
    void scheduleHide(int delayMs = 150);

    [[nodiscard]] bool isInteracting() const noexcept;
    [[nodiscard]] bool isControlsVisible() const noexcept;

signals:
    void playPauseClicked();
    void seekRequested(double seconds);
    void seekLiveMoved(double seconds);
    void seekingChanged(bool isSeeking);
    void volumeChanged(int volume);
    void fullscreenToggleRequested();
    void controlsVisibilityChanged(bool visible);
    void qualityChanged(const QString &quality);
    void playbackSpeedChanged(double speed);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private slots:
    void onMuteClicked();
    void onSettingsClicked();
    void onHideTimerTimeout();

private:
    PlayerScrubber *m_scrubber = nullptr;
    QPushButton *m_playPauseBtn = nullptr;

    // Volume group with hover expansion
    QWidget *m_volumeArea = nullptr;
    QPushButton *m_muteBtn = nullptr;
    QWidget *m_sliderContainer = nullptr;
    VolumeSlider *m_volumeSlider = nullptr;
    QVariantAnimation *m_volExpandAnimation = nullptr;
    QTimer *m_volCollapseTimer = nullptr;
    bool m_isVolumeExpanded = false;

    QLabel *m_timeLabel = nullptr;
    QPushButton *m_settingsBtn = nullptr;
    QPushButton *m_fullscreenBtn = nullptr;
    PlayerSettingsPopup *m_settingsPopup = nullptr;
    bool m_isMenuOpen = false;

    QTimer *m_hideTimer = nullptr;
    QGraphicsOpacityEffect *m_opacityEffect = nullptr;
    QPropertyAnimation *m_fadeAnimation = nullptr;

    bool m_isPlaying = false;
    bool m_isFullscreen = false;
    int m_preMuteVolume = 100;
    double m_duration = 0.0;
    double m_position = 0.0;
    QString m_quality{"1080p"};
    QStringList m_availableQualities{"Auto", "1080p", "720p", "480p", "360p", "Audio Only"};
    double m_playbackSpeed = 1.0;

    void setupUi();
    void updateTimeLabel();
    void updateVolumeIcon();
    void animateVolumeExpand(bool expand);
    static QString formatTime(double seconds);
};
