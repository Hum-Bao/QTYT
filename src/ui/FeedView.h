#pragma once

#include "../core/VideoItem.h"
#include "FeedModel.h"
#include "ThumbnailLoader.h"
#include "VideoCardDelegate.h"

#include <QListView>
#include <QTimer>
#include <memory>

class FeedView : public QListView {
    Q_OBJECT
public:
    using CardLayoutMode = VideoCardDelegate::CardLayoutMode;
    using QListView::setLayoutMode;

    explicit FeedView(QWidget *parent = nullptr);
    ~FeedView() override;

    void setLayoutMode(CardLayoutMode mode);
    [[nodiscard]] CardLayoutMode layoutMode() const noexcept { return m_layoutMode; }
    [[nodiscard]] CardLayoutMode cardLayoutMode() const noexcept { return m_layoutMode; }

    void setForwardWheelEvents(bool forward) noexcept { m_forwardWheelEvents = forward; }
    [[nodiscard]] bool forwardWheelEvents() const noexcept { return m_forwardWheelEvents; }

    [[nodiscard]] FeedModel *model() const noexcept { return m_model; }

    void setVideos(const std::vector<VideoItem> &videos);
    void appendVideos(const std::vector<VideoItem> &videos);
    void clearVideos();

    void setHideShorts(bool hide);
    [[nodiscard]] bool isHideShorts() const;
    void cancelPendingThumbnails() noexcept {
        if (m_thumbLoader)
            m_thumbLoader->cancelPending();
    }

signals:
    void videoSelected(const VideoItem &item);
    void videoHovered(const VideoItem &item);
    void requestMoreVideos();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private slots:
    void onItemClicked(const QModelIndex &index);
    void onScrollValueChanged(int value);
    void onHoverTimeout();

private:
    FeedModel *m_model = nullptr;
    VideoCardDelegate *m_delegate = nullptr;
    std::unique_ptr<ThumbnailLoader> m_thumbLoader;
    QTimer *m_hoverTimer = nullptr;
    QModelIndex m_hoveredIndex;
    CardLayoutMode m_layoutMode{CardLayoutMode::Grid};
    bool m_forwardWheelEvents = false;

    void updateGridLayout();
};
