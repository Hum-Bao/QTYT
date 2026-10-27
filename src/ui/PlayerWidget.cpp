#include "PlayerWidget.h"

#include "../core/HttpClient.h"
#include "../core/ThumbnailCache.h"
#include "../core/YouTubeClient.h"
#include "../core/StreamResolver.h"
#include "../utils/AppPaths.h"
#include "../utils/AppSettings.h"
#include "../utils/Logger.h"
#include "../utils/StringUtils.h"
#include "FeedView.h"
#include "MpvGlWidget.h"
#include "PlayerControlsOverlay.h"
#include "StatsForNerdsWidget.h"
#include "ThumbnailLoader.h"
#include "YouTubeIcons.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLocale>
#include <QMenu>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <algorithm>
#include <charconv>
#include <thread>

namespace {

const char *kPillBtnSecondaryQss = "QPushButton {"
                                   "  background-color: #272727;"
                                   "  color: #f1f1f1;"
                                   "  border: 1px solid rgba(255, 255, 255, 0.08);"
                                   "  min-height: 34px;"
                                   "  max-height: 34px;"
                                   "  padding: 0px 14px;"
                                   "  border-radius: 18px;"
                                   "  font-weight: 600;"
                                   "  font-size: 13px;"
                                   "  outline: none;"
                                   "}"
                                   "QPushButton:hover {"
                                   "  background-color: #3f3f3f;"
                                   "  border: 1px solid rgba(255, 255, 255, 0.08);"
                                   "  border-radius: 18px;"
                                   "  outline: none;"
                                   "}"
                                   "QPushButton:pressed {"
                                   "  background-color: #525252;"
                                   "  border: 1px solid rgba(255, 255, 255, 0.08);"
                                   "  border-radius: 18px;"
                                   "  outline: none;"
                                   "}"
                                   "QPushButton:focus { outline: none; }";

const char *kSubscribePrimaryQss = "QPushButton {"
                                   "  background-color: #f1f1f1;"
                                   "  color: #0f0f0f;"
                                   "  border: 1px solid #f1f1f1;"
                                   "  min-height: 34px;"
                                   "  max-height: 34px;"
                                   "  padding: 0px 16px;"
                                   "  border-radius: 18px;"
                                   "  font-weight: 600;"
                                   "  font-size: 13px;"
                                   "  outline: none;"
                                   "}"
                                   "QPushButton:hover {"
                                   "  background-color: #d9d9d9;"
                                   "  border: 1px solid #d9d9d9;"
                                   "  border-radius: 18px;"
                                   "  outline: none;"
                                   "}"
                                   "QPushButton:pressed {"
                                   "  background-color: #cccccc;"
                                   "  border: 1px solid #cccccc;"
                                   "  border-radius: 18px;"
                                   "  outline: none;"
                                   "}"
                                   "QPushButton:focus { outline: none; }";

}  // namespace

PlayerWidget::PlayerWidget(QWidget *parent) : QWidget(parent), m_player(std::make_unique<PlayerController>()) {
    setFocusPolicy(Qt::StrongFocus);
    setupUi();
    setupEvents();
}

PlayerWidget::~PlayerWidget() {
    m_destroying.store(true);
    if (m_cancelToken) {
        m_cancelToken->store(true);
    }
    if (m_videoSurface) {
        m_videoSurface->destroyRenderContext();
    }
    if (m_player) {
        m_player->setEvents({});
    }
    stopPlayback();
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
    if (m_metadataThread.joinable()) {
        m_metadataThread.join();
    }
    if (m_avatarThread.joinable()) {
        m_avatarThread.join();
    }
}

