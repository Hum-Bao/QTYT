#pragma once

#include <QFrame>
#include <QIcon>
#include <QMap>
#include <QPushButton>
#include <QScrollArea>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

class QPropertyAnimation;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;

namespace lightyt::ui {

enum class NavSectionId {
    Primary,
    You,
    Custom
};

struct NavEntry {
    QString id;
    QString title;
    QIcon icon;
    NavSectionId section = NavSectionId::Primary;
};

class LeftNavDrawer : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal slideProgress READ slideProgress WRITE setSlideProgress)

public:
    explicit LeftNavDrawer(QWidget *parent = nullptr);
    ~LeftNavDrawer() override;

    [[nodiscard]] qreal slideProgress() const noexcept { return m_slideProgress; }
    void setSlideProgress(qreal progress);

    [[nodiscard]] bool isOpen() const noexcept { return m_isOpen; }

    void addEntry(const QString &id, const QString &title, const QIcon &icon,
                  NavSectionId section = NavSectionId::Primary);
    void setActiveEntry(const QString &id);
    [[nodiscard]] QString activeEntry() const noexcept { return m_activeId; }

public slots:
    void openDrawer();
    void closeDrawer();
    void toggleDrawer();
    void syncGeometry();

signals:
    void entryClicked(const QString &id);
    void drawerOpened();
    void drawerClosed();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onAnimationFinished();

private:
    static constexpr int kDrawerWidth = 240;
    static constexpr int kHeaderHeight = 56;
    static constexpr int kAnimationDurationMs = 200;

    void setupUi();
    void updatePanelGeometry();
    void updateItemStyles();

    qreal m_slideProgress = 0.0;
    bool m_isOpen = false;
    QString m_activeId;

    QPropertyAnimation *m_animation = nullptr;
    QWidget *m_panelWidget = nullptr;
    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_scrollContent = nullptr;
    QVBoxLayout *m_primaryLayout = nullptr;
    QVBoxLayout *m_youLayout = nullptr;
    QVBoxLayout *m_customLayout = nullptr;
    QFrame *m_dividerPrimaryYou = nullptr;
    QWidget *m_youHeaderWidget = nullptr;

    QMap<QString, QPushButton *> m_entryButtons;
};

}  // namespace lightyt::ui

