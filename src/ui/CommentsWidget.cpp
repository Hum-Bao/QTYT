#include "CommentsWidget.h"

#include "../core/YouTubeClient.h"
#include "Theme.h"
#include "ThumbnailLoader.h"
#include "YouTubeIcons.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextLayout>
#include <QThreadPool>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>

namespace lightyt::ui {

using namespace theme;

namespace {

const QColor s_authorNameColor = ColorTextPrimary;
const QColor s_authorBadgeBg = ColorElevated;
const QColor s_metaTextColor = ColorTextSecondary;
const QColor s_bodyTextColor = ColorTextPrimary;
const QColor s_accentColor = ColorAccentBlue;
const QColor s_avatarFallbackBg = ColorElevated;
const QColor s_heartBg(0x38, 0x18, 0x18);
const QColor s_heartBorder(0x7f, 0x1d, 0x1d);

}  // namespace

// =============================================================================
// CommentModel Implementation
// =============================================================================

CommentModel::CommentModel(QObject *parent) : QAbstractListModel(parent) {
    connect(&ThumbnailLoader::instance(), &ThumbnailLoader::avatarReady, this, &CommentModel::onAvatarLoaded);
}

CommentModel::~CommentModel() = default;

int CommentModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_rows.size());
}

QVariant CommentModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid())
        return {};
    int row = index.row();
    if (row < 0 || row >= static_cast<int>(m_rows.size()))
        return {};

    const auto &r = m_rows[row];

    switch (role) {
    case Qt::DisplayRole:
    case TextRole:
        return QString::fromUtf8(r.item.text.c_str());
    case CommentIdRole:
        return QString::fromUtf8(r.item.commentId.c_str());
    case AuthorRole:
        return QString::fromUtf8(r.item.author.c_str());
    case PublishedTimeRole:
        return QString::fromUtf8(r.item.publishedTime.c_str());
    case LikeCountTextRole:
        return QString::fromUtf8(r.item.likeCountText.c_str());
    case LikeCountRole:
        return static_cast<qlonglong>(r.item.likeCount);
    case IsLikedRole:
        return r.item.isLiked;
    case IsDislikedRole:
        return r.item.isDisliked;
    case IsPinnedRole:
        return r.item.isPinned;
    case IsHeartedRole:
        return r.item.isHearted;
    case HeartTooltipRole:
        return QString::fromUtf8(r.item.heartTooltip.c_str());
    case IsAuthorOwnerRole:
        return r.item.isAuthorChannelOwner;
    case IsReplyRole:
        return r.item.isReply;
    case ReplyCountRole:
        return r.item.replyCount;
    case ReplyCountTextRole:
        return QString::fromUtf8(r.item.replyCountText.c_str());
    case RepliesExpandedRole:
        return r.repliesExpanded;
    case IsLoadingRepliesRole:
        return r.isLoadingReplies;
    case IsExpandedTextRole:
        return r.isExpanded;
    case ParentIndexRole:
        return r.parentIndex;
    case AvatarPixmapRole: {
        if (r.item.authorAvatarUrl.empty())
            return {};
        int size = r.item.isReply ? 24 : 36;
        QPixmap cached = ThumbnailLoader::instance().getCachedAvatar(r.item.authorAvatarUrl, size);
        if (!cached.isNull()) {
            return cached;
        }
        ThumbnailLoader::instance().requestAvatar(r.item.authorAvatarUrl, size);
        return {};
    }
    default:
        break;
    }
    return {};
}

void CommentModel::indexAvatars() {
    m_avatarUrlToRows.clear();
    for (size_t i = 0; i < m_rows.size(); ++i) {
        if (!m_rows[i].item.authorAvatarUrl.empty()) {
            m_avatarUrlToRows[m_rows[i].item.authorAvatarUrl].push_back(static_cast<int>(i));
        }
    }
}

void CommentModel::onAvatarLoaded(const std::string &url, int size, const QPixmap &) {
    auto it = m_avatarUrlToRows.find(url);
    if (it != m_avatarUrlToRows.end()) {
        for (int r : it->second) {
            if (r >= 0 && r < static_cast<int>(m_rows.size())) {
                int expectedSize = m_rows[r].item.isReply ? 24 : 36;
                if (size == expectedSize) {
                    QModelIndex idx = index(r, 0);
                    emit dataChanged(idx, idx, {AvatarPixmapRole});
                }
            }
        }
    }
}

void CommentModel::setComments(const std::vector<CommentItem> &comments) {
    beginResetModel();
    m_rows.clear();
    m_rows.reserve(comments.size());
    for (const auto &c : comments) {
        CommentRow r;
        r.item = c;
        m_rows.push_back(std::move(r));
    }
    indexAvatars();
    endResetModel();
}

void CommentModel::appendComments(const std::vector<CommentItem> &comments) {
    if (comments.empty())
        return;
    int first = static_cast<int>(m_rows.size());
    int last = first + static_cast<int>(comments.size()) - 1;
    beginInsertRows(QModelIndex(), first, last);
    for (const auto &c : comments) {
        CommentRow r;
        r.item = c;
        m_rows.push_back(std::move(r));
    }
    indexAvatars();
    endInsertRows();
}

void CommentModel::clear() {
    beginResetModel();
    m_rows.clear();
    m_avatarUrlToRows.clear();
    endResetModel();
}