void PlayerWidget::setupUi() {
    setObjectName("playerWidgetRoot");
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet("#playerWidgetRoot { background-color: #0f0f0f; color: #f1f1f1; }");

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Unified Scrollable Watch Page (Video Player + Controls + Title + Actions + Description)
    // Scrolling moves the video player up just like YouTube's watch page so the full description is readable.
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setStyleSheet(
        "QScrollArea { background-color: transparent; border: none; padding: 0px; margin: 0px; }"
        "QScrollBar:vertical { background: #0f0f0f; width: 8px; margin: 0px; border: none; }"
        "QScrollBar::handle:vertical { background: #383838; border-radius: 4px; min-height: 24px; }"
        "QScrollBar::handle:vertical:hover { background: #555555; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; background: none; border: none; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }");

    auto *scrollContent = new QWidget(m_scrollArea);
    scrollContent->setObjectName("watchScrollContent");
    scrollContent->setStyleSheet("#watchScrollContent { background-color: transparent; }");
    m_watchGridLayout = new QGridLayout(scrollContent);
    m_watchGridLayout->setContentsMargins(0, 0, 0, 24);
    m_watchGridLayout->setHorizontalSpacing(16);
    m_watchGridLayout->setVerticalSpacing(8);

    // 1. Video Surface Container
    m_surfaceContainer = new QWidget(scrollContent);
    m_surfaceContainer->setObjectName("surfaceContainer");
    m_surfaceContainer->setAttribute(Qt::WA_StyledBackground, true);
    m_surfaceContainer->setStyleSheet("#surfaceContainer { background-color: #000000; border-radius: 0px; }");
    m_surfaceContainer->setMinimumHeight(280);
    auto *surfaceLayout = new QVBoxLayout(m_surfaceContainer);
    surfaceLayout->setContentsMargins(0, 0, 0, 0);
    surfaceLayout->setSpacing(0);

    m_errorBanner = new QLabel(m_surfaceContainer);
    m_errorBanner->setAlignment(Qt::AlignCenter);
    m_errorBanner->setWordWrap(true);
    m_errorBanner->setStyleSheet("QLabel {"
                                 "  background-color: #2b1818;"
                                 "  color: #f87171;"
                                 "  border: 1px solid #7f1d1d;"
                                 "  border-radius: 8px;"
                                 "  padding: 12px;"
                                 "  font-size: 13px;"
                                 "}");
    m_errorBanner->hide();
    surfaceLayout->addWidget(m_errorBanner);

    m_videoSurface = new MpvGlWidget(m_surfaceContainer);
    m_videoSurface->setStyleSheet("background-color: #000000;");
    m_videoSurface->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    connect(m_videoSurface, &MpvGlWidget::clicked, this, &PlayerWidget::onPlayPauseClicked);
    connect(m_videoSurface, &MpvGlWidget::doubleClicked, this, &PlayerWidget::fullscreenToggleRequested);
    surfaceLayout->addWidget(m_videoSurface, 1);

    m_surfaceContainer->setMouseTracking(true);
    m_videoSurface->setMouseTracking(true);
    m_surfaceContainer->installEventFilter(this);
    m_videoSurface->installEventFilter(this);

    m_controlsOverlay = new PlayerControlsOverlay(m_surfaceContainer);
    connect(m_controlsOverlay, &PlayerControlsOverlay::playPauseClicked, this, &PlayerWidget::onPlayPauseClicked);
    connect(m_controlsOverlay, &PlayerControlsOverlay::seekRequested, this, &PlayerWidget::onSeekRequested);
    connect(m_controlsOverlay, &PlayerControlsOverlay::volumeChanged, this, &PlayerWidget::onVolumeChanged);
    connect(m_controlsOverlay, &PlayerControlsOverlay::fullscreenToggleRequested, this,
            &PlayerWidget::fullscreenToggleRequested);
    connect(m_controlsOverlay, &PlayerControlsOverlay::qualityChanged, this, &PlayerWidget::onQualityChanged);
    connect(m_controlsOverlay, &PlayerControlsOverlay::playbackSpeedChanged, this,
            &PlayerWidget::onPlaybackSpeedChanged);
    connect(m_controlsOverlay, &PlayerControlsOverlay::statsForNerdsToggled, this,
            &PlayerWidget::toggleStatsForNerds);
    connect(m_controlsOverlay, &PlayerControlsOverlay::autoplayToggled, this,
            &PlayerWidget::onAutoplayToggled);
    connect(m_controlsOverlay, &PlayerControlsOverlay::theaterModeToggleRequested, this,
            &PlayerWidget::toggleTheaterMode);
    connect(m_controlsOverlay, &PlayerControlsOverlay::seekingChanged, this, [this](bool seeking) {
        m_userIsSeeking = seeking;
        if (!seeking) {
            m_lastSeekTime = std::chrono::steady_clock::now();
        }
    });
    connect(m_controlsOverlay, &PlayerControlsOverlay::controlsVisibilityChanged, this, [this](bool visible) {
        if (m_videoSurface) {
            m_videoSurface->setCursor(visible ? Qt::PointingHandCursor : Qt::BlankCursor);
        }
    });
    m_controlsOverlay->setVolume(AppSettings::instance().getVolume());
    m_controlsOverlay->setQuality(m_quality);
    m_controlsOverlay->setPlaybackSpeed(m_playbackSpeed);
    m_controlsOverlay->installEventFilter(this);

    m_statsWidget = new StatsForNerdsWidget(m_surfaceContainer);
    m_statsWidget->hide();
    m_statsWidget->move(16, 16);

    // 3. Watch Page Metadata Section (Title, Channel Row, Actions, Expandable Description)
    m_metadataContainer = new QWidget(scrollContent);
    m_metadataContainer->setObjectName("metadataContainer");
    m_metadataContainer->setStyleSheet("#metadataContainer { background: transparent; }");
    m_metadataContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *metaLayout = new QVBoxLayout(m_metadataContainer);
    metaLayout->setContentsMargins(16, 8, 16, 20);
    metaLayout->setSpacing(10);
    metaLayout->setAlignment(Qt::AlignTop);

    // 3a. Prominent Video Title
    m_titleLabel = new QLabel("Video Title", m_metadataContainer);
    m_titleLabel->setWordWrap(true);
    m_titleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_titleLabel->setStyleSheet(
        "font-size: 20px; font-weight: bold; color: #f1f1f1; margin: 0; padding: 0; background: transparent;");
    metaLayout->addWidget(m_titleLabel);

    // 3b. Channel & Action Bar Row
    auto *actionsRow = new QHBoxLayout();
    actionsRow->setSpacing(12);
    actionsRow->setContentsMargins(0, 0, 0, 0);

    // Channel Avatar Circle (40x40 to match modern YouTube watch page)
    m_channelAvatarLabel = new QLabel("?", m_metadataContainer);
    m_channelAvatarLabel->setFixedSize(40, 40);
    m_channelAvatarLabel->setAlignment(Qt::AlignCenter);
    m_channelAvatarLabel->setStyleSheet(
        "QLabel {"
        "  background-color: #272727; color: #ffffff; font-weight: bold; font-size: 16px;"
        "  border-radius: 20px; border: 1px solid rgba(255,255,255,0.1);"
        "}");
    actionsRow->addWidget(m_channelAvatarLabel);

    // Channel Info
    auto *chanInfoLayout = new QVBoxLayout();
    chanInfoLayout->setSpacing(2);
    chanInfoLayout->setContentsMargins(0, 0, 0, 0);
    m_authorLabel = new QLabel("Channel Name", m_metadataContainer);
    m_authorLabel->setStyleSheet(
        "font-size: 15px; font-weight: bold; color: #f1f1f1; margin: 0; padding: 0; background: transparent;");
    m_authorLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_subscriberCountLabel = new QLabel("YouTube Channel", m_metadataContainer);
    m_subscriberCountLabel->setStyleSheet(
        "font-size: 12px; color: #aaaaaa; margin: 0; padding: 0; background: transparent;");
    chanInfoLayout->addWidget(m_authorLabel);
    chanInfoLayout->addWidget(m_subscriberCountLabel);
    actionsRow->addLayout(chanInfoLayout);

    // Subscribe Button (YouTube high-contrast pill)
    m_subscribeBtn = new QPushButton("Subscribe", m_metadataContainer);
    m_subscribeBtn->setFocusPolicy(Qt::NoFocus);
    m_subscribeBtn->setCursor(Qt::PointingHandCursor);
    m_subscribeBtn->setFixedHeight(36);
    m_subscribeBtn->setStyleSheet(kSubscribePrimaryQss);
    connect(m_subscribeBtn, &QPushButton::clicked, this, &PlayerWidget::onSubscribeClicked);
    actionsRow->addWidget(m_subscribeBtn);

    actionsRow->addStretch();

    // Segmented Like / Dislike Pill Button
    auto *likeDislikeContainer = new QFrame(m_metadataContainer);
    likeDislikeContainer->setObjectName("likeDislikePill");
    likeDislikeContainer->setFixedHeight(36);
    likeDislikeContainer->setStyleSheet("#likeDislikePill {"
                                        "  background-color: #272727;"
                                        "  border-radius: 18px;"
                                        "  border: 1px solid rgba(255, 255, 255, 0.08);"
                                        "}");
    auto *pillLayout = new QHBoxLayout(likeDislikeContainer);
    pillLayout->setContentsMargins(0, 0, 0, 0);
    pillLayout->setSpacing(0);

    m_likeBtn = new QPushButton("Like", likeDislikeContainer);
    m_likeBtn->setFocusPolicy(Qt::NoFocus);
    m_likeBtn->setCursor(Qt::PointingHandCursor);
    m_likeBtn->setFixedHeight(36);
    m_likeBtn->setIcon(lightyt::ui::icons::thumbUp(16, QColor(0xf1, 0xf1, 0xf1)));
    m_likeBtn->setIconSize(QSize(16, 16));
    connect(m_likeBtn, &QPushButton::clicked, this, &PlayerWidget::onLikeClicked);
    pillLayout->addWidget(m_likeBtn);

    auto *divider = new QFrame(likeDislikeContainer);
    divider->setFrameShape(QFrame::VLine);
    divider->setStyleSheet("background-color: rgba(255, 255, 255, 0.2); max-width: 1px; margin: 6px 0;");
    pillLayout->addWidget(divider);

    m_dislikeBtn = new QPushButton(likeDislikeContainer);
    m_dislikeBtn->setFocusPolicy(Qt::NoFocus);
    m_dislikeBtn->setCursor(Qt::PointingHandCursor);
    m_dislikeBtn->setFixedHeight(36);
    m_dislikeBtn->setIcon(lightyt::ui::icons::thumbDown(16, QColor(0xf1, 0xf1, 0xf1)));
    m_dislikeBtn->setIconSize(QSize(16, 16));
    connect(m_dislikeBtn, &QPushButton::clicked, this, &PlayerWidget::onDislikeClicked);
    pillLayout->addWidget(m_dislikeBtn);

    updateLikeDislikeUi();
    actionsRow->addWidget(likeDislikeContainer);

    // Share Button
    m_shareBtn = new QPushButton("Share", m_metadataContainer);
    m_shareBtn->setFocusPolicy(Qt::NoFocus);
    m_shareBtn->setCursor(Qt::PointingHandCursor);
    m_shareBtn->setFixedHeight(36);
    m_shareBtn->setIcon(lightyt::ui::icons::shareArrow(16, QColor(0xf1, 0xf1, 0xf1)));
    m_shareBtn->setIconSize(QSize(16, 16));
    m_shareBtn->setStyleSheet(kPillBtnSecondaryQss);
    connect(m_shareBtn, &QPushButton::clicked, this, &PlayerWidget::onShareClicked);
    actionsRow->addWidget(m_shareBtn);

    metaLayout->addLayout(actionsRow);

    // 3c. Expandable Description Card
    m_descriptionCard = new QWidget(m_metadataContainer);
    m_descriptionCard->setObjectName("descriptionCard");
    m_descriptionCard->setAttribute(Qt::WA_StyledBackground, true);
    m_descriptionCard->setStyleSheet("#descriptionCard { background-color: #272727; border-radius: 12px; }");
    m_descriptionCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *descLayout = new QVBoxLayout(m_descriptionCard);
    descLayout->setContentsMargins(14, 12, 14, 12);
    descLayout->setSpacing(6);

    m_descriptionMetaLabel = new QLabel("Views and date", m_descriptionCard);
    m_descriptionMetaLabel->setStyleSheet(
        "color: #f1f1f1; font-weight: bold; font-size: 13px; margin: 0; padding: 0; background: transparent;");
    descLayout->addWidget(m_descriptionMetaLabel);

    m_descriptionTextLabel = new QLabel(m_descriptionCard);
    m_descriptionTextLabel->setWordWrap(true);
    m_descriptionTextLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_descriptionTextLabel->setStyleSheet(
        "color: #f1f1f1; font-size: 13px; margin-top: 2px; padding: 0; background: transparent;");
    descLayout->addWidget(m_descriptionTextLabel);

    m_toggleDescriptionBtn = new QPushButton("...more", m_descriptionCard);
    m_toggleDescriptionBtn->setFocusPolicy(Qt::NoFocus);
    m_toggleDescriptionBtn->setCursor(Qt::PointingHandCursor);
    m_toggleDescriptionBtn->setStyleSheet("QPushButton {"
                                          "  background: transparent; color: #f1f1f1; font-weight: bold; font-size: "
                                          "13px; border: none; text-align: left; padding: 2px 0; margin-top: 2px;"
                                          "}"
                                          "QPushButton:hover { color: #3ea6ff; }");
    connect(m_toggleDescriptionBtn, &QPushButton::clicked, this, &PlayerWidget::onToggleDescriptionClicked);
    descLayout->addWidget(m_toggleDescriptionBtn);

    metaLayout->addWidget(m_descriptionCard);
    metaLayout->addStretch(1);

    // 4. Compact Recommendations Feed Sidebar (Virtualised FeedView)
    m_recommendationsView = new FeedView(scrollContent);
    m_recommendationsView->setObjectName("recommendationsView");
    m_recommendationsView->setLayoutMode(FeedView::CardLayoutMode::CompactHorizontal);
    m_recommendationsView->setFixedWidth(402);
    m_recommendationsView->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    connect(m_recommendationsView, &FeedView::videoSelected, this, [this](const VideoItem &item) {
        playVideo(item, m_quality, m_cookieFilePath, m_allowAv1);
        if (m_client && !item.id.empty()) {
            m_client->recordWatchHistory(item.id);
        }
    });
    connect(m_recommendationsView, &FeedView::videoHovered, this, [this](const VideoItem &item) {
        if (!item.id.empty()) {
            StreamResolver::prefetch(item.id, m_quality.toStdString(), m_cookieFilePath, m_allowAv1);
        }
    });
    connect(m_recommendationsView, &FeedView::requestMoreVideos, this, [this]() {
        if (m_nextContinuationToken.empty() || !m_client)
            return;
        std::string token = m_nextContinuationToken;
        m_nextContinuationToken.clear();
        auto *client = m_client;
        QPointer<PlayerWidget> weakThis(this);
        uint64_t currentGen = m_generation.load();
        auto cancel = m_cancelToken;
        (void)QtConcurrent::run([weakThis, client, token, currentGen, cancel]() {
            std::string err;
            auto feedRes = client->fetchContinuation(token, err, ContinuationKind::WatchNext);
            if (!feedRes.videos.empty() && (!cancel || !cancel->load())) {
                QMetaObject::invokeMethod(
                    qApp,
                    [weakThis, feedRes, currentGen, cancel]() {
                        if (!weakThis || (cancel && cancel->load()) ||
                            currentGen != weakThis->m_generation.load())
                            return;
                        weakThis->m_nextContinuationToken = feedRes.continuationToken;
                        for (const auto &v : feedRes.videos) {
                            weakThis->m_relatedVideos.push_back(v);
                        }
                        if (weakThis->m_recommendationsView) {
                            weakThis->m_recommendationsView->appendVideos(feedRes.videos);
                        }
                    },
                    Qt::QueuedConnection);
            }
        });
    });

    m_isTheaterMode = AppSettings::instance().getTheaterMode();
    m_isAutoplay.store(AppSettings::instance().getAutoplay());
    if (m_controlsOverlay) {
        m_controlsOverlay->setTheaterMode(m_isTheaterMode);
        m_controlsOverlay->setAutoplay(m_isAutoplay.load());
    }

    m_scrollArea->setWidget(scrollContent);
    mainLayout->addWidget(m_scrollArea, 1);

    applyLayoutMode(m_isTheaterMode, false);
}

