#pragma once

#include "../core/VideoItem.h"
#include "FeedView.h"

#include <QLabel>
#include <QPushButton>
#include <QWidget>
#include <memory>
#include <vector>

class ThumbnailLoader;

class PlaylistCoverWidget : public QWidget {
    Q_OBJECT
public:
    explicit PlaylistCoverWidget(QWidget *parent = nullptr);

    void setPixmap(const QPixmap &pix);
    [[nodiscard]] const QPixmap &pixmap() const noexcept { return m_pixmap; }

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    QPixmap m_pixmap;
    bool m_hovered = false;
};

class PlaylistPageWidget : public QWidget {
    Q_OBJECT
public:
    explicit PlaylistPageWidget(QWidget *parent = nullptr);
    ~PlaylistPageWidget() override;

    void setPlaylist(const VideoItem &item);
    void setPlaylistDetails(const PlaylistItem &details);
    void setVideos(const std::vector<VideoItem> &videos);
    void appendVideos(const std::vector<VideoItem> &videos);
    void clear();

    [[nodiscard]] const VideoItem &currentPlaylist() const noexcept { return m_playlistItem; }
    [[nodiscard]] const std::vector<VideoItem> &videos() const;

signals:
    void backRequested();
    void videoSelected(const VideoItem &item);
    void playAllRequested();
    void shuffleRequested();
    void requestMoreVideos();

private slots:
    void onCoverThumbnailLoaded(const std::string &url, const QPixmap &pix);
    void onPlayAllClicked();
    void onShuffleClicked();

private:
    VideoItem m_playlistItem;
    std::string m_coverUrl;

    PlaylistCoverWidget *m_coverWidget = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_authorLabel = nullptr;
    QLabel *m_metaLabel = nullptr;
    QLabel *m_descLabel = nullptr;
    QPushButton *m_playAllBtn = nullptr;
    QPushButton *m_shuffleBtn = nullptr;

    FeedView *m_videoList = nullptr;
    std::unique_ptr<ThumbnailLoader> m_thumbLoader;

    void updateMetadataDisplay();
};