void CommentModel::toggleTextExpanded(int row) {
    if (row >= 0 && row < static_cast<int>(m_rows.size())) {
        m_rows[row].isExpanded = !m_rows[row].isExpanded;
        QModelIndex idx = index(row, 0);
        emit dataChanged(idx, idx, {IsExpandedTextRole});
    }
}

void CommentModel::setLoadingReplies(int row, bool loading) {
    if (row >= 0 && row < static_cast<int>(m_rows.size())) {
        m_rows[row].isLoadingReplies = loading;
        QModelIndex idx = index(row, 0);
        emit dataChanged(idx, idx, {IsLoadingRepliesRole});
    }
}

void CommentModel::setRepliesExpanded(int row, bool expanded) {
    if (row < 0 || row >= static_cast<int>(m_rows.size()))
        return;

    if (!expanded) {
        // Collapse replies: count how many subsequent rows have parentIndex == row
        int countToRemove = 0;
        int nextRow = row + 1;
        while (nextRow < static_cast<int>(m_rows.size()) && m_rows[nextRow].parentIndex == row) {
            countToRemove++;
            nextRow++;
        }
        if (countToRemove > 0) {
            beginRemoveRows(QModelIndex(), row + 1, row + countToRemove);
            m_rows.erase(m_rows.begin() + row + 1, m_rows.begin() + row + 1 + countToRemove);
            // Adjust parentIndex of subsequent rows
            for (size_t i = row + 1; i < m_rows.size(); ++i) {
                if (m_rows[i].parentIndex > row) {
                    m_rows[i].parentIndex -= countToRemove;
                }
            }
            endRemoveRows();
        }
        m_rows[row].repliesExpanded = false;
        m_rows[row].isLoadingReplies = false;
        indexAvatars();
        QModelIndex idx = index(row, 0);
        emit dataChanged(idx, idx, {RepliesExpandedRole, IsLoadingRepliesRole});
    } else {
        m_rows[row].repliesExpanded = true;
        QModelIndex idx = index(row, 0);
        emit dataChanged(idx, idx, {RepliesExpandedRole});
    }
}

void CommentModel::insertReplies(int parentRow, const std::vector<CommentItem> &replies) {
    if (parentRow < 0 || parentRow >= static_cast<int>(m_rows.size()) || replies.empty())
        return;

    m_rows[parentRow].replies = replies;
    m_rows[parentRow].repliesLoaded = true;
    m_rows[parentRow].repliesExpanded = true;
    m_rows[parentRow].isLoadingReplies = false;

    int insertPos = parentRow + 1;
    // If some replies are already inserted, find the end of them
    while (insertPos < static_cast<int>(m_rows.size()) && m_rows[insertPos].parentIndex == parentRow) {
        insertPos++;
    }

    int first = insertPos;
    int last = insertPos + static_cast<int>(replies.size()) - 1;

    beginInsertRows(QModelIndex(), first, last);
    std::vector<CommentRow> newRows;
    newRows.reserve(replies.size());
    for (const auto &rep : replies) {
        CommentRow r;
        r.item = rep;
        r.item.isReply = true;
        r.parentIndex = parentRow;
        newRows.push_back(std::move(r));
    }
    m_rows.insert(m_rows.begin() + insertPos, newRows.begin(), newRows.end());

    // Update parentIndex of subsequent elements
    int shift = static_cast<int>(replies.size());
    for (size_t i = last + 1; i < m_rows.size(); ++i) {
        if (m_rows[i].parentIndex >= parentRow + 1) {
            m_rows[i].parentIndex += shift;
        }
    }

    indexAvatars();
    endInsertRows();

    QModelIndex parentIdx = index(parentRow, 0);
    emit dataChanged(parentIdx, parentIdx, {RepliesExpandedRole, IsLoadingRepliesRole});
}

void CommentModel::toggleLike(int row) {
    if (row < 0 || row >= static_cast<int>(m_rows.size()))
        return;
    auto &c = m_rows[row].item;
    if (c.isLiked) {
        c.isLiked = false;
        c.likeCount = std::max<int64_t>(0, c.likeCount - 1);
    } else {
        c.isLiked = true;
        c.likeCount++;
        if (c.isDisliked) {
            c.isDisliked = false;
        }
    }
    c.likeCountText = (c.likeCount > 0) ? std::to_string(c.likeCount) : "";
    QModelIndex idx = index(row, 0);
    emit dataChanged(idx, idx, {IsLikedRole, IsDislikedRole, LikeCountRole, LikeCountTextRole});
}

void CommentModel::toggleDislike(int row) {
    if (row < 0 || row >= static_cast<int>(m_rows.size()))
        return;
    auto &c = m_rows[row].item;
    if (c.isDisliked) {
        c.isDisliked = false;
    } else {
        c.isDisliked = true;
        if (c.isLiked) {
            c.isLiked = false;
            c.likeCount = std::max<int64_t>(0, c.likeCount - 1);
            c.likeCountText = (c.likeCount > 0) ? std::to_string(c.likeCount) : "";
        }
    }
    QModelIndex idx = index(row, 0);
    emit dataChanged(idx, idx, {IsLikedRole, IsDislikedRole, LikeCountRole, LikeCountTextRole});
}

const CommentRow *CommentModel::getCommentRow(int row) const {
    if (row >= 0 && row < static_cast<int>(m_rows.size())) {
        return &m_rows[row];
    }
    return nullptr;
}