void PlayerWidget::setupEvents() {
    PlayerEvents events;
    QPointer<PlayerWidget> weakThis(this);

    events.onTime = [weakThis](double pos, double dur) {
        QMetaObject::invokeMethod(
            qApp,
            [weakThis, pos, dur]() {
                if (!weakThis)
                    return;
                if (dur > 0.0) {
                    weakThis->m_fallbackDuration = dur;
                }
                double effDur = dur > 0.0 ? dur : weakThis->effectiveDuration();
                if (effDur > 0.0) {
                    auto now = std::chrono::steady_clock::now();
                    bool inSeekCooldown = (now - weakThis->m_lastSeekTime) < std::chrono::milliseconds(600);
                    if (!weakThis->m_userIsSeeking && !inSeekCooldown && weakThis->m_controlsOverlay) {
                        weakThis->m_controlsOverlay->setDuration(effDur);
                        weakThis->m_controlsOverlay->setPosition(pos);
                    }

                    if (weakThis->m_client && !weakThis->m_currentItem.id.empty()) {
                        if (pos < weakThis->m_lastReportedWatchTime - 1.0) {
                            // User seeked backwards: reset tracking to new position
                            weakThis->m_lastReportedWatchTime = pos;
                        } else if (pos - weakThis->m_lastReportedWatchTime >= 60.0) {
                            // User seeked forward across a large gap: advance tracking to new position
                            weakThis->m_lastReportedWatchTime = pos;
                        } else if (pos - weakThis->m_lastReportedWatchTime >= 10.0 ||
                                   (pos >= effDur - 1.0 && weakThis->m_lastReportedWatchTime < effDur - 1.0)) {
                            double startSec = weakThis->m_lastReportedWatchTime;
                            weakThis->m_lastReportedWatchTime = pos;
                            weakThis->m_client->recordWatchTime(weakThis->m_currentItem.id, startSec, pos, effDur,
                                                                false);
                        }
                    }
                }
            },
            Qt::QueuedConnection);
    };

    events.onPause = [weakThis](bool paused) {
        QMetaObject::invokeMethod(
            qApp,
            [weakThis, paused]() {
                if (!weakThis)
                    return;
                if (weakThis->m_controlsOverlay) {
                    weakThis->m_controlsOverlay->setPlaying(!paused);
                }
                if (paused && weakThis->m_client && !weakThis->m_currentItem.id.empty() && weakThis->m_player) {
                    double curPos = weakThis->m_player->getPosition();
                    double curDur = weakThis->effectiveDuration();
                    if (curPos > weakThis->m_lastReportedWatchTime &&
                        curPos - weakThis->m_lastReportedWatchTime < 60.0) {
                        weakThis->m_client->recordWatchTime(weakThis->m_currentItem.id,
                                                            weakThis->m_lastReportedWatchTime, curPos, curDur, false);
                        weakThis->m_lastReportedWatchTime = curPos;
                    }
                }
            },
            Qt::QueuedConnection);
    };

    events.onCache = [weakThis](double cachedSeconds) {
        QMetaObject::invokeMethod(
            qApp,
            [weakThis, cachedSeconds]() {
                if (!weakThis || !weakThis->m_controlsOverlay || !weakThis->m_player)
                    return;
                double curPos = weakThis->m_player->getPosition();
                weakThis->m_controlsOverlay->setBufferedPosition(curPos + cachedSeconds);
            },
            Qt::QueuedConnection);
    };

    events.onState = [weakThis](PlaybackState state) {
        QMetaObject::invokeMethod(
            qApp,
            [weakThis, state]() {
                if (!weakThis)
                    return;
                switch (state) {
                case PlaybackState::Ended:
                    if (weakThis->m_controlsOverlay) {
                        weakThis->m_controlsOverlay->setPlaying(false);
                    }
                    if (weakThis->m_client && !weakThis->m_currentItem.id.empty() && weakThis->m_player) {
                        double curDur = weakThis->effectiveDuration();
                        weakThis->m_client->recordWatchTime(weakThis->m_currentItem.id,
                                                            weakThis->m_lastReportedWatchTime, curDur, curDur, true);
                        weakThis->m_lastReportedWatchTime = curDur;
                    }
                    if (weakThis->m_isAutoplay.load()) {
                        weakThis->playNextVideo();
                    }
                    break;
                default:
                    break;
                }
            },
            Qt::QueuedConnection);
    };

    events.onError = [weakThis](const std::string &message) {
        QMetaObject::invokeMethod(
            qApp,
            [weakThis, message]() {
                if (!weakThis)
                    return;
                weakThis->m_errorBanner->setText(QString::fromUtf8(message.c_str()));
                weakThis->m_errorBanner->show();
            },
            Qt::QueuedConnection);
    };

    m_player->setEvents(std::move(events));
}

