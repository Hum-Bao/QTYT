#include "PlaylistPageWidget.h"

#include "Theme.h"
#include "ThumbnailLoader.h"
#include "YouTubeIcons.h"

#include <QCursor>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QVBoxLayout>
#include <random>

namespace {

using namespace lightyt::ui::theme;

}  // namespace

// ---------------------------------------------------------
// PlaylistCoverWidget Implementation
// ---------------------------------------------------------

PlaylistCoverWidget::PlaylistCoverWidget(QWidget *parent) : QWidget(parent) {
    setFixedSize(312, 176);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
}

void PlaylistCoverWidget::setPixmap(const QPixmap &pix) {
    m_pixmap = pix;
    update();
}

void PlaylistCoverWidget::enterEvent(QEnterEvent *event) {
    QWidget::enterEvent(event);
    m_hovered = true;
    update();
}

void PlaylistCoverWidget::leaveEvent(QEvent *event) {
    QWidget::leaveEvent(event);
    m_hovered = false;
    update();
}

void PlaylistCoverWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        emit clicked();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void PlaylistCoverWidget::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    QRect r = rect();
    QPainterPath clip;
    clip.addRoundedRect(r, 12, 12);
    painter.setClipPath(clip);

    if (!m_pixmap.isNull()) {
        painter.drawPixmap(r, m_pixmap.scaled(r.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    } else {
        painter.fillRect(r, QColor(0x18, 0x18, 0x18));
    }

    if (m_hovered) {
        painter.fillRect(r, QColor(0, 0, 0, 110));

        // Draw "Play all" badge in center
        int badgeW = 100;
        int badgeH = 34;
        QRect badgeRect((r.width() - badgeW) / 2, (r.height() - badgeH) / 2, badgeW, badgeH);

        QPainterPath badgePath;
        badgePath.addRoundedRect(badgeRect, 17, 17);
        painter.fillPath(badgePath, QColor(0, 0, 0, 180));

        // Play triangle
        QPainterPath tri;
        int tx = badgeRect.left() + 14;
        int ty = badgeRect.top() + 10;
        tri.moveTo(tx, ty);
        tri.lineTo(tx + 12, ty + 7);
        tri.lineTo(tx, ty + 14);
        tri.closeSubpath();
        painter.fillPath(tri, Qt::white);

        QFont font = painter.font();
        font.setPointSize(9);
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(Qt::white);
        QRect textRect(tx + 16, badgeRect.top(), badgeRect.width() - 30, badgeH);
        painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, "Play all");
    }
}

// ---------------------------------------------------------
// PlaylistPageWidget Implementation
// ---------------------------------------------------------