CommentRow *CommentModel::getCommentRow(int row) {
    if (row >= 0 && row < static_cast<int>(m_rows.size())) {
        return &m_rows[row];
    }
    return nullptr;
}

// =============================================================================
// CommentDelegate Implementation
// =============================================================================

CommentDelegate::CommentDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

CommentDelegate::HitRects CommentDelegate::computeHitRects(const QStyleOptionViewItem &option,
                                                          const QModelIndex &index) const {
    HitRects hr;
    if (!index.isValid())
        return hr;

    bool isReply = index.data(CommentModel::IsReplyRole).toBool();
    int indent = isReply ? 46 : 0;
    int avatarSize = isReply ? 24 : 36;

    int avatarLeft = option.rect.left() + indent;
    int textLeft = avatarLeft + avatarSize + 12;
    int textRight = option.rect.right() - 8;
    int textWidth = std::max(100, textRight - textLeft);

    QString text = index.data(CommentModel::TextRole).toString();
    bool isExpanded = index.data(CommentModel::IsExpandedTextRole).toBool();

    // Compute text height and whether "...Read more" button exists
    QFont bodyFont = option.font;
    bodyFont.setPointSize(9);
    QFontMetrics fm(bodyFont);

    QTextLayout layout(text, bodyFont);
    layout.beginLayout();
    int lineCount = 0;
    while (true) {
        QTextLine line = layout.createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(textWidth);
        lineCount++;
    }
    layout.endLayout();

    int lineH = fm.lineSpacing();
    int textHeight = 0;
    bool hasMoreBtn = false;
    if (lineCount > 4) {
        hasMoreBtn = true;
        textHeight = (!isExpanded ? 4 : lineCount) * lineH;
    } else {
        textHeight = std::max(1, lineCount) * lineH;
    }

    int headerH = 20;
    int textTop = option.rect.top() + 8 + headerH + 3;

    if (hasMoreBtn) {
        hr.readMoreRect = QRect(textLeft, textTop + textHeight + 2, 80, 20);
        textHeight += 22;
    }

    int actionsTop = textTop + textHeight + 4;
    constexpr int kBtnH = 30;

    // Compute like button width based on count text
    QString likeCountText = index.data(CommentModel::LikeCountTextRole).toString();
    QFont lkFont = option.font;
    lkFont.setPointSize(8);
    QFontMetrics lfm(lkFont);
    int countW = likeCountText.isEmpty() ? 0 : lfm.horizontalAdvance(likeCountText);
    int likeW = likeCountText.isEmpty() ? 32 : (8 + 16 + 6 + countW + 10);

    hr.likeRect = QRect(textLeft, actionsTop, likeW, kBtnH);
    hr.dislikeRect = QRect(hr.likeRect.right() + 7, actionsTop, 32, kBtnH);

    int replyCount = index.data(CommentModel::ReplyCountRole).toInt();
    if (!isReply && replyCount > 0) {
        int replyBtnTop = actionsTop + kBtnH + 6;
        QString replyText = index.data(CommentModel::ReplyCountTextRole).toString();
        if (replyText.isEmpty()) {
            replyText = QString("%1 %2").arg(replyCount).arg(replyCount == 1 ? "reply" : "replies");
        }
        QFont repFont = option.font;
        repFont.setPointSize(8);
        repFont.setBold(true);
        QFontMetrics rfm(repFont);
        int repW = 12 + 6 + rfm.horizontalAdvance(replyText) + 20;
        hr.repliesRect = QRect(textLeft, replyBtnTop, std::max(100, repW), 28);
    }

    return hr;
}

CommentDelegate::HitTarget CommentDelegate::hitTest(const QPoint &pos, const QStyleOptionViewItem &option,
                                                   const QModelIndex &index) const {
    HitTarget ht;
    ht.row = index.row();
    HitRects hr = computeHitRects(option, index);
    if (hr.likeRect.contains(pos)) {
        ht.type = HitTarget::Like;
    } else if (hr.dislikeRect.contains(pos)) {
        ht.type = HitTarget::Dislike;
    } else if (!hr.readMoreRect.isEmpty() && hr.readMoreRect.contains(pos)) {
        ht.type = HitTarget::ReadMore;
    } else if (!hr.repliesRect.isEmpty() && hr.repliesRect.contains(pos)) {
        ht.type = HitTarget::ToggleReplies;
    }
    return ht;
}

QSize CommentDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {
    if (!index.isValid())
        return QSize(200, 60);

    bool isReply = index.data(CommentModel::IsReplyRole).toBool();
    int indent = isReply ? 46 : 0;
    int avatarSize = isReply ? 24 : 36;
    int avatarLeft = indent;
    int textLeft = avatarLeft + avatarSize + 12;
    int availWidth = std::max(120, option.rect.width() - textLeft - 8);

    QString text = index.data(CommentModel::TextRole).toString();
    bool isExpanded = index.data(CommentModel::IsExpandedTextRole).toBool();

    QFont bodyFont = option.font;
    bodyFont.setPointSize(9);
    QFontMetrics fm(bodyFont);

    QTextLayout layout(text, bodyFont);
    layout.beginLayout();
    int lineCount = 0;
    while (true) {
        QTextLine line = layout.createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(availWidth);
        lineCount++;
    }
    layout.endLayout();

    int lineH = fm.lineSpacing();
    int textH = 0;
    if (lineCount > 4) {
        if (!isExpanded) {
            textH = 4 * lineH + 22;  // Truncated + "...Read more" button
        } else {
            textH = lineCount * lineH + 22;  // Full + "Show less" button
        }
    } else {
        textH = std::max(1, lineCount) * lineH;
    }

    int headerH = 20;
    constexpr int kBtnH = 30;
    int actionsH = kBtnH + 4;
    int replyCount = index.data(CommentModel::ReplyCountRole).toInt();
    int replyBtnH = (!isReply && replyCount > 0) ? 34 : 0;

    int totalH = 8 + headerH + 3 + textH + 4 + actionsH + replyBtnH + 8;
    return QSize(option.rect.width(), std::max(avatarSize + 16, totalH));
}

void CommentDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                            const QModelIndex &index) const {
    if (!index.isValid())
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);

    bool isReply = index.data(CommentModel::IsReplyRole).toBool();
    int indent = isReply ? 46 : 0;
    int avatarSize = isReply ? 24 : 36;

    // 1. Avatar circle
    int avatarLeft = option.rect.left() + indent;
    int avatarTop = option.rect.top() + 8;
    QRect avatarRect(avatarLeft, avatarTop, avatarSize, avatarSize);

    QVariant pixVar = index.data(CommentModel::AvatarPixmapRole);
    if (pixVar.isValid() && !pixVar.value<QPixmap>().isNull()) {
        painter->drawPixmap(avatarRect, pixVar.value<QPixmap>());
    } else {
        // Fallback initials circle
        painter->setPen(Qt::NoPen);
        painter->setBrush(s_avatarFallbackBg);
        painter->drawEllipse(avatarRect);

        QString author = index.data(CommentModel::AuthorRole).toString();
        QString initial = author.isEmpty() ? "?" : author.left(1).toUpper();
        painter->setPen(s_authorNameColor);
        QFont initFont = painter->font();
        initFont.setPointSize(isReply ? 8 : 10);
        initFont.setBold(true);
        painter->setFont(initFont);
        painter->drawText(avatarRect, Qt::AlignCenter, initial);
    }

    // 2. Author and Metadata Row
    int textLeft = avatarRect.right() + 12;
    int curY = option.rect.top() + 8;

    QString author = index.data(CommentModel::AuthorRole).toString();
    QString publishedTime = index.data(CommentModel::PublishedTimeRole).toString();
    bool isAuthorOwner = index.data(CommentModel::IsAuthorOwnerRole).toBool();
    bool isPinned = index.data(CommentModel::IsPinnedRole).toBool();

    int curX = textLeft;

    // Pinned Badge
    if (isPinned && !isReply) {
        QIcon pinIcon = icons::pin(13, s_metaTextColor);
        pinIcon.paint(painter, QRect(curX, curY + 2, 13, 13));
        curX += 16;

        QFont pinFont = painter->font();
        pinFont.setPointSize(8);
        pinFont.setBold(true);
        painter->setFont(pinFont);
        painter->setPen(s_metaTextColor);
        painter->drawText(QPointF(curX, curY + 13), "Pinned");
        QFontMetrics pfm(pinFont);
        curX += pfm.horizontalAdvance("Pinned") + 8;
    }

    // Author Name / Badge
    QFont authorFont = painter->font();
    authorFont.setPointSize(8);
    authorFont.setBold(true);
    painter->setFont(authorFont);
    QFontMetrics afm(authorFont);
    int authorWidth = afm.horizontalAdvance(author);

    if (isAuthorOwner) {
        // Channel creator pill highlight
        QRect badgeRect(curX - 4, curY, authorWidth + 8, 18);
        painter->setPen(Qt::NoPen);
        painter->setBrush(s_authorBadgeBg);
        painter->drawRoundedRect(badgeRect, 9, 9);
        painter->setPen(s_authorNameColor);
        painter->drawText(QPointF(curX, curY + 13), author);
        curX += authorWidth + 10;
    } else {
        painter->setPen(s_authorNameColor);
        painter->drawText(QPointF(curX, curY + 13), author);
        curX += authorWidth + 8;
    }

    // Published Date
    if (!publishedTime.isEmpty()) {
        QFont timeFont = painter->font();
        timeFont.setPointSize(8);
        timeFont.setBold(false);
        painter->setFont(timeFont);
        painter->setPen(s_metaTextColor);
        painter->drawText(QPointF(curX, curY + 13), publishedTime);
    }

    // 3. Comment Body Text
    curY += 22;
    int textRight = option.rect.right() - 8;
    int textWidth = std::max(100, textRight - textLeft);

    QString text = index.data(CommentModel::TextRole).toString();
    bool isExpanded = index.data(CommentModel::IsExpandedTextRole).toBool();

    QFont bodyFont = painter->font();
    bodyFont.setPointSize(9);
    bodyFont.setBold(false);
    painter->setFont(bodyFont);
    painter->setPen(s_bodyTextColor);
    QFontMetrics bfm(bodyFont);

    QTextLayout layout(text, bodyFont);
    layout.beginLayout();
    int lineCount = 0;
    int lineH = bfm.lineSpacing();
    int textY = curY;

    while (true) {
        QTextLine line = layout.createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(textWidth);
        if (!isExpanded && lineCount >= 4) {
            lineCount++;
            continue;
        }
        line.draw(painter, QPointF(textLeft, textY));
        textY += lineH;
        lineCount++;
    }
    layout.endLayout();

    // 4. Compute hit rects to guarantee visual and hit-testing symmetry
    HitRects hr = computeHitRects(option, index);

    // "...Read more" / "Show less" button
    if (!hr.readMoreRect.isEmpty()) {
        bool isHoveredMore = (m_hoverTarget.row == index.row() && m_hoverTarget.type == HitTarget::ReadMore);
        QFont moreFont = painter->font();
        moreFont.setPointSize(8);
        moreFont.setBold(true);
        painter->setFont(moreFont);
        painter->setPen(isHoveredMore ? QColor(0x60, 0xb8, 0xff) : s_accentColor);
        QString moreLabel = isExpanded ? "Show less" : "...Read more";
        painter->drawText(QPointF(hr.readMoreRect.left(), hr.readMoreRect.top() + 13), moreLabel);
    }

    // 5. Actions Bar (Like Container, Dislike Container, Creator Heart)
    bool isLiked = index.data(CommentModel::IsLikedRole).toBool();
    bool isDisliked = index.data(CommentModel::IsDislikedRole).toBool();
    QString likeCountText = index.data(CommentModel::LikeCountTextRole).toString();

    bool isHoveredLike = (m_hoverTarget.row == index.row() && m_hoverTarget.type == HitTarget::Like);
    bool isHoveredDislike = (m_hoverTarget.row == index.row() && m_hoverTarget.type == HitTarget::Dislike);

    // 5a. Like Button Container
    if (isHoveredLike) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(255, 255, 255, 25));
        painter->drawRoundedRect(hr.likeRect, 15, 15);
    } else if (isLiked) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(255, 255, 255, 15));
        painter->drawRoundedRect(hr.likeRect, 15, 15);
    }

    QIcon likeIcon = icons::thumbUp(16, isLiked ? ColorTextPrimary : s_metaTextColor, isLiked);
    int likeIconX = hr.likeRect.left() + (likeCountText.isEmpty() ? (hr.likeRect.width() - 16) / 2 : 8);
    int likeIconY = hr.likeRect.top() + (hr.likeRect.height() - 16) / 2;
    likeIcon.paint(painter, QRect(likeIconX, likeIconY, 16, 16));

    if (!likeCountText.isEmpty()) {
        QFont lkFont = painter->font();
        lkFont.setPointSize(8);
        lkFont.setBold(false);
        painter->setFont(lkFont);
        painter->setPen(isLiked ? ColorTextPrimary : s_metaTextColor);
        QFontMetrics lfm(lkFont);
        int textX = likeIconX + 16 + 6;
        int textY = hr.likeRect.top() + (hr.likeRect.height() + lfm.ascent() - lfm.descent()) / 2;
        painter->drawText(QPointF(textX, textY), likeCountText);
    }

    // 5b. Dislike Button Container
    if (isHoveredDislike) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(255, 255, 255, 25));
        painter->drawRoundedRect(hr.dislikeRect, 15, 15);
    } else if (isDisliked) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(255, 255, 255, 15));
        painter->drawRoundedRect(hr.dislikeRect, 15, 15);
    }

    QIcon dislikeIcon = icons::thumbDown(16, isDisliked ? ColorTextPrimary : s_metaTextColor, isDisliked);
    int dislikeIconX = hr.dislikeRect.left() + (hr.dislikeRect.width() - 16) / 2;
    int dislikeIconY = hr.dislikeRect.top() + (hr.dislikeRect.height() - 16) / 2;
    dislikeIcon.paint(painter, QRect(dislikeIconX, dislikeIconY, 16, 16));

    // 5c. Creator Heart Badge
    bool isHearted = index.data(CommentModel::IsHeartedRole).toBool();
    if (isHearted) {
        int heartLeft = hr.dislikeRect.right() + 8;
        int heartTop = hr.dislikeRect.top() + (hr.dislikeRect.height() - 18) / 2;
        QRect heartAvatarRect(heartLeft, heartTop, 18, 18);

        painter->setPen(Qt::NoPen);
        painter->setBrush(s_heartBg);
        painter->drawEllipse(heartAvatarRect);

        QIcon heartIcon = icons::heart(12, ColorBrandRed);
        heartIcon.paint(painter, QRect(heartLeft + 9, heartTop + 7, 12, 12));
    }

    // 6. Nested Replies Pill Button ("▼ X replies")
    if (!hr.repliesRect.isEmpty()) {
        bool repliesExpanded = index.data(CommentModel::RepliesExpandedRole).toBool();
        bool isHoveredReplies = (m_hoverTarget.row == index.row() && m_hoverTarget.type == HitTarget::ToggleReplies);

        if (isHoveredReplies) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(62, 166, 255, 30));
            painter->drawRoundedRect(hr.repliesRect, 14, 14);
        }

        QString replyText = index.data(CommentModel::ReplyCountTextRole).toString();
        if (replyText.isEmpty()) {
            int replyCount = index.data(CommentModel::ReplyCountRole).toInt();
            replyText = QString("%1 %2").arg(replyCount).arg(replyCount == 1 ? "reply" : "replies");
        }

        QFont repFont = painter->font();
        repFont.setPointSize(8);
        repFont.setBold(true);
        painter->setFont(repFont);
        painter->setPen(s_accentColor);
        QFontMetrics rfm(repFont);

        QIcon arrowIcon = repliesExpanded ? icons::chevronUp(12, s_accentColor)
                                          : icons::chevronDown(12, s_accentColor);
        int arrowX = hr.repliesRect.left() + 10;
        int arrowY = hr.repliesRect.top() + (hr.repliesRect.height() - 12) / 2;
        arrowIcon.paint(painter, QRect(arrowX, arrowY, 12, 12));

        int textX = arrowX + 12 + 6;
        int textY = hr.repliesRect.top() + (hr.repliesRect.height() + rfm.ascent() - rfm.descent()) / 2;
        painter->drawText(QPointF(textX, textY), replyText);
    }

    painter->restore();
}

