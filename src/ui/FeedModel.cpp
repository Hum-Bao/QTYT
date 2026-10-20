#include "FeedModel.h"

#include "ThumbnailLoader.h"

FeedModel::FeedModel(QObject *parent) : QAbstractListModel(parent) {}

int FeedModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_filteredIndices.size());
}

QVariant FeedModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid())
        return {};
    int row = index.row();
    if (row < 0 || row >= static_cast<int>(m_filteredIndices.size()))
        return {};

    const auto &item = m_allVideos[m_filteredIndices[row]];

    switch (role) {
    case Qt::DisplayRole:
    case TitleRole:
        return QString::fromUtf8(item.title.c_str());
    case VideoIdRole:
        return QString::fromUtf8(item.id.c_str());
    case AuthorRole:
        return QString::fromUtf8(item.author.c_str());
    case DurationRole:
        return QString::fromUtf8(item.duration.c_str());
    case ViewCountRole:
        return QString::fromUtf8(item.viewCount.c_str());
    case PublishedTimeRole:
        return QString::fromUtf8(item.publishedTime.c_str());
    case ThumbnailUrlRole:
        return QString::fromUtf8(item.thumbnailUrl.c_str());
    case IsShortRole:
        return item.isShort;
    case ThumbnailPixmapRole: {
        if (item.thumbnailUrl.empty() || m_loader == nullptr) {
            return {};
        }
        QPixmap cached = m_loader->getCached(item.thumbnailUrl);
        if (!cached.isNull()) {
            return cached;
        }
        // Lazy-trigger download off-thread (never blocks or emits synchronously)
        m_loader->requestThumbnail(item.thumbnailUrl);
        return {};
    }
    case ChannelAvatarUrlRole:
        return QString::fromUtf8(item.channelAvatarUrl.c_str());
    case ChannelAvatarPixmapRole: {
        if (item.channelAvatarUrl.empty() || m_loader == nullptr) {
            return {};
        }
        QPixmap cached = m_loader->getCachedAvatar(item.channelAvatarUrl, 36);
        if (!cached.isNull()) {
            return cached;
        }
        // Lazy-trigger download & circular clip off-thread (never blocks paint)
        m_loader->requestAvatar(item.channelAvatarUrl, 36);
        return {};
    }
    default:
        break;
    }

    return {};
}

void FeedModel::setThumbnailLoader(ThumbnailLoader *loader) {
    m_loader = loader;
}

void FeedModel::indexRowLocked(int row, const VideoItem &item) {
    if (!item.thumbnailUrl.empty()) {
        m_urlToRows[item.thumbnailUrl].push_back(row);
    }
    if (!item.channelAvatarUrl.empty()) {
        m_avatarUrlToRows[item.channelAvatarUrl].push_back(row);
    }
}

void FeedModel::setHideShorts(bool hide) {
    if (m_hideShorts == hide)
        return;

    beginResetModel();
    m_hideShorts = hide;
    m_filteredIndices.clear();
    m_urlToRows.clear();
    m_avatarUrlToRows.clear();

    for (size_t i = 0; i < m_allVideos.size(); ++i) {
        const auto &item = m_allVideos[i];
        if (m_hideShorts && item.isShort) {
            continue;
        }
        int row = static_cast<int>(m_filteredIndices.size());
        m_filteredIndices.push_back(i);
        indexRowLocked(row, item);
    }
    endResetModel();
}

void FeedModel::setVideos(const std::vector<VideoItem> &videos) {
    clearVideos();
    appendVideos(videos);
}

void FeedModel::appendVideos(const std::vector<VideoItem> &videos) {
    if (videos.empty())
        return;

    std::vector<size_t> newIndices;
    for (const auto &v : videos) {
        if (v.id.empty())
            continue;
        if (m_knownIds.insert(v.id).second) {
            size_t globalIdx = m_allVideos.size();
            m_allVideos.push_back(v);
            if (m_hideShorts && v.isShort) {
                continue;
            }
            newIndices.push_back(globalIdx);
        }
    }

    if (newIndices.empty())
        return;

    int first = static_cast<int>(m_filteredIndices.size());
    int last = first + static_cast<int>(newIndices.size()) - 1;

    beginInsertRows(QModelIndex(), first, last);
    for (size_t idx : newIndices) {
        int row = static_cast<int>(m_filteredIndices.size());
        m_filteredIndices.push_back(idx);
        indexRowLocked(row, m_allVideos[idx]);
    }
    endInsertRows();
}

void FeedModel::clearVideos() {
    beginResetModel();
    m_allVideos.clear();
    m_knownIds.clear();
    m_filteredIndices.clear();
    m_urlToRows.clear();
    m_avatarUrlToRows.clear();
    endResetModel();
}

void FeedModel::setThumbnail(const std::string &url, const QPixmap &) {
    auto it = m_urlToRows.find(url);
    if (it != m_urlToRows.end()) {
        for (int r : it->second) {
            QModelIndex idx = index(r, 0);
            emit dataChanged(idx, idx, {ThumbnailPixmapRole});
        }
    }
}

void FeedModel::setAvatar(const std::string &url, const QPixmap &) {
    auto it = m_avatarUrlToRows.find(url);
    if (it != m_avatarUrlToRows.end()) {
        for (int r : it->second) {
            QModelIndex idx = index(r, 0);
            emit dataChanged(idx, idx, {ChannelAvatarPixmapRole});
        }
    }
}

const VideoItem *FeedModel::getVideoItem(int row) const {
    if (row >= 0 && row < static_cast<int>(m_filteredIndices.size())) {
        return &m_allVideos[m_filteredIndices[row]];
    }
    return nullptr;
}