void PlayerWidget::playVideo(const VideoItem &item, const QString &quality, const std::string &cookieFilePath,
                             bool allowAv1) {
    if (m_player) {
        m_player->stop();
    }

    if (m_cancelToken) {
        m_cancelToken->store(true);
    }
    if (m_workerThread.joinable()) {
        m_workerThread.detach();
    }
    if (m_metadataThread.joinable()) {
        m_metadataThread.detach();
    }
    if (m_avatarThread.joinable()) {
        m_avatarThread.detach();
    }

    // Create fresh cancellation token and increment generation before launching avatar or stream workers
    m_cancelToken = std::make_shared<std::atomic<bool>>(false);
    auto cancel = m_cancelToken;
    uint64_t currentGen = ++m_generation;

    m_currentItem = item;
    m_cookieFilePath = cookieFilePath;
    m_allowAv1 = allowAv1;
    m_lastReportedWatchTime = 0.0;
    m_fallbackDuration = parseDurationString(item.duration);

    m_autoplayVideoId.clear();
    m_relatedVideos.clear();
    m_nextContinuationToken.clear();
    if (m_recommendationsView) {
        m_recommendationsView->clearVideos();
    }

    m_playbackSpeed = 1.0;
    if (m_player) {
        m_player->setPlaybackSpeed(1.0);
    }

    if (!quality.isEmpty()) {
        m_quality = quality;
    } else if (m_quality.isEmpty()) {
        m_quality = "1080p";
    }

    if (m_controlsOverlay) {
        m_controlsOverlay->setPosition(0.0);
        m_controlsOverlay->setBufferedPosition(0.0);
        m_controlsOverlay->setDuration(m_fallbackDuration);
        m_controlsOverlay->setPlaying(true);
        m_controlsOverlay->setPlaybackSpeed(1.0);
        m_controlsOverlay->setQuality(m_quality);
        m_controlsOverlay->showControlsAnimated();
    }

    if (m_scrollArea && m_scrollArea->verticalScrollBar()) {
        m_scrollArea->verticalScrollBar()->setValue(0);
    }
    updatePlayerLayout();
    QTimer::singleShot(0, this, &PlayerWidget::updatePlayerLayout);

    m_titleLabel->setText(QString::fromUtf8(item.title.c_str()));
    m_authorLabel->setText(QString::fromUtf8(item.author.c_str()));

    QString initial = "?";
    if (!item.author.empty()) {
        initial = QString::fromUtf8(item.author.c_str()).left(1).toUpper();
    }
    if (m_channelAvatarLabel) {
        m_loadedAvatarUrl.clear();
        m_channelAvatarLabel->setPixmap(QPixmap());
        m_channelAvatarLabel->setText(initial);
        m_channelAvatarLabel->setFixedSize(40, 40);
        m_channelAvatarLabel->setStyleSheet(
            "QLabel {"
            "  background-color: #272727; color: #ffffff; font-weight: bold; font-size: 16px;"
            "  border-radius: 20px; border: 1px solid rgba(255,255,255,0.1);"
            "}");
        if (!item.channelAvatarUrl.empty()) {
            loadChannelAvatar(item.channelAvatarUrl);
        }
    }

    m_isLiked = false;
    m_isDisliked = false;
    m_initialLiked = false;
    m_initialDisliked = false;
    m_isSubscribed = false;
    m_baseLikes = 0;
    m_descriptionExpanded = false;
    m_fullDescription = item.description;

    QStringList metaParts;
    if (!item.viewCount.empty())
        metaParts << QString::fromUtf8(item.viewCount.c_str());
    if (!item.publishedTime.empty())
        metaParts << QString::fromUtf8(item.publishedTime.c_str());
    if (m_descriptionMetaLabel) {
        m_descriptionMetaLabel->setText(metaParts.isEmpty() ? "YouTube Video" : metaParts.join("  •  "));
    }

    if (m_subscribeBtn) {
        m_subscribeBtn->setText("Subscribe");
        m_subscribeBtn->setStyleSheet(kSubscribePrimaryQss);
    }

    updateLikeDislikeUi();
    updateDescriptionUi();

    if (!m_player->isInitialized()) {
        bool initialized = false;

        // 1. Prioritize in-process OpenGL render context (universal for Wayland, X11 & Windows)
        if (m_player->initialize(0)) {
            m_videoSurface->setPlayer(m_player.get());
            if (m_videoSurface->initRenderContext()) {
                initialized = true;
                LOG_INFO("[PlayerWidget] Video surface initialized via mpv_render_context (OpenGL FBO).");
            } else {
                m_player->stop();
                m_videoSurface->destroyRenderContext();
            }
        }

        // 2. Fallback to native window handle embedding (e.g. standalone mpv binary IPC)
        if (!initialized) {
            auto raw_wid = m_videoSurface->winId();
#ifdef _WIN32
            int64_t wid = static_cast<int64_t>(reinterpret_cast<uintptr_t>(raw_wid));
#else
            int64_t wid = static_cast<int64_t>(raw_wid);
#endif
            if (!m_player->initialize(wid)) {
                std::string err = m_player->getInitError();
                if (err.empty())
                    err = "Playback engine (mpv) not found.";
                m_errorBanner->setText(QString::fromUtf8(err.c_str()));
                m_errorBanner->show();
                return;
            }
            LOG_INFO("[PlayerWidget] Video surface initialized via native window handle fallback (wid: {}).", wid);
        }
        m_player->setVolume(AppSettings::instance().getVolume());
    }

    if (m_errorBanner) {
        m_errorBanner->hide();
    }

    std::string q = m_quality.toUtf8().constData();
    std::string videoId = item.id;
    std::string cookies = m_cookieFilePath;

    QPointer<PlayerWidget> weakThis(this);
    auto client = m_client;
    // 1. Immediately fetch watch metadata via InnerTube /next concurrently in background
    m_metadataThread = std::thread([weakThis, client, videoId, currentGen, cancel, q, cookies, allowAv1]() {
        WatchMetadata nextMeta;
        if (client) {
            nextMeta = client->fetchWatchMetadata(videoId, cancel.get());
        } else {
            nextMeta = YouTubeClient::fetchWatchMetadataDirect(videoId, cancel.get());
        }

        if (!cancel || !cancel->load()) {
            std::string nextId = !nextMeta.autoplayVideoId.empty()
                                     ? nextMeta.autoplayVideoId
                                     : (!nextMeta.relatedVideos.empty() ? nextMeta.relatedVideos.front().id : "");
            if (weakThis && weakThis->m_isAutoplay.load() && !nextId.empty() && nextId != videoId) {
                StreamResolver::prefetch(nextId, q, cookies, allowAv1);
            }

            QMetaObject::invokeMethod(
                qApp,
                [weakThis, nextMeta, currentGen, cancel]() {
                    if (!weakThis || (cancel && cancel->load()) ||
                        currentGen != weakThis->m_generation.load(std::memory_order_relaxed))
                        return;

                    weakThis->m_autoplayVideoId = nextMeta.autoplayVideoId;
                    weakThis->m_relatedVideos = nextMeta.relatedVideos;
                    weakThis->m_nextContinuationToken = nextMeta.continuationToken;

                    if (weakThis->m_recommendationsView) {
                        weakThis->m_recommendationsView->setVideos(nextMeta.relatedVideos);
                    }

                    if (!nextMeta.author.empty() && weakThis->m_authorLabel) {
                        weakThis->m_authorLabel->setText(QString::fromUtf8(nextMeta.author.c_str()));
                    }
                    if (!nextMeta.channelAvatarUrl.empty()) {
                        weakThis->m_currentItem.channelAvatarUrl = nextMeta.channelAvatarUrl;
                        weakThis->loadChannelAvatar(nextMeta.channelAvatarUrl);
                    }
                    if (!nextMeta.subscriberCount.empty() && weakThis->m_subscriberCountLabel) {
                        weakThis->m_subscriberCountLabel->setText(QString::fromUtf8(nextMeta.subscriberCount.c_str()));
                    }
                    if (!nextMeta.channelId.empty()) {
                        weakThis->m_currentItem.channelId = nextMeta.channelId;
                    }
                    if (nextMeta.likeCount > 0) {
                        weakThis->m_baseLikes = nextMeta.likeCount;
                    }
                    if (nextMeta.isLiked) {
                        weakThis->m_isLiked = true;
                        weakThis->m_initialLiked = true;
                        weakThis->m_isDisliked = false;
                        weakThis->m_initialDisliked = false;
                    } else if (nextMeta.isDisliked) {
                        weakThis->m_isDisliked = true;
                        weakThis->m_initialDisliked = true;
                        weakThis->m_isLiked = false;
                        weakThis->m_initialLiked = false;
                    }
                    weakThis->updateLikeDislikeUi();
                    if (nextMeta.isSubscribed && weakThis->m_subscribeBtn) {
                        weakThis->m_isSubscribed = true;
                        weakThis->m_subscribeBtn->setText("Subscribed");
                        weakThis->m_subscribeBtn->setStyleSheet(kPillBtnSecondaryQss);
                    }
                    if (!nextMeta.description.empty() && (weakThis->m_fullDescription.empty() ||
                                                          weakThis->m_fullDescription == "No description provided.")) {
                        weakThis->m_fullDescription = nextMeta.description;
                        weakThis->updateDescriptionUi();
                    }
                },
                Qt::QueuedConnection);
        }

        std::vector<std::string> qualities;
        if (client) {
            qualities = client->fetchAvailableQualities(videoId, cancel.get());
        } else {
            qualities = YouTubeClient::fetchAvailableQualitiesDirect(videoId, cancel.get());
        }
        if (!qualities.empty() && (!cancel || !cancel->load())) {
            QStringList qList;
            for (const auto &str : qualities) {
                qList.append(QString::fromUtf8(str.c_str()));
            }
            QMetaObject::invokeMethod(
                qApp,
                [weakThis, qList, currentGen, cancel]() {
                    if (!weakThis || (cancel && cancel->load()) ||
                        currentGen != weakThis->m_generation.load(std::memory_order_relaxed))
                        return;
                    if (weakThis->m_controlsOverlay) {
                        weakThis->m_controlsOverlay->setAvailableQualities(qList);
                    }
                },
                Qt::QueuedConnection);
        }
    });

    // 2. Resolve stream using yt-dlp concurrently (hits memory cache instantly without waiting for /next)
    m_workerThread = std::thread([weakThis, videoId, q, cookies, allowAv1, currentGen, cancel]() {
        StreamInfo info;
        std::string err;
        bool ok = StreamResolver::resolve(videoId, q, info, err, cookies, allowAv1, cancel.get());

        if (cancel && cancel->load())
            return;
        QMetaObject::invokeMethod(
            qApp,
            [weakThis, ok, info, err, currentGen, cancel]() {
                if (!weakThis || (cancel && cancel->load()) ||
                    currentGen != weakThis->m_generation.load(std::memory_order_relaxed)) {
                    // Resolution completed for a video the user already left
                    return;
                }

                if (!ok || info.videoUrl.empty()) {
                    weakThis->m_errorBanner->setText(
                        QString::fromUtf8(err.empty() ? "Failed to resolve stream." : err.c_str()));
                    weakThis->m_errorBanner->show();
                    return;
                }

                weakThis->m_currentVideoItag = info.videoItag;
                weakThis->m_currentAudioItag = info.audioItag;
                weakThis->updateStatsContext();
                weakThis->m_player->setVolume(AppSettings::instance().getVolume());
                weakThis->m_player->playDirectStream(info.videoUrl, info.audioUrl);
            },
            Qt::QueuedConnection);
    });
}

void PlayerWidget::setQuality(const QString &quality) {
    if (m_quality != quality) {
        m_quality = quality;
        onQualityChanged(quality);
    }
}

void PlayerWidget::setPlaybackSpeed(double speed) {
    onPlaybackSpeedChanged(speed);
}

void PlayerWidget::stopPlayback() {
    m_generation.fetch_add(1, std::memory_order_relaxed);
    if (m_cancelToken) {
        m_cancelToken->store(true);
    }
    if (m_client && !m_currentItem.id.empty() && m_player && m_player->getPosition() > 1.0) {
        double curPos = m_player->getPosition();
        double curDur = m_player->getDuration();
        if (curPos > m_lastReportedWatchTime && curPos - m_lastReportedWatchTime < 60.0) {
            m_client->recordWatchTime(m_currentItem.id, m_lastReportedWatchTime, curPos, curDur, false);
            m_lastReportedWatchTime = curPos;
        }
    }
    if (m_player) {
        m_player->stop();
    }
    if (m_controlsOverlay) {
        m_controlsOverlay->setPlaying(false);
        m_controlsOverlay->setPosition(0.0);
        m_controlsOverlay->setBufferedPosition(0.0);
        m_controlsOverlay->showControlsAnimated();
    }
    if (m_cancelToken) {
        m_cancelToken->store(true);
    }
    if (!m_destroying.load()) {
        if (m_workerThread.joinable()) {
            m_workerThread.detach();
        }
        if (m_metadataThread.joinable()) {
            m_metadataThread.detach();
        }
        if (m_avatarThread.joinable()) {
            m_avatarThread.detach();
        }
    }
}

void PlayerWidget::onPlayPauseClicked() {
    if (!m_player)
        return;
    m_player->togglePause();
    if (m_controlsOverlay) {
        m_controlsOverlay->showControlsAnimated();
    }
}

void PlayerWidget::onSeekRequested(double seconds) {
    if (!m_player)
        return;
    double duration = effectiveDuration();
    double targetSeconds = std::clamp(seconds, 0.0, duration > 0.0 ? duration : seconds);
    m_lastSeekTime = std::chrono::steady_clock::now();
    m_player->seekAbsolute(targetSeconds);
    if (m_controlsOverlay) {
        m_controlsOverlay->setPosition(targetSeconds);
        m_controlsOverlay->showControlsAnimated();
    }
}

