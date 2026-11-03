#pragma once

#include "../core/VideoItem.h"

#include <QAbstractListModel>
#include <QAction>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QPixmap>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QWidget>
#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class YouTubeClient;

namespace lightyt::ui {

struct CommentRow {
    CommentItem item;
    bool isExpanded = false;        // Body text expanded ("...Read more" clicked)
    bool repliesExpanded = false;   // Nested reply rows revealed below
    bool repliesLoaded = false;     // Whether replies were fetched from API
    bool isLoadingReplies = false;  // Whether replies fetch is currently in progress
    int parentIndex = -1;           // -1 for top-level comments, or index of parent comment in row list
    std::vector<CommentItem> replies; // Cached reply items
};

class CommentModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum CommentRoles {
        CommentIdRole = Qt::UserRole + 1,
        AuthorRole,
        TextRole,
        PublishedTimeRole,
        LikeCountTextRole,
        LikeCountRole,
        IsLikedRole,
        IsDislikedRole,
        IsPinnedRole,
        IsHeartedRole,
        HeartTooltipRole,
        IsAuthorOwnerRole,
        IsReplyRole,
        ReplyCountRole,
        ReplyCountTextRole,
        RepliesExpandedRole,
        IsLoadingRepliesRole,
        IsExpandedTextRole,
        AvatarPixmapRole,
        ParentIndexRole
    };

    explicit CommentModel(QObject *parent = nullptr);
    ~CommentModel() override;

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    void setComments(const std::vector<CommentItem> &comments);
    void appendComments(const std::vector<CommentItem> &comments);
    void clear();

    void toggleTextExpanded(int row);
    void setRepliesExpanded(int row, bool expanded);
    void setLoadingReplies(int row, bool loading);
    void insertReplies(int parentRow, const std::vector<CommentItem> &replies);

    void toggleLike(int row);
    void toggleDislike(int row);

    [[nodiscard]] const CommentRow *getCommentRow(int row) const;
    [[nodiscard]] CommentRow *getCommentRow(int row);

    void onAvatarLoaded(const std::string &url, int size, const QPixmap &pixmap);

private:
    std::vector<CommentRow> m_rows;
    std::unordered_map<std::string, std::vector<int>> m_avatarUrlToRows;
    void indexAvatars();
};

class CommentDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    struct HitTarget {
        enum Type { None, Like, Dislike, ReadMore, ToggleReplies };
        Type type = None;
        int row = -1;
    };

    struct HitRects {
        QRect likeRect;
        QRect dislikeRect;
        QRect readMoreRect;
        QRect repliesRect;
    };

    explicit CommentDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    [[nodiscard]] HitRects computeHitRects(const QStyleOptionViewItem &option, const QModelIndex &index) const;
    [[nodiscard]] HitTarget hitTest(const QPoint &pos, const QStyleOptionViewItem &option, const QModelIndex &index) const;

    void setHoverTarget(const HitTarget &target) noexcept { m_hoverTarget = target; }
    void clearHoverTarget() noexcept { m_hoverTarget = {}; }
    [[nodiscard]] const HitTarget &hoverTarget() const noexcept { return m_hoverTarget; }

private:
    HitTarget m_hoverTarget;
    struct TextLayoutCache {
        QString fullText;
        int width = 0;
        bool isExpanded = false;
        int textHeight = 0;
        bool showMoreButton = false;
        QString textToDraw;
    };
    mutable std::unordered_map<std::string, TextLayoutCache> m_layoutCache;
};

class CommentListView : public QListView {
    Q_OBJECT
public:
    explicit CommentListView(QWidget *parent = nullptr);

    void setForwardWheelEvents(bool forward) noexcept { m_forwardWheelEvents = forward; }
    void updateContentHeight();

signals:
    void likeClicked(int row);
    void dislikeClicked(int row);
    void readMoreClicked(int row);
    void toggleRepliesClicked(int row);

protected:
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    bool m_forwardWheelEvents = true;
};

class CommentsWidget : public QWidget {
    Q_OBJECT
public:
    explicit CommentsWidget(QWidget *parent = nullptr);
    ~CommentsWidget() override;

    void setYouTubeClient(YouTubeClient *client);
    void loadCommentsForVideo(const std::string &videoId, const std::string &commentsToken,
                              const std::string &commentCountText = "");
    void loadNextPageIfNeeded();
    void clear();

    [[nodiscard]] bool hasComments() const noexcept;

signals:
    void heightChanged();

private slots:
    void onLikeClicked(int row);
    void onDislikeClicked(int row);
    void onReadMoreClicked(int row);
    void onToggleRepliesClicked(int row);
    void onSortTopClicked();
    void onSortNewestClicked();

private:
    YouTubeClient *m_client = nullptr;
    std::string m_currentVideoId;
    std::string m_nextContinuationToken;
    std::string m_topSortToken;
    std::string m_newestSortToken;
    bool m_isTopSort = true;
    bool m_isLoading = false;
    uint64_t m_currentGen = 0;
    std::shared_ptr<std::atomic<bool>> m_cancelToken;

    QLabel *m_headerCountLabel = nullptr;
    QPushButton *m_sortBtn = nullptr;
    QMenu *m_sortMenu = nullptr;
    QAction *m_sortTopAction = nullptr;
    QAction *m_sortNewestAction = nullptr;
    QLabel *m_statusLabel = nullptr;

    CommentListView *m_listView = nullptr;
    CommentModel *m_model = nullptr;
    CommentDelegate *m_delegate = nullptr;

    void setupUi();
    void fetchCommentsPage(const std::string &token, bool isReset);
    void fetchRepliesForComment(int row, const std::string &token);
};

}  // namespace lightyt::ui