PlaylistPageWidget::PlaylistPageWidget(QWidget *parent)
    : QWidget(parent), m_thumbLoader(std::make_unique<ThumbnailLoader>(this)) {
    setStyleSheet("background-color: #0f0f0f;");

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(16, 16, 16, 16);
    rootLayout->setSpacing(12);

    // Main Horizontal Content Split
    auto *mainSplitLayout = new QHBoxLayout();
    mainSplitLayout->setContentsMargins(0, 0, 0, 0);
    mainSplitLayout->setSpacing(20);

    // Left Sidebar inside ScrollArea
    auto *sidebarScroll = new QScrollArea(this);
    sidebarScroll->setFixedWidth(344);
    sidebarScroll->setWidgetResizable(true);
    sidebarScroll->setFrameShape(QFrame::NoFrame);
    sidebarScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sidebarScroll->setStyleSheet(
        "QScrollArea { background-color: transparent; border: none; }"
        "QScrollBar:vertical { background: transparent; width: 6px; margin: 0px; }"
        "QScrollBar::handle:vertical { background: #333333; border-radius: 3px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }");

    auto *sidebarWidget = new QWidget();
    sidebarWidget->setStyleSheet(
        "background-color: #161616; border-radius: 12px; border: 1px solid rgba(255, 255, 255, 0.06);");
    auto *sidebarLayout = new QVBoxLayout(sidebarWidget);
    sidebarLayout->setContentsMargins(16, 16, 16, 16);
    sidebarLayout->setSpacing(12);

    // Cover Widget
    m_coverWidget = new PlaylistCoverWidget(sidebarWidget);
    connect(m_coverWidget, &PlaylistCoverWidget::clicked, this, &PlaylistPageWidget::onPlayAllClicked);
    sidebarLayout->addWidget(m_coverWidget, 0, Qt::AlignHCenter);

    // Title
    m_titleLabel = new QLabel("Playlist Title", sidebarWidget);
    m_titleLabel->setStyleSheet("color: #f1f1f1; font-size: 20px; font-weight: bold; background: transparent; border: none;");
    m_titleLabel->setWordWrap(true);
    m_titleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sidebarLayout->addWidget(m_titleLabel);

    // Author
    m_authorLabel = new QLabel("by Channel", sidebarWidget);
    m_authorLabel->setStyleSheet("color: #ffffff; font-size: 13px; font-weight: 500; background: transparent; border: none;");
    m_authorLabel->setWordWrap(true);
    m_authorLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sidebarLayout->addWidget(m_authorLabel);

    // Metadata line
    m_metaLabel = new QLabel("Playlist • 0 videos", sidebarWidget);
    m_metaLabel->setStyleSheet("color: #aaaaaa; font-size: 12px; background: transparent; border: none;");
    m_metaLabel->setWordWrap(true);
    sidebarLayout->addWidget(m_metaLabel);

    // Action buttons row (Play all & Shuffle) - matches nav bar chips styling with 0px vertical padding & 36px fixed height
    auto *btnRowLayout = new QHBoxLayout();
    btnRowLayout->setContentsMargins(0, 4, 0, 4);
    btnRowLayout->setSpacing(8);

    m_playAllBtn = new QPushButton("Play all", sidebarWidget);
    m_playAllBtn->setIcon(lightyt::ui::icons::play(QColor(0x0f, 0x0f, 0x0f), 18));
    m_playAllBtn->setIconSize(QSize(18, 18));
    m_playAllBtn->setFixedHeight(36);
    m_playAllBtn->setCursor(Qt::PointingHandCursor);
    m_playAllBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: #f1f1f1;"
        "  color: #0f0f0f;"
        "  border: 1px solid #f1f1f1;"
        "  font-weight: 600;"
        "  padding: 0px 14px;"
        "  border-radius: 18px;"
        "  min-height: 34px;"
        "  max-height: 34px;"
        "  font-size: 13px;"
        "}"
        "QPushButton:hover { background-color: #ffffff; border-color: #ffffff; }"
        "QPushButton:pressed { background-color: #cccccc; border-color: #cccccc; }");
    connect(m_playAllBtn, &QPushButton::clicked, this, &PlaylistPageWidget::onPlayAllClicked);
    btnRowLayout->addWidget(m_playAllBtn);

    m_shuffleBtn = new QPushButton("Shuffle", sidebarWidget);
    m_shuffleBtn->setIcon(lightyt::ui::icons::shuffle(QColor(0xf1, 0xf1, 0xf1), 18));
    m_shuffleBtn->setIconSize(QSize(18, 18));
    m_shuffleBtn->setFixedHeight(36);
    m_shuffleBtn->setCursor(Qt::PointingHandCursor);
    m_shuffleBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: #272727;"
        "  color: #f1f1f1;"
        "  border: 1px solid rgba(255, 255, 255, 0.08);"
        "  font-weight: 500;"
        "  padding: 0px 14px;"
        "  border-radius: 18px;"
        "  min-height: 34px;"
        "  max-height: 34px;"
        "  font-size: 13px;"
        "}"
        "QPushButton:hover { background-color: #3f3f3f; }"
        "QPushButton:pressed { background-color: #1f1f1f; }");
    connect(m_shuffleBtn, &QPushButton::clicked, this, &PlaylistPageWidget::onShuffleClicked);
    btnRowLayout->addWidget(m_shuffleBtn);

    btnRowLayout->addStretch(1);
    sidebarLayout->addLayout(btnRowLayout);

    // Description
    m_descLabel = new QLabel("", sidebarWidget);
    m_descLabel->setStyleSheet("color: #aaaaaa; font-size: 12px; background: transparent; border: none;");
    m_descLabel->setWordWrap(true);
    m_descLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sidebarLayout->addWidget(m_descLabel);

    sidebarLayout->addStretch(1);
    sidebarScroll->setWidget(sidebarWidget);
    mainSplitLayout->addWidget(sidebarScroll);

    // Right Column: Playlist Video Column (FeedView in Playlist mode)
    m_videoList = new FeedView(this);
    m_videoList->setLayoutMode(CardLayoutMode::Playlist);
    connect(m_videoList, &FeedView::videoSelected, this, &PlaylistPageWidget::videoSelected);
    connect(m_videoList, &FeedView::requestMoreVideos, this, &PlaylistPageWidget::requestMoreVideos);
    mainSplitLayout->addWidget(m_videoList, 1);

    rootLayout->addLayout(mainSplitLayout, 1);

    // Wire thumbnail loader for cover
    if (m_thumbLoader) {
        connect(m_thumbLoader.get(), &ThumbnailLoader::thumbnailLoaded, this,
                &PlaylistPageWidget::onCoverThumbnailLoaded);
    }
}

PlaylistPageWidget::~PlaylistPageWidget() = default;

void PlaylistPageWidget::setPlaylist(const VideoItem &item) {
    m_playlistItem = item;
    m_coverUrl = item.thumbnailUrl;

    if (m_titleLabel) {
        m_titleLabel->setText(QString::fromUtf8(item.title.c_str()));
    }
    if (m_authorLabel) {
        m_authorLabel->setText(item.author.empty() ? "" : QString("by %1").arg(QString::fromUtf8(item.author.c_str())));
    }
    if (m_descLabel) {
        m_descLabel->setText(QString::fromUtf8(item.description.c_str()));
        m_descLabel->setVisible(!item.description.empty());
    }

    updateMetadataDisplay();

    if (m_coverWidget) {
        m_coverWidget->setPixmap({});
        if (!item.thumbnailUrl.empty() && m_thumbLoader) {
            QPixmap cached = m_thumbLoader->getCached(item.thumbnailUrl);
            if (!cached.isNull()) {
                m_coverWidget->setPixmap(cached);
            } else {
                m_thumbLoader->requestThumbnail(item.thumbnailUrl);
            }
        }
    }

    if (m_videoList) {
        m_videoList->clearVideos();
    }
}