// =============================================================================
// CommentListView Implementation
// =============================================================================

CommentListView::CommentListView(QWidget *parent) : QListView(parent) {
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setUniformItemSizes(false);
    setMouseTracking(true);
    setResizeMode(QListView::Adjust);
    setSelectionMode(QListView::NoSelection);
    setFocusPolicy(Qt::NoFocus);
    setStyleSheet("background: transparent; border: none; padding: 0;");
}

void CommentListView::updateContentHeight() {
    int totalH = 0;
    auto *m = model();
    auto *d = itemDelegate();
    if (m && d) {
        int count = m->rowCount();
        QStyleOptionViewItem opt;
        opt.rect = QRect(0, 0, std::max(200, width()), 100);
        opt.font = font();
        for (int i = 0; i < count; ++i) {
            totalH += d->sizeHint(opt, m->index(i, 0)).height();
        }
    }
    setFixedHeight(totalH);
    updateGeometry();
}

void CommentListView::wheelEvent(QWheelEvent *event) {
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

void CommentListView::mousePressEvent(QMouseEvent *event) {
    QModelIndex idx = indexAt(event->pos());
    if (idx.isValid()) {
        auto *del = qobject_cast<CommentDelegate *>(itemDelegate());
        if (del) {
            QStyleOptionViewItem opt;
            opt.rect = visualRect(idx);
            opt.font = font();
            auto target = del->hitTest(event->pos(), opt, idx);
            if (target.type == CommentDelegate::HitTarget::Like) {
                emit likeClicked(idx.row());
                event->accept();
                return;
            }
            if (target.type == CommentDelegate::HitTarget::Dislike) {
                emit dislikeClicked(idx.row());
                event->accept();
                return;
            }
            if (target.type == CommentDelegate::HitTarget::ReadMore) {
                emit readMoreClicked(idx.row());
                updateContentHeight();
                event->accept();
                return;
            }
            if (target.type == CommentDelegate::HitTarget::ToggleReplies) {
                emit toggleRepliesClicked(idx.row());
                event->accept();
                return;
            }
        }
    }
    QListView::mousePressEvent(event);
}

void CommentListView::mouseMoveEvent(QMouseEvent *event) {
    QModelIndex idx = indexAt(event->pos());
    if (idx.isValid()) {
        auto *del = qobject_cast<CommentDelegate *>(itemDelegate());
        if (del) {
            QStyleOptionViewItem opt;
            opt.rect = visualRect(idx);
            opt.font = font();
            auto target = del->hitTest(event->pos(), opt, idx);
            if (target.type != CommentDelegate::HitTarget::None) {
                setCursor(Qt::PointingHandCursor);
                if (del->hoverTarget().row != target.row || del->hoverTarget().type != target.type) {
                    del->setHoverTarget(target);
                    viewport()->update();
                }
                QListView::mouseMoveEvent(event);
                return;
            }
        }
    }
    if (auto *del = qobject_cast<CommentDelegate *>(itemDelegate())) {
        if (del->hoverTarget().type != CommentDelegate::HitTarget::None) {
            del->clearHoverTarget();
            viewport()->update();
        }
    }
    setCursor(Qt::ArrowCursor);
    QListView::mouseMoveEvent(event);
}

void CommentListView::leaveEvent(QEvent *event) {
    if (auto *del = qobject_cast<CommentDelegate *>(itemDelegate())) {
        if (del->hoverTarget().type != CommentDelegate::HitTarget::None) {
            del->clearHoverTarget();
            viewport()->update();
        }
    }
    setCursor(Qt::ArrowCursor);
    QListView::leaveEvent(event);
}

void CommentListView::resizeEvent(QResizeEvent *event) {
    QListView::resizeEvent(event);
    updateContentHeight();
}

// =============================================================================
// CommentsWidget Implementation
// =============================================================================

CommentsWidget::CommentsWidget(QWidget *parent) : QWidget(parent) {
    m_cancelToken = std::make_shared<std::atomic<bool>>(false);
    setupUi();
}

CommentsWidget::~CommentsWidget() {
    if (m_cancelToken) {
        m_cancelToken->store(true, std::memory_order_relaxed);
    }
}

void CommentsWidget::setupUi() {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 0, 16, 16);
    layout->setSpacing(12);

    // 1. Comments Header (Count & Sort Menu Button)
    auto *headerRow = new QHBoxLayout();
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->setSpacing(16);

    m_headerCountLabel = new QLabel("Comments", this);
    m_headerCountLabel->setStyleSheet(
        "font-size: 16px; font-weight: bold; color: #f1f1f1; background: transparent;");
    headerRow->addWidget(m_headerCountLabel);

    m_sortBtn = new QPushButton("Sort by", this);
    m_sortBtn->setIcon(icons::sortLines(18, ColorTextPrimary));
    m_sortBtn->setIconSize(QSize(18, 18));
    m_sortBtn->setFocusPolicy(Qt::NoFocus);
    m_sortBtn->setCursor(Qt::PointingHandCursor);
    m_sortBtn->setFixedHeight(32);
    m_sortBtn->setStyleSheet(
        "QPushButton {"
        "  background: transparent; color: #f1f1f1; font-size: 13px; font-weight: bold;"
        "  border: none; padding: 0 8px; outline: none;"
        "}"
        "QPushButton:hover { background-color: rgba(255, 255, 255, 0.1); border-radius: 16px; }");

    m_sortMenu = new QMenu(this);
    m_sortMenu->setStyleSheet(
        "QMenu {"
        "  background-color: #212121; color: #f1f1f1; border: 1px solid rgba(255, 255, 255, 0.18);"
        "  border-radius: 8px; padding: 6px 4px; font-size: 13px;"
        "}"
        "QMenu::item {"
        "  color: #f1f1f1; background-color: transparent; padding: 8px 24px 8px 20px; border-radius: 4px;"
        "}"
        "QMenu::item:selected { background-color: #383838; }");

    m_sortTopAction = m_sortMenu->addAction("Top comments");
    m_sortNewestAction = m_sortMenu->addAction("Newest first");
    connect(m_sortTopAction, &QAction::triggered, this, &CommentsWidget::onSortTopClicked);
    connect(m_sortNewestAction, &QAction::triggered, this, &CommentsWidget::onSortNewestClicked);
    m_sortBtn->setMenu(m_sortMenu);

    headerRow->addWidget(m_sortBtn);
    headerRow->addStretch(1);
    layout->addLayout(headerRow);

    // 2. Status / Loading / Empty Label
    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet("color: #aaaaaa; font-size: 13px; background: transparent; padding: 8px 0;");
    m_statusLabel->hide();
    layout->addWidget(m_statusLabel);

    // 3. Virtualized Comment List View
    m_listView = new CommentListView(this);
    m_model = new CommentModel(this);
    m_delegate = new CommentDelegate(this);
    m_listView->setModel(m_model);
    m_listView->setItemDelegate(m_delegate);

    connect(m_listView, &CommentListView::likeClicked, this, &CommentsWidget::onLikeClicked);
    connect(m_listView, &CommentListView::dislikeClicked, this, &CommentsWidget::onDislikeClicked);
    connect(m_listView, &CommentListView::readMoreClicked, this, &CommentsWidget::onReadMoreClicked);
    connect(m_listView, &CommentListView::toggleRepliesClicked, this, &CommentsWidget::onToggleRepliesClicked);

    connect(m_model, &QAbstractListModel::rowsInserted, this, [this]() {
        m_listView->updateContentHeight();
        emit heightChanged();
    });
    connect(m_model, &QAbstractListModel::rowsRemoved, this, [this]() {
        m_listView->updateContentHeight();
        emit heightChanged();
    });
    connect(m_model, &QAbstractListModel::modelReset, this, [this]() {
        m_listView->updateContentHeight();
        emit heightChanged();
    });

    layout->addWidget(m_listView);
}

