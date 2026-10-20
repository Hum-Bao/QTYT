#pragma once

#include "../core/VideoItem.h"

#include <QAbstractListModel>
#include <QPixmap>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

enum FeedItemRole {
    VideoIdRole = Qt::UserRole + 1,
    TitleRole,
    AuthorRole,
    DurationRole,
    ViewCountRole,
    PublishedTimeRole,
    ThumbnailUrlRole,
    ThumbnailPixmapRole,
    IsShortRole,
    VideoItemRole,
    ChannelAvatarUrlRole,
    ChannelAvatarPixmapRole
};

class FeedModel : public QAbstractListModel {
    Q_OBJECT
public:
    explicit FeedModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    void setVideos(const std::vector<VideoItem> &videos);
    void appendVideos(const std::vector<VideoItem> &videos);
    void clearVideos();

    void setHideShorts(bool hide);
    [[nodiscard]] bool isHideShorts() const noexcept { return m_hideShorts; }

    void setThumbnailLoader(class ThumbnailLoader *loader);
    void setThumbnail(const std::string &url, const QPixmap &pixmap = {});
    void setAvatar(const std::string &url, const QPixmap &pixmap = {});

    [[nodiscard]] const VideoItem *getVideoItem(int row) const;
    [[nodiscard]] const std::vector<VideoItem> &getVideos() const noexcept { return m_allVideos; }

private:
    void indexRowLocked(int row, const VideoItem &item);

    std::vector<VideoItem> m_allVideos;
    std::vector<size_t> m_filteredIndices;
    std::unordered_set<std::string> m_knownIds;
    bool m_hideShorts = false;
    std::unordered_map<std::string, std::vector<int>> m_urlToRows;
    std::unordered_map<std::string, std::vector<int>> m_avatarUrlToRows;
    class ThumbnailLoader *m_loader = nullptr;
};
