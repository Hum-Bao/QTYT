#include "VideoCardDelegate.h"

#include "FeedModel.h"
#include "Theme.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QTextLayout>
#include <unordered_map>

namespace {

using namespace lightyt::ui::theme;

const QColor s_cardBgHover = ColorSurface;
const QColor s_thumbBg(0x18, 0x18, 0x18);
const QColor s_shortsBadge = ColorShortsBadge;
const QColor s_durationBadge = ColorBadgeDuration;
const QColor s_titleColor = ColorTextPrimary;
const QColor s_authorColor = ColorTextSecondary;
const QColor s_metaColor = ColorTextSecondary;

struct TitleLayoutData {
    QString line1;
    QString line2;
    int line1Height = 0;
    int ascent = 0;
};

struct TitleCacheKey {
    QString title;
    int width = 0;
    int pointSize = 0;

    bool operator==(const TitleCacheKey &other) const noexcept {
        return width == other.width && pointSize == other.pointSize && title == other.title;
    }
};

struct TitleCacheKeyHash {
    size_t operator()(const TitleCacheKey &k) const noexcept {
        return qHash(k.title) ^ (static_cast<size_t>(k.width) << 16) ^ (static_cast<size_t>(k.pointSize) << 8);
    }
};

// Accessed exclusively on the Qt GUI thread
std::unordered_map<TitleCacheKey, TitleLayoutData, TitleCacheKeyHash> s_titleCache;

TitleLayoutData getOrComputeTitleLayout(const QString &title, const QFont &font, int width) {
    int ptSize = font.pointSizeF() > 0 ? static_cast<int>(font.pointSizeF() * 10.0) : font.pixelSize();
    TitleCacheKey key{title, width, ptSize};
    auto it = s_titleCache.find(key);
    if (it != s_titleCache.end()) {
        return it->second;
    }

    TitleLayoutData data;
    QFontMetrics tfm(font);
    data.ascent = tfm.ascent();

    QTextLayout layout(title, font);
    layout.beginLayout();
    QTextLine line1 = layout.createLine();
    if (!line1.isValid()) {
        layout.endLayout();
        return data;
    }
    line1.setLineWidth(width);
    data.line1Height = static_cast<int>(line1.height());
    int firstLen = line1.textLength();
    data.line1 = title.left(firstLen);

    QTextLine line2 = layout.createLine();
    if (line2.isValid()) {
        QString rem = title.mid(firstLen).trimmed();
        data.line2 = tfm.elidedText(rem, Qt::ElideRight, width);
    }
    layout.endLayout();

    if (s_titleCache.size() >= 1024) {
        s_titleCache.erase(s_titleCache.begin());
    }
    s_titleCache[key] = data;
    return data;
}

}  // namespace

VideoCardDelegate::VideoCardDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

QSize VideoCardDelegate::sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const {
    if (m_mode == CardLayoutMode::CompactHorizontal) {
        return QSize(m_cardSize.width() > 0 ? m_cardSize.width() : 402, 102);
    }
    return m_cardSize;
}

void VideoCardDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    if (!index.isValid())
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);

    bool isHovered = (option.state & QStyle::State_MouseOver);

    if (m_mode == CardLayoutMode::CompactHorizontal) {
        // --- COMPACT HORIZONTAL MODE (Recommendations Sidebar) ---
        QRect hoverRect = option.rect.adjusted(2, 2, -2, -2);
        QRect contentRect = hoverRect.adjusted(2, 2, -2, -2);

        // 1. Subtle card hover background with 8px rounded corners
        if (isHovered) {
            QPainterPath bgPath;
            bgPath.addRoundedRect(hoverRect, 8, 8);
            painter->fillPath(bgPath, s_cardBgHover);
        }

        // 2. Thumbnail Area: Fixed 168x94 on left with 8px rounded corners
        constexpr int kThumbW = 168;
        constexpr int kThumbH = 94;
        int thumbY = contentRect.top() + (contentRect.height() - kThumbH) / 2;
        QRect thumbRect(contentRect.left(), thumbY, kThumbW, kThumbH);

        QPainterPath thumbClip;
        thumbClip.addRoundedRect(thumbRect, 8, 8);
        painter->save();
        painter->setClipPath(thumbClip);

        if (QVariant v = index.data(ThumbnailPixmapRole); v.isValid()) {
            QPixmap pix = v.value<QPixmap>();
            if (!pix.isNull()) {
                painter->drawPixmap(thumbRect, pix);
            } else {
                painter->fillRect(thumbRect, s_thumbBg);
            }
        } else {
            painter->fillRect(thumbRect, s_thumbBg);
        }
        painter->restore();

        // 3. Duration or Shorts badge (bottom-right of thumbnail)
        QString duration = index.data(DurationRole).toString();
        bool isShort = index.data(IsShortRole).toBool();

        if (isShort) {
            QFont badgeFont = painter->font();
            badgeFont.setPointSize(8);
            badgeFont.setBold(true);
            painter->setFont(badgeFont);

            QFontMetrics fm(badgeFont);
            QString badgeText = "SHORTS";
            int bw = fm.horizontalAdvance(badgeText) + 8;
            int bh = fm.height() + 2;
            QRect badgeRect(thumbRect.right() - bw - 4, thumbRect.bottom() - bh - 4, bw, bh);

            QPainterPath badgePath;
            badgePath.addRoundedRect(badgeRect, RadiusBadge, RadiusBadge);
            painter->fillPath(badgePath, s_shortsBadge);

            painter->setPen(Qt::white);
            painter->drawText(badgeRect, Qt::AlignCenter, badgeText);
        } else if (!duration.isEmpty()) {
            QFont badgeFont = painter->font();
            badgeFont.setPointSize(8);
            badgeFont.setBold(true);
            painter->setFont(badgeFont);

            QFontMetrics fm(badgeFont);
            int bw = fm.horizontalAdvance(duration) + 8;
            int bh = fm.height() + 2;
            QRect badgeRect(thumbRect.right() - bw - 4, thumbRect.bottom() - bh - 4, bw, bh);

            QPainterPath badgePath;
            badgePath.addRoundedRect(badgeRect, RadiusBadge, RadiusBadge);
            painter->fillPath(badgePath, s_durationBadge);

            painter->setPen(Qt::white);
            painter->drawText(badgeRect, Qt::AlignCenter, duration);
        }

        // 4. Metadata on Right (starting at thumbRect.right() + 8)
        int textLeft = thumbRect.right() + 8;
        int textWidth = std::max(40, contentRect.right() - textLeft);

        // 4a. Video Title: 2 lines max, bold #f1f1f1 (9.5pt)
        QFont titleFont = painter->font();
        titleFont.setPointSizeF(9.5);
        titleFont.setBold(true);
        painter->setFont(titleFont);
        painter->setPen(s_titleColor);

        TitleLayoutData layoutData =
            getOrComputeTitleLayout(textWidth > 0 ? index.data(TitleRole).toString() : "", titleFont, textWidth);
        int textTop = thumbRect.top();
        painter->drawText(QPointF(textLeft, textTop + layoutData.ascent), layoutData.line1);

        int titleBottom = textTop + std::max(16, layoutData.line1Height);
        if (!layoutData.line2.isEmpty()) {
            painter->drawText(QPointF(textLeft, textTop + layoutData.line1Height + layoutData.ascent), layoutData.line2);
            titleBottom = textTop + std::max(32, layoutData.line1Height * 2);
        }

        // 4b. Channel Name: 8pt regular #aaaaaa
        QString author = index.data(AuthorRole).toString();
        QRect authorRect(textLeft, titleBottom + 3, textWidth, 15);

        QFont subFont = painter->font();
        subFont.setPointSize(8);
        subFont.setBold(false);
        painter->setFont(subFont);
        painter->setPen(s_authorColor);
        QFontMetrics afm(subFont);
        painter->drawText(authorRect, Qt::AlignVCenter | Qt::AlignLeft,
                          afm.elidedText(author, Qt::ElideRight, authorRect.width()));

        // 4c. Views and Upload Date: 8pt regular #aaaaaa
        QString views = index.data(ViewCountRole).toString();
        QString date = index.data(PublishedTimeRole).toString();
        QString meta = views;
        if (!date.isEmpty()) {
            if (!meta.isEmpty())
                meta += " • ";
            meta += date;
        }

        QRect metaRect(textLeft, authorRect.bottom() + 1, textWidth, 15);
        painter->setPen(s_metaColor);
        painter->drawText(metaRect, Qt::AlignVCenter | Qt::AlignLeft,
                          afm.elidedText(meta, Qt::ElideRight, metaRect.width()));

        painter->restore();
        return;
    }

    // --- STANDARD GRID MODE ---
    QRect hoverRect = option.rect.adjusted(4, 4, -4, -4);
    QRect contentRect = hoverRect.adjusted(4, 4, -4, -4);

    // 1. Draw Subtle Card Background on Hover Only (Seamless YouTube Look)
    if (isHovered) {
        QPainterPath bgPath;
        bgPath.addRoundedRect(hoverRect, RadiusCard, RadiusCard);
        painter->fillPath(bgPath, s_cardBgHover);
    }

    // 2. Thumbnail Area (Responsive 16:9 across full card width with 12px rounded corners)
    int thumbW = std::max(160, contentRect.width());
    int thumbH = (thumbW * 9) / 16;
    QRect thumbRect(contentRect.left(), contentRect.top(), thumbW, thumbH);

    QPainterPath thumbClip;
    thumbClip.addRoundedRect(thumbRect, RadiusThumbnail, RadiusThumbnail);
    painter->save();
    painter->setClipPath(thumbClip);

    if (QVariant v = index.data(ThumbnailPixmapRole); v.isValid()) {
        QPixmap pix = v.value<QPixmap>();
        if (!pix.isNull()) {
            painter->drawPixmap(thumbRect, pix);
        } else {
            painter->fillRect(thumbRect, s_thumbBg);
        }
    } else {
        painter->fillRect(thumbRect, s_thumbBg);
    }
    painter->restore();

    // 3. Duration or Shorts badge (bottom-right of thumbnail)
    QString duration = index.data(DurationRole).toString();
    bool isShort = index.data(IsShortRole).toBool();

    if (isShort) {
        QFont badgeFont = painter->font();
        badgeFont.setPointSize(8);
        badgeFont.setBold(true);
        painter->setFont(badgeFont);

        QFontMetrics fm(badgeFont);
        QString badgeText = "SHORTS";
        int bw = fm.horizontalAdvance(badgeText) + 10;
        int bh = fm.height() + 4;
        QRect badgeRect(thumbRect.right() - bw - 6, thumbRect.bottom() - bh - 6, bw, bh);

        QPainterPath badgePath;
        badgePath.addRoundedRect(badgeRect, RadiusBadge, RadiusBadge);
        painter->fillPath(badgePath, s_shortsBadge);

        painter->setPen(Qt::white);
        painter->drawText(badgeRect, Qt::AlignCenter, badgeText);
    } else if (!duration.isEmpty()) {
        QFont badgeFont = painter->font();
        badgeFont.setPointSize(9);
        badgeFont.setBold(true);
        painter->setFont(badgeFont);

        QFontMetrics fm(badgeFont);
        int bw = fm.horizontalAdvance(duration) + 10;
        int bh = fm.height() + 4;
        QRect badgeRect(thumbRect.right() - bw - 6, thumbRect.bottom() - bh - 6, bw, bh);

        QPainterPath badgePath;
        badgePath.addRoundedRect(badgeRect, RadiusBadge, RadiusBadge);
        painter->fillPath(badgePath, s_durationBadge);

        painter->setPen(Qt::white);
        painter->drawText(badgeRect, Qt::AlignCenter, duration);
    }

    // 4. Channel Avatar (36x36 circular avatar to the left of metadata)
    int metaTop = thumbRect.bottom() + 10;
    int avatarX = contentRect.left();
    QRect avatarRect(avatarX, metaTop, AvatarFeedSize, AvatarFeedSize);

    bool hasAvatar = false;
    if (QVariant avVar = index.data(ChannelAvatarPixmapRole); avVar.isValid()) {
        QPixmap avPix = avVar.value<QPixmap>();
        if (!avPix.isNull()) {
            painter->drawPixmap(avatarRect, avPix);
            hasAvatar = true;
        }
    }

    if (!hasAvatar) {
        // Fallback: styled circular avatar with author initial
        QPainterPath avClip;
        avClip.addEllipse(avatarRect);
        painter->fillPath(avClip, ColorElevated);
        painter->setPen(QPen(ColorBorder, 1));
        painter->drawPath(avClip);

        QString author = index.data(AuthorRole).toString();
        QString initial = author.isEmpty() ? "?" : author.left(1).toUpper();
        QFont avFont = painter->font();
        avFont.setPointSize(11);
        avFont.setBold(true);
        painter->setFont(avFont);
        painter->setPen(ColorTextSecondary);
        painter->drawText(avatarRect, Qt::AlignCenter, initial);
    }

    // 5. Video Title (2 lines max, bold #f1f1f1, elided with cached layout)
    int textLeft = avatarRect.right() + 12;
    int textWidth = std::max(40, contentRect.right() - textLeft);

    QFont titleFont = painter->font();
    titleFont.setPointSize(10);
    titleFont.setBold(true);
    painter->setFont(titleFont);
    painter->setPen(s_titleColor);

    TitleLayoutData layoutData =
        getOrComputeTitleLayout(textWidth > 0 ? index.data(TitleRole).toString() : "", titleFont, textWidth);
    painter->drawText(QPointF(textLeft, metaTop + layoutData.ascent), layoutData.line1);

    int titleBottom = metaTop + std::max(18, layoutData.line1Height);
    if (!layoutData.line2.isEmpty()) {
        painter->drawText(QPointF(textLeft, metaTop + layoutData.line1Height + layoutData.ascent), layoutData.line2);
        titleBottom = metaTop + std::max(36, layoutData.line1Height * 2);
    }

    // 6. Channel Name (positioned directly below actual rendered title lines)
    QString author = index.data(AuthorRole).toString();
    QRect authorRect(textLeft, titleBottom + 4, textWidth, 17);

    QFont subFont = painter->font();
    subFont.setPointSize(9);
    subFont.setBold(false);
    painter->setFont(subFont);
    painter->setPen(s_authorColor);
    QFontMetrics afm(subFont);
    painter->drawText(authorRect, Qt::AlignVCenter | Qt::AlignLeft,
                      afm.elidedText(author, Qt::ElideRight, authorRect.width()));

    // 7. Views and Upload Date
    QString views = index.data(ViewCountRole).toString();
    QString date = index.data(PublishedTimeRole).toString();
    QString meta = views;
    if (!date.isEmpty()) {
        if (!meta.isEmpty())
            meta += " • ";
        meta += date;
    }

    QRect metaRect(textLeft, authorRect.bottom() + 1, textWidth, 17);
    painter->setPen(s_metaColor);
    painter->drawText(metaRect, Qt::AlignVCenter | Qt::AlignLeft,
                      afm.elidedText(meta, Qt::ElideRight, metaRect.width()));

    painter->restore();
}
