#include "FeedView.h"

#include <QCoreApplication>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QScrollBar>
#include <QScrollArea>
#include <QWheelEvent>
#include <algorithm>

namespace {

constexpr const char *kGridFeedStyleSheet =
    "QListView {"
    "  background-color: #0f0f0f;"
    "  border: none;"
    "  padding: 12px;"
    "}"
    "QScrollBar:vertical {"
    "  background: #0f0f0f; width: 10px; margin: 0px;"
    "}"
    "QScrollBar::handle:vertical {"
    "  background: #333333; min-height: 20px; border-radius: 5px;"
    "}"
    "QScrollBar::handle:vertical:hover { background: #555555; }"
    "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }";

constexpr const char *kCompactFeedStyleSheet =
    "QListView {"
    "  background-color: transparent;"
    "  border: none;"
    "  padding: 0px;"
    "}";

}  // namespace

FeedView::FeedView(QWidget *parent)
    : QListView(parent), m_model(new FeedModel(this)), m_delegate(new VideoCardDelegate(this)),
      m_thumbLoader(std::make_unique<ThumbnailLoader>(this)) {
    setModel(m_model);
    setItemDelegate(m_delegate);

    setViewMode(QListView::IconMode);
    setMovement(QListView::Static);
    setResizeMode(QListView::Adjust);
    setLayoutMode(QListView::Batched);
    setBatchSize(20);
    setUniformItemSizes(true);
    setMouseTracking(true);
    setSpacing(0);

    setStyleSheet(kGridFeedStyleSheet);

    connect(this, &QListView::clicked, this, &FeedView::onItemClicked);

    // Infinite scroll pagination when scrolled past 85%
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, &FeedView::onScrollValueChanged);

    // Wire thumbnail loader to model (lazy lookup + async queued notifications)
    if (m_model && m_thumbLoader) {
        m_model->setThumbnailLoader(m_thumbLoader.get());
        connect(m_thumbLoader.get(), &ThumbnailLoader::thumbnailLoaded, m_model, &FeedModel::setThumbnail);
        connect(m_thumbLoader.get(), &ThumbnailLoader::avatarLoaded, m_model, &FeedModel::setAvatar);
    }

    m_hoverTimer = new QTimer(this);
    m_hoverTimer->setSingleShot(true);
    connect(m_hoverTimer, &QTimer::timeout, this, &FeedView::onHoverTimeout);
}

FeedView::~FeedView() {
    if (m_hoverTimer) {
        m_hoverTimer->stop();
    }
}

void FeedView::setLayoutMode(CardLayoutMode mode) {
    if (m_layoutMode == mode)
        return;

    m_layoutMode = mode;
    if (m_delegate) {
        m_delegate->setLayoutMode(mode);
    }

    if (m_layoutMode == CardLayoutMode::CompactHorizontal) {
        m_forwardWheelEvents = true;
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setStyleSheet(kCompactFeedStyleSheet);
    } else {
        m_forwardWheelEvents = false;
        setMinimumHeight(0);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setStyleSheet(kGridFeedStyleSheet);
    }
    updateGridLayout();
}

void FeedView::setVideos(const std::vector<VideoItem> &videos) {
    if (m_thumbLoader) {
        m_thumbLoader->cancelPending();
    }
    if (m_model) {
        m_model->setVideos(videos);
    }
    updateGridLayout();
}

void FeedView::appendVideos(const std::vector<VideoItem> &videos) {
    if (m_model) {
        m_model->appendVideos(videos);
    }
    updateGridLayout();
}

void FeedView::clearVideos() {
    if (m_thumbLoader) {
        m_thumbLoader->cancelPending();
    }
    if (m_model) {
        m_model->clearVideos();
    }
    updateGridLayout();
}

void FeedView::setHideShorts(bool hide) {
    if (m_model) {
        m_model->setHideShorts(hide);
        updateGridLayout();
    }
}

bool FeedView::isHideShorts() const {
    return m_model ? m_model->isHideShorts() : false;
}

void FeedView::updateGridLayout() {
    if (m_layoutMode == CardLayoutMode::CompactHorizontal) {
        int availableWidth = std::max(240, viewport()->width());
        constexpr int compactHeight = 102;
        QSize cellSize(availableWidth, compactHeight);
        if (m_delegate && m_delegate->cardSize() != cellSize) {
            m_delegate->setCardSize(cellSize);
        }
        if (gridSize() != cellSize) {
            setGridSize(cellSize);
        }
        if (m_model) {
            int totalHeight = m_model->rowCount() * compactHeight;
            setMinimumHeight(totalHeight);
        } else {
            setMinimumHeight(0);
        }
        updateGeometry();
        return;
    }

    int availableWidth = std::max(300, viewport()->width() - 4);

    constexpr int minCardWidth = 300;
    int cols = std::clamp(availableWidth / minCardWidth, 1, 6);
    int colWidth = availableWidth / cols;

    int thumbW = std::max(160, colWidth - 16);
    int thumbH = (thumbW * 9) / 16;
    int cardHeight = thumbH + 100;

    QSize cellSize(colWidth, cardHeight);
    if (m_delegate && m_delegate->cardSize() != cellSize) {
        m_delegate->setCardSize(cellSize);
    }
    if (gridSize() != cellSize) {
        setGridSize(cellSize);
    }
}

void FeedView::resizeEvent(QResizeEvent *event) {
    QListView::resizeEvent(event);
    updateGridLayout();
    if (m_layoutMode != CardLayoutMode::CompactHorizontal) {
        if (m_model && m_model->rowCount() >= 20 && verticalScrollBar()->maximum() == 0) {
            emit requestMoreVideos();
        }
    }
}

void FeedView::mouseMoveEvent(QMouseEvent *event) {
    QListView::mouseMoveEvent(event);
    QModelIndex index = indexAt(event->pos());
    if (index != m_hoveredIndex) {
        m_hoveredIndex = index;
        if (m_hoverTimer) {
            m_hoverTimer->start(200);  // 200ms debounce before speculative stream prefetch
        }
    }
}

void FeedView::leaveEvent(QEvent *event) {
    QListView::leaveEvent(event);
    if (m_hoverTimer) {
        m_hoverTimer->stop();
    }
    m_hoveredIndex = QModelIndex();
}

void FeedView::wheelEvent(QWheelEvent *event) {
    if (m_forwardWheelEvents) {
        QWidget *p = parentWidget();
        while (p) {
            if (auto *sa = qobject_cast<QScrollArea *>(p)) {
                QCoreApplication::sendEvent(sa->viewport(), event);
                event->accept();
                return;
            }
            p = p->parentWidget();
        }
    }
    QListView::wheelEvent(event);
}

void FeedView::onHoverTimeout() {
    if (!m_hoveredIndex.isValid() || !m_model)
        return;
    const auto *item = m_model->getVideoItem(m_hoveredIndex.row());
    if (item != nullptr && !item->id.empty()) {
        emit videoHovered(*item);
    }
}

void FeedView::onItemClicked(const QModelIndex &index) {
    if (!index.isValid() || !m_model)
        return;
    const auto *item = m_model->getVideoItem(index.row());
    if (item != nullptr) {
        emit videoSelected(*item);
    }
}

void FeedView::onScrollValueChanged(int value) {
    if (m_layoutMode == CardLayoutMode::CompactHorizontal)
        return;
    int max = verticalScrollBar()->maximum();
    if (max > 0) {
        if (value >= (max * 85 / 100)) {
            emit requestMoreVideos();
        }
    }
}