void CommentsWidget::setYouTubeClient(YouTubeClient *client) {
    m_client = client;
}

bool CommentsWidget::hasComments() const noexcept {
    return m_model && m_model->rowCount() > 0;
}

void CommentsWidget::clear() {
    m_currentGen++;
    if (m_cancelToken) {
        m_cancelToken->store(true, std::memory_order_relaxed);
    }
    m_cancelToken = std::make_shared<std::atomic<bool>>(false);
    m_currentVideoId.clear();
    m_nextContinuationToken.clear();
    m_topSortToken.clear();
    m_newestSortToken.clear();
    m_isLoading = false;

    if (m_model) {
        m_model->clear();
    }
    if (m_headerCountLabel) {
        m_headerCountLabel->setText("Comments");
    }
    if (m_statusLabel) {
        m_statusLabel->hide();
    }
}

void CommentsWidget::loadCommentsForVideo(const std::string &videoId, const std::string &commentsToken,
                                         const std::string &commentCountText) {
    clear();
    m_currentVideoId = videoId;
    m_nextContinuationToken = commentsToken;
    m_isTopSort = true;

    if (!commentCountText.empty()) {
        m_headerCountLabel->setText(QString::fromUtf8(commentCountText.c_str()));
    } else {
        m_headerCountLabel->setText("Comments");
    }

    if (commentsToken.empty()) {
        m_statusLabel->setText("Comments are turned off.");
        m_statusLabel->show();
        return;
    }

    m_statusLabel->setText("Loading comments...");
    m_statusLabel->show();
    fetchCommentsPage(commentsToken, true);
}

