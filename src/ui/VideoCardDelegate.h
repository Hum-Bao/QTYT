#pragma once

#include <QSize>
#include <QStyledItemDelegate>

class VideoCardDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit VideoCardDelegate(QObject *parent = nullptr);

    void setCardSize(const QSize &size) noexcept { m_cardSize = size; }
    [[nodiscard]] QSize cardSize() const noexcept { return m_cardSize; }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
    QSize m_cardSize{312, 268};
};
