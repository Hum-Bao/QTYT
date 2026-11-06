#pragma once

#include "LeftNavDrawer.h"

#include <QIcon>
#include <QMap>
#include <QString>
#include <QWidget>

class QFrame;
class QLabel;
class QPushButton;
class QScrollArea;
class QToolButton;
class QVBoxLayout;

namespace lightyt::ui {

class NavSidebar : public QWidget {
    Q_OBJECT

public:
    explicit NavSidebar(QWidget *parent = nullptr);
    ~NavSidebar() override;

    void addEntry(const QString &id, const QString &title, const QIcon &icon,
                  NavSectionId section = NavSectionId::Primary);
    void setActiveEntry(const QString &id);
    [[nodiscard]] QString activeEntry() const noexcept { return m_activeId; }

    void setMinimal(bool minimal);
    void toggleMinimal();
    [[nodiscard]] bool isMinimal() const noexcept { return m_isMinimal; }

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

    [[nodiscard]] QWidget *expandedWidget() const noexcept { return m_expandedWidget; }
    [[nodiscard]] QWidget *minimalWidget() const noexcept { return m_minimalWidget; }
    [[nodiscard]] QPushButton *expandedButton(const QString &id) const;
    [[nodiscard]] QToolButton *minimalButton(const QString &id) const;

signals:
    void entryClicked(const QString &id);

private:
    static constexpr int kExpandedWidth = 240;
    static constexpr int kMinimalWidth = 72;

    void setupUi();
    void updateItemStyles();

    bool m_isMinimal = false;
    QString m_activeId;

    QWidget *m_expandedWidget = nullptr;
    QScrollArea *m_expandedScrollArea = nullptr;
    QWidget *m_expandedScrollContent = nullptr;
    QVBoxLayout *m_primaryLayout = nullptr;
    QVBoxLayout *m_youLayout = nullptr;
    QVBoxLayout *m_customLayout = nullptr;
    QFrame *m_dividerPrimaryYou = nullptr;
    QWidget *m_youHeaderWidget = nullptr;

    QWidget *m_minimalWidget = nullptr;
    QScrollArea *m_minimalScrollArea = nullptr;
    QWidget *m_minimalScrollContent = nullptr;
    QVBoxLayout *m_minimalButtonsLayout = nullptr;

    QMap<QString, QPushButton *> m_expandedButtons;
    QMap<QString, QToolButton *> m_minimalButtons;
};

}  // namespace lightyt::ui