double PlayerWidget::effectiveDuration() const noexcept {
    if (m_player) {
        double dur = m_player->getDuration();
        if (dur > 0.0) {
            return dur;
        }
    }
    return m_fallbackDuration;
}

double PlayerWidget::parseDurationString(std::string_view durStr) noexcept {
    if (durStr.empty()) {
        return 0.0;
    }
    double total = 0.0;
    size_t pos = 0;
    while (pos < durStr.size()) {
        size_t next = durStr.find(':', pos);
        std::string_view part = (next == std::string_view::npos) ? durStr.substr(pos) : durStr.substr(pos, next - pos);
        while (!part.empty() && (part.front() == ' ' || part.front() == '\t'))
            part.remove_prefix(1);
        while (!part.empty() && (part.back() == ' ' || part.back() == '\t'))
            part.remove_suffix(1);
        if (part.empty()) {
            return 0.0;
        }
        int val = 0;
        auto [ptr, ec] = std::from_chars(part.data(), part.data() + part.size(), val);
        if (ec != std::errc{} || ptr != part.data() + part.size() || val < 0) {
            return 0.0;
        }
        total = total * 60.0 + static_cast<double>(val);
        if (next == std::string_view::npos) {
            break;
        }
        pos = next + 1;
    }
    return total;
}

void PlayerWidget::setVolume(int value) {
    if (value > 0) {
        m_preMuteVolume = value;
        AppSettings::instance().setVolume(value);
    }
    if (m_controlsOverlay) {
        m_controlsOverlay->setVolume(value);
    }
    if (m_player) {
        m_player->setVolume(value);
    }
}

int PlayerWidget::getVolume() const {
    return AppSettings::instance().getVolume();
}

void PlayerWidget::onVolumeChanged(int value) {
    if (m_player) {
        m_player->setVolume(value);
    }
    if (m_controlsOverlay) {
        m_controlsOverlay->setVolume(value);
        m_controlsOverlay->showControlsAnimated();
    }
    if (value > 0) {
        m_preMuteVolume = value;
        AppSettings::instance().setVolume(value);
    }
}

void PlayerWidget::onQualityChanged(const QString &quality) {
    m_quality = quality;
    AppSettings::instance().setQuality(quality);
    if (m_controlsOverlay) {
        m_controlsOverlay->setQuality(quality);
    }
    if (m_player && !m_currentItem.id.empty()) {
        auto state = m_player->getState();
        if (state == PlaybackState::Idle || state == PlaybackState::Ended) {
            return;
        }
        double currentPos = m_player->getPosition();
        if (m_cancelToken) {
            m_cancelToken->store(true);
        }
        if (m_workerThread.joinable()) {
            m_workerThread.detach();
        }

        m_cancelToken = std::make_shared<std::atomic<bool>>(false);
        auto cancel = m_cancelToken;
        uint64_t currentGen = ++m_generation;

        std::string qStr = quality.toUtf8().constData();
        std::string videoId = m_currentItem.id;
        std::string cookies = m_cookieFilePath;
        bool allowAv1 = m_allowAv1;

        QPointer<PlayerWidget> weakThis(this);
        m_workerThread = std::thread([weakThis, videoId, qStr, cookies, allowAv1, currentPos, currentGen, cancel]() {
            StreamInfo info;
            std::string err;
            bool ok = StreamResolver::resolve(videoId, qStr, info, err, cookies, allowAv1, cancel.get());

            if (cancel && cancel->load())
                return;
            QMetaObject::invokeMethod(
                qApp,
                [weakThis, ok, info, err, currentGen, currentPos, cancel]() {
                    if (!weakThis || (cancel && cancel->load()) ||
                        currentGen != weakThis->m_generation.load(std::memory_order_relaxed))
                        return;
                    if (!ok || info.videoUrl.empty()) {
                        LOG_WARN("[PlayerWidget] Failed to switch quality: {}", err);
                        weakThis->m_errorBanner->setText(
                            QString::fromUtf8(err.empty() ? "Failed to switch quality." : err.c_str()));
                        weakThis->m_errorBanner->show();
                        return;
                    }
                    weakThis->m_currentVideoItag = info.videoItag;
                    weakThis->m_currentAudioItag = info.audioItag;
                    weakThis->updateStatsContext();
                    weakThis->m_player->playDirectStream(info.videoUrl, info.audioUrl, currentPos);
                },
                Qt::QueuedConnection);
        });
    }
}

void PlayerWidget::onPlaybackSpeedChanged(double speed) {
    double clamped = std::clamp(speed, 1.0, 2.0);
    m_playbackSpeed = clamped;
    if (m_player) {
        m_player->setPlaybackSpeed(clamped);
    }
    if (m_controlsOverlay) {
        m_controlsOverlay->setPlaybackSpeed(clamped);
    }
}