void CommentsWidget::fetchCommentsPage(const std::string &token, bool isReset) {
    if (token.empty() || m_isLoading || !m_client)
        return;

    m_isLoading = true;
    uint64_t gen = m_currentGen;
    auto cancelToken = m_cancelToken;
    YouTubeClient *client = m_client;
    QPointer<CommentsWidget> self(this);

    QThreadPool::globalInstance()->start([self, client, token, isReset, gen, cancelToken]() {
        std::string err;
        CommentsResult res = client->fetchComments(token, err, cancelToken.get());

        if (cancelToken->load())
            return;

        QMetaObject::invokeMethod(
            qApp,
            [self, res = std::move(res), isReset, gen, cancelToken, err]() {
                if (!self || cancelToken->load() || gen != self->m_currentGen)
                    return;

                self->m_isLoading = false;
                if (self->m_statusLabel) {
                    self->m_statusLabel->hide();
                }

                if (!res.commentCountText.empty() && self->m_headerCountLabel) {
                    self->m_headerCountLabel->setText(QString::fromUtf8(res.commentCountText.c_str()));
                }
                if (!res.topCommentsToken.empty()) {
                    self->m_topSortToken = res.topCommentsToken;
                }
                if (!res.newestCommentsToken.empty()) {
                    self->m_newestSortToken = res.newestCommentsToken;
                }
                self->m_nextContinuationToken = res.nextContinuationToken;

                if (isReset) {
                    self->m_model->setComments(res.comments);
                } else {
                    self->m_model->appendComments(res.comments);
                }

                if (self->m_model->rowCount() == 0) {
                    self->m_statusLabel->setText(
                        QString::fromUtf8(err.empty() ? "No comments found." : err.c_str()));
                    self->m_statusLabel->show();
                }
            },
            Qt::QueuedConnection);
    });
}

