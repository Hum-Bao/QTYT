#pragma once

#include <QSize>
#include <QStyledItemDelegate>

class VideoCardDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    enum class CardLayoutMode {
        Grid,
        CompactHorizontal
    };

    explicit VideoCardDelegate(QObject *parent = nullptr);

    void setLayoutMode(CardLayoutMode mode) noexcept { m_mode = mode; }
    [[nodiscard]] CardLayoutMode layoutMode() const noexcept { return m_mode; }

    void setCardSize(const QSize &size) noexcept { m_cardSize = size; }
    [[nodiscard]] QSize cardSize() const noexcept { return m_cardSize; }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
    CardLayoutMode m_mode{CardLayoutMode::Grid};
    QSize m_cardSize{312, 268};
};

using CardLayoutMode = VideoCardDelegate::CardLayoutMode;