void PlaylistPageWidget::setPlaylistDetails(const PlaylistItem &details) {
    if (!details.title.empty() && m_titleLabel) {
        m_playlistItem.title = details.title;
        m_titleLabel->setText(QString::fromUtf8(details.title.c_str()));
    }
    if (!details.author.empty() && m_authorLabel) {
        m_playlistItem.author = details.author;
        m_authorLabel->setText(QString("by %1").arg(QString::fromUtf8(details.author.c_str())));
    }
    if (!details.videoCountText.empty()) {
        m_playlistItem.duration = details.videoCountText;
    }
    if (!details.viewCountText.empty()) {
        m_playlistItem.viewCount = details.viewCountText;
    }
    if (!details.updatedTime.empty()) {
        m_playlistItem.publishedTime = details.updatedTime;
    }
    if (!details.description.empty() && m_descLabel) {
        m_playlistItem.description = details.description;
        m_descLabel->setText(QString::fromUtf8(details.description.c_str()));
        m_descLabel->setVisible(true);
    }
    if (!details.thumbnailUrl.empty() && m_coverWidget && m_coverWidget->pixmap().isNull()) {
        m_coverUrl = details.thumbnailUrl;
        if (m_thumbLoader) {
            QPixmap cached = m_thumbLoader->getCached(m_coverUrl);
            if (!cached.isNull()) {
                m_coverWidget->setPixmap(cached);
            } else {
                m_thumbLoader->requestThumbnail(m_coverUrl);
            }
        }
    }
    updateMetadataDisplay();
}

void PlaylistPageWidget::updateMetadataDisplay() {
    if (!m_metaLabel)
        return;

    QString meta = "Playlist";
    if (!m_playlistItem.publishedTime.empty()) {
        meta += " • " + QString::fromUtf8(m_playlistItem.publishedTime.c_str());
    }
    if (!m_playlistItem.duration.empty()) {
        meta += " • " + QString::fromUtf8(m_playlistItem.duration.c_str());
    }
    if (!m_playlistItem.viewCount.empty()) {
        meta += " • " + QString::fromUtf8(m_playlistItem.viewCount.c_str());
    }
    m_metaLabel->setText(meta);
}

void PlaylistPageWidget::setVideos(const std::vector<VideoItem> &videos) {
    if (m_videoList) {
        m_videoList->setVideos(videos);
    }
    // If cover is still empty and videos are non-empty, use video 0 thumbnail
    if (m_coverWidget && m_coverWidget->pixmap().isNull() && !videos.empty()) {
        if (!videos.front().thumbnailUrl.empty() && m_thumbLoader) {
            m_coverUrl = videos.front().thumbnailUrl;
            QPixmap cached = m_thumbLoader->getCached(m_coverUrl);
            if (!cached.isNull()) {
                m_coverWidget->setPixmap(cached);
            } else {
                m_thumbLoader->requestThumbnail(m_coverUrl);
            }
        }
    }
}

void PlaylistPageWidget::appendVideos(const std::vector<VideoItem> &videos) {
    if (m_videoList) {
        m_videoList->appendVideos(videos);
    }
}

void PlaylistPageWidget::clear() {
    m_playlistItem = VideoItem{};
    m_coverUrl.clear();
    if (m_coverWidget)
        m_coverWidget->setPixmap({});
    if (m_titleLabel)
        m_titleLabel->setText("");
    if (m_authorLabel)
        m_authorLabel->setText("");
    if (m_metaLabel)
        m_metaLabel->setText("");
    if (m_descLabel) {
        m_descLabel->setText("");
        m_descLabel->setVisible(false);
    }
    if (m_videoList)
        m_videoList->clearVideos();
}

const std::vector<VideoItem> &PlaylistPageWidget::videos() const {
    if (m_videoList && m_videoList->model()) {
        return m_videoList->model()->getVideos();
    }
    static const std::vector<VideoItem> s_empty;
    return s_empty;
}

void PlaylistPageWidget::onCoverThumbnailLoaded(const std::string &url, const QPixmap &pix) {
    if (url == m_coverUrl && m_coverWidget && !pix.isNull()) {
        m_coverWidget->setPixmap(pix);
    }
}

void PlaylistPageWidget::onPlayAllClicked() {
    emit playAllRequested();
    const auto &v = videos();
    if (!v.empty()) {
        emit videoSelected(v.front());
    }
}

void PlaylistPageWidget::onShuffleClicked() {
    emit shuffleRequested();
    const auto &v = videos();
    if (!v.empty()) {
        static std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<size_t> dist(0, v.size() - 1);
        size_t idx = dist(rng);
        emit videoSelected(v[idx]);
    }
}