void CommentsWidget::loadNextPageIfNeeded() {
    if (m_isLoading || m_nextContinuationToken.empty())
        return;
    fetchCommentsPage(m_nextContinuationToken, false);
}

void CommentsWidget::onSortTopClicked() {
    if (m_isTopSort && m_model->rowCount() > 0)
        return;
    m_isTopSort = true;
    if (!m_topSortToken.empty()) {
        m_statusLabel->setText("Loading top comments...");
        m_statusLabel->show();
        fetchCommentsPage(m_topSortToken, true);
    }
}

void CommentsWidget::onSortNewestClicked() {
    if (!m_isTopSort && m_model->rowCount() > 0)
        return;
    m_isTopSort = false;
    if (!m_newestSortToken.empty()) {
        m_statusLabel->setText("Loading newest comments...");
        m_statusLabel->show();
        fetchCommentsPage(m_newestSortToken, true);
    }
}

void CommentsWidget::onLikeClicked(int row) {
    if (!m_model)
        return;
    const auto *r = m_model->getCommentRow(row);
    if (!r)
        return;

    bool wasLiked = r->item.isLiked;
    std::string actionToken = wasLiked ? r->item.unlikeActionToken : r->item.likeActionToken;

    m_model->toggleLike(row);

    if (m_client && !actionToken.empty()) {
        auto cancelToken = m_cancelToken;
        YouTubeClient *client = m_client;
        QThreadPool::globalInstance()->start([client, actionToken, cancelToken]() {
            std::string err;
            (void)client->performCommentAction(actionToken, err, cancelToken.get());
        });
    }
}

void CommentsWidget::onDislikeClicked(int row) {
    if (!m_model)
        return;
    const auto *r = m_model->getCommentRow(row);
    if (!r)
        return;

    bool wasDisliked = r->item.isDisliked;
    std::string actionToken = wasDisliked ? r->item.undislikeActionToken : r->item.dislikeActionToken;

    m_model->toggleDislike(row);

    if (m_client && !actionToken.empty()) {
        auto cancelToken = m_cancelToken;
        YouTubeClient *client = m_client;
        QThreadPool::globalInstance()->start([client, actionToken, cancelToken]() {
            std::string err;
            (void)client->performCommentAction(actionToken, err, cancelToken.get());
        });
    }
}

void CommentsWidget::onReadMoreClicked(int row) {
    if (m_model) {
        m_model->toggleTextExpanded(row);
    }
}

void CommentsWidget::onToggleRepliesClicked(int row) {
    if (!m_model)
        return;
    const auto *r = m_model->getCommentRow(row);
    if (!r)
        return;

    if (r->repliesExpanded) {
        m_model->setRepliesExpanded(row, false);
        return;
    }

    if (r->repliesLoaded) {
        m_model->insertReplies(row, r->replies);
        return;
    }

    std::string repliesToken = r->item.repliesContinuationToken;
    if (repliesToken.empty()) {
        m_model->setRepliesExpanded(row, false);
        return;
    }

    m_model->setLoadingReplies(row, true);
    fetchRepliesForComment(row, repliesToken);
}

void CommentsWidget::fetchRepliesForComment(int row, const std::string &token) {
    if (!m_client)
        return;

    uint64_t gen = m_currentGen;
    auto cancelToken = m_cancelToken;
    YouTubeClient *client = m_client;
    QPointer<CommentsWidget> self(this);

    QThreadPool::globalInstance()->start([self, client, row, token, gen, cancelToken]() {
        std::string err;
        CommentsResult res = client->fetchComments(token, err, cancelToken.get());

        if (cancelToken->load())
            return;

        QMetaObject::invokeMethod(
            qApp,
            [self, row, res = std::move(res), gen, cancelToken]() {
                if (!self || cancelToken->load() || gen != self->m_currentGen)
                    return;

                if (self->m_model) {
                    self->m_model->insertReplies(row, res.comments);
                }
            },
            Qt::QueuedConnection);
    });
}

}  // namespace lightyt::ui