bool PlayerWidget::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_surfaceContainer || watched == m_videoSurface || watched == m_controlsOverlay) {
        if (event->type() == QEvent::MouseMove) {
            if (m_controlsOverlay) {
                m_controlsOverlay->showControlsAnimated();
            }
            if (m_videoSurface) {
                m_videoSurface->setCursor(Qt::PointingHandCursor);
            }
        } else if (event->type() == QEvent::Enter) {
            if (m_controlsOverlay) {
                m_controlsOverlay->showControlsAnimated();
            }
            if (m_videoSurface) {
                m_videoSurface->setCursor(Qt::PointingHandCursor);
            }
        } else if (event->type() == QEvent::Leave) {
            if (m_surfaceContainer && m_controlsOverlay) {
                QPoint globalPos = QCursor::pos();
                QPoint localPos = m_surfaceContainer->mapFromGlobal(globalPos);
                if (!m_surfaceContainer->rect().contains(localPos)) {
                    m_controlsOverlay->scheduleHide(150);
                }
            }
        } else if (event->type() == QEvent::ContextMenu) {
            auto *ctxEvent = static_cast<QContextMenuEvent *>(event);
            showPlayerContextMenu(ctxEvent->globalPos());
            return true;
        } else if (event->type() == QEvent::Resize) {
            if (watched == m_surfaceContainer) {
                if (m_controlsOverlay) {
                    constexpr int kOverlayHeight = 82;
                    m_controlsOverlay->setGeometry(0, m_surfaceContainer->height() - kOverlayHeight,
                                                   m_surfaceContainer->width(), kOverlayHeight);
                    m_controlsOverlay->raise();
                }
                if (m_statsWidget) {
                    if (m_videoSurface) {
                        m_statsWidget->setViewportDimensions(m_videoSurface->width(), m_videoSurface->height(),
                                                             devicePixelRatio());
                    }
                    m_statsWidget->clampToParent();
                    if (m_statsWidget->isVisible()) {
                        m_statsWidget->raise();
                    }
                }
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void PlayerWidget::keyPressEvent(QKeyEvent *event) {
    if (!m_player) {
        QWidget::keyPressEvent(event);
        return;
    }

    auto applyRelativeSeek = [this](double deltaSeconds) {
        double dur = effectiveDuration();
        if (dur > 0.0) {
            double curPos = m_player->getPosition();
            double target = std::clamp(curPos + deltaSeconds, 0.0, dur);
            m_lastSeekTime = std::chrono::steady_clock::now();
            if (m_controlsOverlay) {
                m_controlsOverlay->setPosition(target);
                m_controlsOverlay->showControlsAnimated();
            }
        }
        m_player->seekRelative(deltaSeconds);
    };

    switch (event->key()) {
    case Qt::Key_Space:
    case Qt::Key_K:
        onPlayPauseClicked();
        event->accept();
        break;
    case Qt::Key_Left:
        applyRelativeSeek(-5.0);
        event->accept();
        break;
    case Qt::Key_Right:
        applyRelativeSeek(5.0);
        event->accept();
        break;
    case Qt::Key_J:
        applyRelativeSeek(-10.0);
        event->accept();
        break;
    case Qt::Key_L:
        applyRelativeSeek(10.0);
        event->accept();
        break;
    case Qt::Key_Up:
        onVolumeChanged(std::min(100, getVolume() + 5));
        event->accept();
        break;
    case Qt::Key_Down:
        onVolumeChanged(std::max(0, getVolume() - 5));
        event->accept();
        break;
    case Qt::Key_M: {
        int cur = getVolume();
        int target = (cur > 0) ? 0 : (m_preMuteVolume > 0 ? m_preMuteVolume : 100);
        onVolumeChanged(target);
        event->accept();
        break;
    }
    case Qt::Key_F:
        emit fullscreenToggleRequested();
        event->accept();
        break;
    case Qt::Key_S:
        toggleStatsForNerds();
        event->accept();
        break;
    case Qt::Key_T:
        toggleTheaterMode();
        event->accept();
        break;
    case Qt::Key_N:
        if (event->modifiers() & Qt::ShiftModifier) {
            playNextVideo();
            event->accept();
            break;
        }
        QWidget::keyPressEvent(event);
        break;
    case Qt::Key_Greater:
        onPlaybackSpeedChanged(std::min(2.0, m_playbackSpeed + 0.25));
        event->accept();
        break;
    case Qt::Key_Less:
        onPlaybackSpeedChanged(std::max(1.0, m_playbackSpeed - 0.25));
        event->accept();
        break;
    case Qt::Key_Escape:
        if (m_isFullscreen) {
            emit fullscreenToggleRequested();
        } else {
            emit backRequested();
        }
        event->accept();
        break;
    default:
        QWidget::keyPressEvent(event);
        break;
    }
}

void PlayerWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    updatePlayerLayout();
}

void PlayerWidget::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    updatePlayerLayout();
}

void PlayerWidget::updatePlayerLayout() {
    if (!m_surfaceContainer)
        return;

    if (m_isFullscreen) {
        m_surfaceContainer->setFixedHeight(height());
    } else {
        int scrollBarW =
            (m_scrollArea && m_scrollArea->verticalScrollBar() && m_scrollArea->verticalScrollBar()->isVisible())
                ? m_scrollArea->verticalScrollBar()->width()
                : 8;
        int maxPlayerH = std::max(360, height() - 120);

        if (m_isTheaterMode) {
            int availW = std::max(480, width() - scrollBarW);
            int ideal16x9 = (availW * 9) / 16;
            int targetH = std::clamp(ideal16x9, 360, maxPlayerH);
            m_surfaceContainer->setFixedHeight(targetH);
        } else {
            int availCol0 = std::max(360, width() - scrollBarW - 402 - 16);
            int ideal16x9 = (availCol0 * 9) / 16;
            int targetH = std::clamp(ideal16x9, 360, maxPlayerH);
            m_surfaceContainer->setFixedHeight(targetH);
        }
    }

    if (m_controlsOverlay) {
        constexpr int kOverlayHeight = 82;
        int containerW = m_surfaceContainer->width();
        int containerH = m_surfaceContainer->height();
        m_controlsOverlay->setGeometry(0, containerH - kOverlayHeight, containerW, kOverlayHeight);
        m_controlsOverlay->raise();
    }
}

QString PlayerWidget::formatTime(double seconds) {
    if (seconds < 0.0)
        seconds = 0.0;
    int totalSec = static_cast<int>(seconds);
    int sec = totalSec % 60;
    int min = (totalSec / 60) % 60;
    int hr = totalSec / 3600;

    if (hr > 0) {
        return QString::asprintf("%02d:%02d:%02d", hr, min, sec);
    }
    return QString::asprintf("%02d:%02d", min, sec);
}

void PlayerWidget::setFullscreen(bool fullscreen) {
    applyLayoutMode(m_isTheaterMode, fullscreen);
}

void PlayerWidget::setTheaterMode(bool theater) {
    if (m_isTheaterMode != theater) {
        applyLayoutMode(theater, m_isFullscreen);
    }
}

void PlayerWidget::toggleTheaterMode() {
    setTheaterMode(!m_isTheaterMode);
}

void PlayerWidget::setAutoplay(bool autoplay) {
    onAutoplayToggled(autoplay);
}

void PlayerWidget::onAutoplayToggled(bool enabled) {
    m_isAutoplay.store(enabled);
    AppSettings::instance().setAutoplay(enabled);
    if (m_controlsOverlay) {
        m_controlsOverlay->setAutoplay(enabled);
    }
    if (enabled) {
        std::string candidateId = !m_autoplayVideoId.empty()
                                      ? m_autoplayVideoId
                                      : (!m_relatedVideos.empty() ? m_relatedVideos.front().id : "");
        if (!candidateId.empty() && candidateId != m_currentItem.id) {
            StreamResolver::prefetch(candidateId, m_quality.toStdString(), m_cookieFilePath, m_allowAv1);
        }
    }
}

void PlayerWidget::playNextVideo() {
    std::string candidateId = !m_autoplayVideoId.empty()
                                  ? m_autoplayVideoId
                                  : (!m_relatedVideos.empty() ? m_relatedVideos.front().id : "");

    // Guard against self-loop: if candidate ID is empty or equals m_currentItem.id, pick first non-matching related item
    if (candidateId.empty() || candidateId == m_currentItem.id) {
        candidateId.clear();
        for (const auto &item : m_relatedVideos) {
            if (!item.id.empty() && item.id != m_currentItem.id) {
                candidateId = item.id;
                break;
            }
        }
    }

    if (candidateId.empty() || candidateId == m_currentItem.id) {
        LOG_INFO("[PlayerWidget] playNextVideo: No valid next video available to play.");
        return;
    }

    LOG_INFO("[PlayerWidget] Playing next video: {}", candidateId);

    // Locate candidate VideoItem from m_relatedVideos or construct with ID
    VideoItem nextItem;
    bool found = false;
    for (const auto &rel : m_relatedVideos) {
        if (rel.id == candidateId) {
            nextItem = rel;
            found = true;
            break;
        }
    }
    if (!found) {
        nextItem.id = candidateId;
        nextItem.title = "Next Video";
    }

    playVideo(nextItem, m_quality, m_cookieFilePath, m_allowAv1);
    if (m_client) {
        m_client->recordWatchHistory(candidateId);
    }
}

void PlayerWidget::applyLayoutMode(bool isTheater, bool isFullscreen) {
    m_isTheaterMode = isTheater;
    m_isFullscreen = isFullscreen;

    if (!m_watchGridLayout || !m_surfaceContainer || !m_metadataContainer || !m_recommendationsView) {
        return;
    }

    // NEVER reparent or destroy m_surfaceContainer or MpvGlWidget;
    // reconfigure grid coordinates exclusively using removeWidget and addWidget on m_watchGridLayout.
    m_watchGridLayout->removeWidget(m_surfaceContainer);
    m_watchGridLayout->removeWidget(m_metadataContainer);
    m_watchGridLayout->removeWidget(m_recommendationsView);

    if (m_isFullscreen) {
        m_metadataContainer->hide();
        m_recommendationsView->hide();
        if (m_scrollArea) {
            if (m_scrollArea->verticalScrollBar()) {
                m_scrollArea->verticalScrollBar()->setValue(0);
            }
            m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        }
        m_watchGridLayout->setContentsMargins(0, 0, 0, 0);
        m_watchGridLayout->setHorizontalSpacing(0);
        m_watchGridLayout->setVerticalSpacing(0);

        m_watchGridLayout->addWidget(m_surfaceContainer, 0, 0, 1, 2);
        m_watchGridLayout->setColumnStretch(0, 1);
        m_watchGridLayout->setColumnStretch(1, 0);
        m_watchGridLayout->setRowStretch(0, 1);
        m_watchGridLayout->setRowStretch(1, 0);
        m_watchGridLayout->setRowStretch(2, 0);

        if (m_surfaceContainer) {
            m_surfaceContainer->setStyleSheet("#surfaceContainer { background-color: #000000; border-radius: 0px; }");
        }
        if (m_controlsOverlay) {
            m_controlsOverlay->setFullscreen(true);
        }
    } else if (m_isTheaterMode) {
        m_metadataContainer->show();
        m_recommendationsView->show();
        if (m_scrollArea) {
            m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        }
        m_watchGridLayout->setContentsMargins(0, 0, 0, 24);
        m_watchGridLayout->setHorizontalSpacing(16);
        m_watchGridLayout->setVerticalSpacing(8);

        // Feature 15: Theater Mode top-banner layout:
        // Video spans columns 0..1 full-width across top; metadata in row 1 col 0; recommendations in row 1 col 1.
        m_watchGridLayout->addWidget(m_surfaceContainer, 0, 0, 1, 2, Qt::AlignTop);
        m_watchGridLayout->addWidget(m_metadataContainer, 1, 0, 1, 1, Qt::AlignTop);
        m_watchGridLayout->addWidget(m_recommendationsView, 1, 1, 1, 1, Qt::AlignTop);
        m_watchGridLayout->setColumnStretch(0, 1);
        m_watchGridLayout->setColumnStretch(1, 0);
        m_watchGridLayout->setRowStretch(0, 0);
        m_watchGridLayout->setRowStretch(1, 1);
        m_watchGridLayout->setRowStretch(2, 0);

        if (m_surfaceContainer) {
            m_surfaceContainer->setStyleSheet("#surfaceContainer { background-color: #000000; border-radius: 0px; }");
        }
        if (m_controlsOverlay) {
            m_controlsOverlay->setFullscreen(false);
            m_controlsOverlay->setTheaterMode(true);
        }
    } else {
        m_metadataContainer->show();
        m_recommendationsView->show();
        if (m_scrollArea) {
            m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        }
        m_watchGridLayout->setContentsMargins(0, 0, 0, 24);
        m_watchGridLayout->setHorizontalSpacing(16);
        m_watchGridLayout->setVerticalSpacing(8);

        // Feature 14: Normal View 2-column layout:
        // Video 16:9 and metadata in column 0; compact recommendations FeedView in column 1 spanning rows 0..1.
        m_watchGridLayout->addWidget(m_surfaceContainer, 0, 0, 1, 1, Qt::AlignTop);
        m_watchGridLayout->addWidget(m_metadataContainer, 1, 0, 1, 1, Qt::AlignTop);
        m_watchGridLayout->addWidget(m_recommendationsView, 0, 1, 2, 1, Qt::AlignTop);
        m_watchGridLayout->setColumnStretch(0, 1);
        m_watchGridLayout->setColumnStretch(1, 0);
        m_watchGridLayout->setRowStretch(0, 0);
        m_watchGridLayout->setRowStretch(1, 1);
        m_watchGridLayout->setRowStretch(2, 0);

        if (m_surfaceContainer) {
            m_surfaceContainer->setStyleSheet("#surfaceContainer { background-color: #000000; border-radius: 0px; }");
        }
        if (m_controlsOverlay) {
            m_controlsOverlay->setFullscreen(false);
            m_controlsOverlay->setTheaterMode(false);
        }
    }

    AppSettings::instance().setTheaterMode(m_isTheaterMode);
    if (layout()) {
        layout()->activate();
    }
    updatePlayerLayout();
    if (m_videoSurface) {
        m_videoSurface->update();
    }
    emit theaterModeToggled(m_isTheaterMode);
}

void PlayerWidget::onLikeClicked() {
    bool sendLike = false;
    bool sendRemove = false;
    if (m_isLiked) {
        m_isLiked = false;
        sendRemove = true;
    } else {
        m_isLiked = true;
        m_isDisliked = false;
        sendLike = true;
    }
    updateLikeDislikeUi();

    if (m_client && !m_currentItem.id.empty()) {
        std::string videoId = m_currentItem.id;
        auto *client = m_client;
        const std::atomic<bool> *cancelPtr = &m_destroying;
        (void)QtConcurrent::run([client, videoId, sendLike, sendRemove, cancelPtr]() {
            std::string err;
            if (sendRemove) {
                (void)client->removeLike(videoId, err, cancelPtr);
            } else if (sendLike) {
                (void)client->likeVideo(videoId, true, err, cancelPtr);
            }
        });
    }
}

void PlayerWidget::onDislikeClicked() {
    bool sendDislike = false;
    bool sendRemove = false;
    if (m_isDisliked) {
        m_isDisliked = false;
        sendRemove = true;
    } else {
        m_isDisliked = true;
        m_isLiked = false;
        sendDislike = true;
    }
    updateLikeDislikeUi();

    if (m_client && !m_currentItem.id.empty()) {
        std::string videoId = m_currentItem.id;
        auto *client = m_client;
        const std::atomic<bool> *cancelPtr = &m_destroying;
        (void)QtConcurrent::run([client, videoId, sendDislike, sendRemove, cancelPtr]() {
            std::string err;
            if (sendRemove) {
                (void)client->removeLike(videoId, err, cancelPtr);
            } else if (sendDislike) {
                (void)client->likeVideo(videoId, false, err, cancelPtr);
            }
        });
    }
}

void PlayerWidget::onShareClicked() {
    if (m_currentItem.id.empty())
        return;
    QString url = QString::fromStdString(m_currentItem.getWatchUrl());
    QGuiApplication::clipboard()->setText(url);
    m_shareBtn->setIcon(lightyt::ui::icons::checkMark(16, QColor(0xf1, 0xf1, 0xf1)));
    m_shareBtn->setText("Copied!");
    QPointer<PlayerWidget> weakThis(this);
    QTimer::singleShot(2000, this, [weakThis]() {
        if (weakThis && weakThis->m_shareBtn) {
            weakThis->m_shareBtn->setIcon(lightyt::ui::icons::shareArrow(16, QColor(0xf1, 0xf1, 0xf1)));
            weakThis->m_shareBtn->setText("Share");
        }
    });
}

void PlayerWidget::onSubscribeClicked() {
    m_isSubscribed = !m_isSubscribed;
    if (m_isSubscribed) {
        m_subscribeBtn->setText("Subscribed");
        m_subscribeBtn->setStyleSheet(kPillBtnSecondaryQss);
    } else {
        m_subscribeBtn->setText("Subscribe");
        m_subscribeBtn->setStyleSheet(kSubscribePrimaryQss);
    }

    if (m_client && !m_currentItem.channelId.empty()) {
        std::string channelId = m_currentItem.channelId;
        bool sub = m_isSubscribed;
        auto *client = m_client;
        const std::atomic<bool> *cancelPtr = &m_destroying;
        (void)QtConcurrent::run([client, channelId, sub, cancelPtr]() {
            std::string err;
            (void)client->subscribeChannel(channelId, sub, err, cancelPtr);
        });
    }
}

void PlayerWidget::onToggleDescriptionClicked() {
    m_descriptionExpanded = !m_descriptionExpanded;
    updateDescriptionUi();
}

void PlayerWidget::updateLikeDislikeUi() {
    if (!m_likeBtn || !m_dislikeBtn)
        return;
    int delta = (m_isLiked ? 1 : 0) - (m_initialLiked ? 1 : 0);
    int64_t displayLikes = std::max<int64_t>(0, m_baseLikes + delta);
    QString likeText = displayLikes > 0 ? formatCount(displayLikes) : "Like";
    m_likeBtn->setText(likeText);

    if (m_isLiked) {
        m_likeBtn->setIcon(lightyt::ui::icons::thumbUp(16, QColor(0x0f, 0x0f, 0x0f), true));
        m_likeBtn->setStyleSheet("QPushButton {"
                                 "  background-color: #f1f1f1; color: #0f0f0f; font-weight: 600; font-size: 13px;"
                                 "  border: 1px solid transparent;"
                                 "  border-top-left-radius: 18px; border-bottom-left-radius: 18px;"
                                 "  border-top-right-radius: 0px; border-bottom-right-radius: 0px;"
                                 "  min-height: 34px; max-height: 34px; padding: 0px 14px; outline: none;"
                                 "}"
                                 "QPushButton:hover {"
                                 "  background-color: #d9d9d9;"
                                 "  border: 1px solid transparent;"
                                 "  border-top-left-radius: 18px; border-bottom-left-radius: 18px;"
                                 "  border-top-right-radius: 0px; border-bottom-right-radius: 0px;"
                                 "  outline: none;"
                                 "}"
                                 "QPushButton:pressed, QPushButton:hover:pressed {"
                                 "  background-color: #cccccc;"
                                 "  border: 1px solid transparent;"
                                 "  border-top-left-radius: 18px; border-bottom-left-radius: 18px;"
                                 "  border-top-right-radius: 0px; border-bottom-right-radius: 0px;"
                                 "  outline: none;"
                                 "}"
                                 "QPushButton:focus { outline: none; border: 1px solid transparent; }");
    } else {
        m_likeBtn->setIcon(lightyt::ui::icons::thumbUp(16, QColor(0xf1, 0xf1, 0xf1), false));
        m_likeBtn->setStyleSheet("QPushButton {"
                                 "  background-color: transparent; color: #f1f1f1; font-weight: 600; font-size: 13px;"
                                 "  border: 1px solid transparent;"
                                 "  border-top-left-radius: 18px; border-bottom-left-radius: 18px;"
                                 "  border-top-right-radius: 0px; border-bottom-right-radius: 0px;"
                                 "  min-height: 34px; max-height: 34px; padding: 0px 14px; outline: none;"
                                 "}"
                                 "QPushButton:hover {"
                                 "  background-color: #3f3f3f;"
                                 "  border: 1px solid transparent;"
                                 "  border-top-left-radius: 18px; border-bottom-left-radius: 18px;"
                                 "  border-top-right-radius: 0px; border-bottom-right-radius: 0px;"
                                 "  outline: none;"
                                 "}"
                                 "QPushButton:pressed, QPushButton:hover:pressed {"
                                 "  background-color: #525252;"
                                 "  border: 1px solid transparent;"
                                 "  border-top-left-radius: 18px; border-bottom-left-radius: 18px;"
                                 "  border-top-right-radius: 0px; border-bottom-right-radius: 0px;"
                                 "  outline: none;"
                                 "}"
                                 "QPushButton:focus { outline: none; border: 1px solid transparent; }");
    }

    if (m_isDisliked) {
        m_dislikeBtn->setIcon(lightyt::ui::icons::thumbDown(16, QColor(0x0f, 0x0f, 0x0f), true));
        m_dislikeBtn->setStyleSheet("QPushButton {"
                                    "  background-color: #f1f1f1; color: #0f0f0f; font-weight: 600; font-size: 13px;"
                                    "  border: 1px solid transparent;"
                                    "  border-top-left-radius: 0px; border-bottom-left-radius: 0px;"
                                    "  border-top-right-radius: 18px; border-bottom-right-radius: 18px;"
                                    "  min-height: 34px; max-height: 34px; padding: 0px 14px; outline: none;"
                                    "}"
                                    "QPushButton:hover {"
                                    "  background-color: #d9d9d9;"
                                    "  border: 1px solid transparent;"
                                    "  border-top-left-radius: 0px; border-bottom-left-radius: 0px;"
                                    "  border-top-right-radius: 18px; border-bottom-right-radius: 18px;"
                                    "  outline: none;"
                                    "}"
                                    "QPushButton:pressed, QPushButton:hover:pressed {"
                                    "  background-color: #cccccc;"
                                    "  border: 1px solid transparent;"
                                    "  border-top-left-radius: 0px; border-bottom-left-radius: 0px;"
                                    "  border-top-right-radius: 18px; border-bottom-right-radius: 18px;"
                                    "  outline: none;"
                                    "}"
                                    "QPushButton:focus { outline: none; border: 1px solid transparent; }");
    } else {
        m_dislikeBtn->setIcon(lightyt::ui::icons::thumbDown(16, QColor(0xf1, 0xf1, 0xf1), false));
        m_dislikeBtn->setStyleSheet(
            "QPushButton {"
            "  background-color: transparent; color: #f1f1f1; font-weight: 600; font-size: 13px;"
            "  border: 1px solid transparent;"
            "  border-top-left-radius: 0px; border-bottom-left-radius: 0px;"
            "  border-top-right-radius: 18px; border-bottom-right-radius: 18px;"
            "  min-height: 34px; max-height: 34px; padding: 0px 14px; outline: none;"
            "}"
            "QPushButton:hover {"
            "  background-color: #3f3f3f;"
            "  border: 1px solid transparent;"
            "  border-top-left-radius: 0px; border-bottom-left-radius: 0px;"
            "  border-top-right-radius: 18px; border-bottom-right-radius: 18px;"
            "  outline: none;"
            "}"
            "QPushButton:pressed, QPushButton:hover:pressed {"
            "  background-color: #525252;"
            "  border: 1px solid transparent;"
            "  border-top-left-radius: 0px; border-bottom-left-radius: 0px;"
            "  border-top-right-radius: 18px; border-bottom-right-radius: 18px;"
            "  outline: none;"
            "}"
            "QPushButton:focus { outline: none; border: 1px solid transparent; }");
    }
}

void PlayerWidget::updateDescriptionUi() {
    if (!m_descriptionTextLabel || !m_toggleDescriptionBtn)
        return;
    if (m_fullDescription.empty()) {
        m_descriptionTextLabel->setText("No description provided.");
        m_toggleDescriptionBtn->hide();
        return;
    }

    QString full = QString::fromUtf8(m_fullDescription.c_str());
    if (m_descriptionExpanded) {
        m_descriptionTextLabel->setText(full);
        m_toggleDescriptionBtn->setText("Show less");
        m_toggleDescriptionBtn->show();
    } else {
        QStringList lines = full.split('\n');
        QString preview;
        if (lines.size() > 3) {
            preview = lines.mid(0, 3).join('\n').trimmed();
        } else if (full.length() > 220) {
            preview = full.left(220).trimmed();
        } else {
            preview = full;
        }

        m_descriptionTextLabel->setText(preview);
        if (full.length() > preview.length() || lines.size() > 3) {
            m_toggleDescriptionBtn->setText("...more");
            m_toggleDescriptionBtn->show();
        } else {
            m_toggleDescriptionBtn->hide();
        }
    }
}

QString PlayerWidget::formatCount(int64_t count) {
    if (count >= 1000000000) {
        double val = count / 1000000000.0;
        QString s = QString::asprintf("%.1fB", val);
        if (s.endsWith(".0B"))
            s.replace(".0B", "B");
        return s;
    }
    if (count >= 1000000) {
        double val = count / 1000000.0;
        QString s = QString::asprintf("%.1fM", val);
        if (s.endsWith(".0M"))
            s.replace(".0M", "M");
        return s;
    }
    if (count >= 1000) {
        double val = count / 1000.0;
        QString s = QString::asprintf("%.1fK", val);
        if (s.endsWith(".0K"))
            s.replace(".0K", "K");
        return s;
    }
    if (count > 0) {
        return QString::number(count);
    }
    return "";
}

void PlayerWidget::setChannelAvatarPixmap(const QPixmap &pix) {
    if (!m_channelAvatarLabel || pix.isNull())
        return;

    m_channelAvatarLabel->setText("");
    m_channelAvatarLabel->setPixmap(pix);
    m_channelAvatarLabel->setStyleSheet("background: transparent; border: none;");
}

void PlayerWidget::loadChannelAvatar(const std::string &avatarUrl) {
    if (avatarUrl.empty()) {
        m_loadedAvatarUrl.clear();
        if (m_channelAvatarLabel) {
            m_channelAvatarLabel->clear();
        }
        return;
    }

    std::string url = lightyt::utils::normalizeAvatarUrl(avatarUrl, /*preferWebp=*/true);
    if (url == m_loadedAvatarUrl && m_channelAvatarLabel && !m_channelAvatarLabel->pixmap().isNull()) {
        return;
    }

    QString qUrl = QString::fromUtf8(url.c_str());
    if (QPixmap *cached = m_avatarCache.object(qUrl)) {
        m_loadedAvatarUrl = url;
        setChannelAvatarPixmap(*cached);
        return;
    }

    if (m_avatarThread.joinable()) {
        m_avatarThread.detach();
    }

    int size = (m_channelAvatarLabel && m_channelAvatarLabel->width() > 0) ? m_channelAvatarLabel->width() : 40;
    uint64_t currentGen = m_generation.load(std::memory_order_relaxed);
    auto cancel = m_cancelToken;
    QPointer<PlayerWidget> weakThis(this);

    m_avatarThread = std::thread([weakThis, url, size, currentGen, cancel]() {
        if (cancel && cancel->load())
            return;

        std::vector<uint8_t> buffer;
        bool haveData = ThumbnailCache::instance().readDisk(url, buffer) && !buffer.empty();
        if (!haveData) {
            if (cancel && cancel->load())
                return;
            if (HttpClient::downloadBytes(url, buffer, 8, 5 * 1024 * 1024, cancel.get()) && !buffer.empty()) {
                (void)ThumbnailCache::instance().writeDisk(url, buffer);
                haveData = true;
            }
        }

        if (!haveData || (cancel && cancel->load()))
            return;

        QImage dlImage = ThumbnailLoader::decodeImageFromBytes(buffer);
        if (dlImage.isNull()) {
            std::string jpegUrl = lightyt::utils::convertAvatarUrlToJpeg(url);
            if (jpegUrl != url) {
                std::vector<uint8_t> fallbackBuf;
                if (HttpClient::downloadBytes(jpegUrl, fallbackBuf, 8, 5 * 1024 * 1024, cancel.get()) &&
                    !fallbackBuf.empty()) {
                    dlImage = ThumbnailLoader::decodeImageFromBytes(fallbackBuf);
                    if (!dlImage.isNull()) {
                        (void)ThumbnailCache::instance().writeDisk(url, fallbackBuf);
                    }
                }
            }
        }

        if (dlImage.isNull()) {
            LOG_WARN("[PlayerWidget] Failed to decode avatar image ({} bytes) from {}", buffer.size(), url);
            return;
        }

        int renderSize = size * 2;
        QImage circularImg(renderSize, renderSize, QImage::Format_ARGB32_Premultiplied);
        circularImg.fill(Qt::transparent);
        {
            QPainter painter(&circularImg);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

            QPainterPath clip;
            clip.addEllipse(0, 0, renderSize, renderSize);
            painter.setClipPath(clip);

            QImage scaled =
                dlImage.scaled(renderSize, renderSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            int x = std::max(0, (scaled.width() - renderSize) / 2);
            int y = std::max(0, (scaled.height() - renderSize) / 2);
            painter.drawImage(0, 0, scaled.copy(x, y, renderSize, renderSize));
        }
        circularImg.setDevicePixelRatio(2.0);

        QMetaObject::invokeMethod(
            qApp,
            [weakThis, url, circularImg = std::move(circularImg), currentGen, cancel]() {
                if (!weakThis || (cancel && cancel->load()) ||
                    weakThis->m_generation.load(std::memory_order_relaxed) != currentGen) {
                    return;
                }
                QPixmap pix = QPixmap::fromImage(circularImg);
                weakThis->m_avatarCache.insert(QString::fromUtf8(url.c_str()), new QPixmap(pix), 1);
                weakThis->m_loadedAvatarUrl = url;
                weakThis->setChannelAvatarPixmap(pix);
            },
            Qt::QueuedConnection);
    });
}

void PlayerWidget::toggleStatsForNerds() {
    if (!m_statsWidget) {
        return;
    }
    bool willShow = !m_statsWidget->isVisible();
    if (willShow) {
        updateStatsContext();
        m_statsWidget->adjustSize();
        m_statsWidget->clampToParent();
        m_statsWidget->show();
        m_statsWidget->raise();
    } else {
        m_statsWidget->hide();
    }
}

void PlayerWidget::showPlayerContextMenu(const QPoint &globalPos) {
    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu {"
        "  background-color: #212121;"
        "  color: #f1f1f1;"
        "  border: 1px solid rgba(255, 255, 255, 0.18);"
        "  border-radius: 8px;"
        "  padding: 6px 4px;"
        "  font-size: 13px;"
        "}"
        "QMenu::item {"
        "  color: #f1f1f1;"
        "  background-color: transparent;"
        "  padding: 7px 24px 7px 20px;"
        "  border-radius: 4px;"
        "  margin: 1px 2px;"
        "}"
        "QMenu::item:selected {"
        "  background-color: rgba(255, 255, 255, 0.15);"
        "  color: #ffffff;"
        "}"
        "QMenu::item:disabled {"
        "  color: #717171;"
        "}"
        "QMenu::separator {"
        "  height: 1px;"
        "  background-color: rgba(255, 255, 255, 0.12);"
        "  margin: 4px 6px;"
        "}");

    bool statsVisible = (m_statsWidget && m_statsWidget->isVisible());
    auto *statsAction = menu.addAction(statsVisible ? "Hide stats for nerds" : "Stats for nerds");
    if (statsVisible) {
        statsAction->setIcon(lightyt::ui::icons::checkMark(QColor(0xf1, 0xf1, 0xf1), 14));
    }
    connect(statsAction, &QAction::triggered, this, &PlayerWidget::toggleStatsForNerds);

    menu.addSeparator();

    auto *copyUrlAction = menu.addAction("Copy video URL");
    connect(copyUrlAction, &QAction::triggered, this, [this]() {
        if (!m_currentItem.id.empty()) {
            QString url = "https://www.youtube.com/watch?v=" + QString::fromStdString(m_currentItem.id);
            QGuiApplication::clipboard()->setText(url);
        }
    });

    auto *copyUrlAtTimeAction = menu.addAction("Copy video URL at current time");
    connect(copyUrlAtTimeAction, &QAction::triggered, this, [this]() {
        if (!m_currentItem.id.empty() && m_player) {
            int64_t sec = static_cast<int64_t>(std::floor(m_player->getPosition()));
            QString url = QString("https://www.youtube.com/watch?v=%1&t=%2s")
                              .arg(QString::fromStdString(m_currentItem.id))
                              .arg(sec);
            QGuiApplication::clipboard()->setText(url);
        }
    });

    menu.exec(globalPos);
}

void PlayerWidget::updateStatsContext() {
    if (!m_statsWidget) {
        return;
    }
    if (m_player) {
        m_statsWidget->setPlayerController(m_player.get());
    }
    if (m_videoSurface) {
        m_statsWidget->setViewportDimensions(m_videoSurface->width(), m_videoSurface->height(), devicePixelRatio());
    }
    QString optimal = "1920x1080@60";
    if (!m_quality.isEmpty() && m_quality != "Auto") {
        optimal = m_quality + "@60";
    }
    m_statsWidget->setVideoContext(m_currentItem.id, m_currentVideoItag, m_currentAudioItag, optimal);
}
