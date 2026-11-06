#include "LeftNavDrawer.h"

#include "YouTubeIcons.h"

#include <QEasingCurve>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPropertyAnimation>
#include <algorithm>

namespace lightyt::ui {

namespace {

QString inactiveItemStyle() {
    return QStringLiteral(
        "QPushButton {"
        "  background-color: transparent;"
        "  color: #f1f1f1;"
        "  border: none;"
        "  border-radius: 10px;"
        "  text-align: left;"
        "  padding-left: 12px;"
        "  margin: 0px 12px;"
        "  font-size: 14px;"
        "  font-weight: 400;"
        "  min-height: 40px;"
        "  max-height: 40px;"
        "}"
        "QPushButton:hover {"
        "  background-color: rgba(255, 255, 255, 0.08);"
        "}"
        "QPushButton:pressed {"
        "  background-color: rgba(255, 255, 255, 0.15);"
        "}");
}

QString activeItemStyle() {
    return QStringLiteral(
        "QPushButton {"
        "  background-color: #272727;"
        "  color: #ffffff;"
        "  border: none;"
        "  border-radius: 10px;"
        "  text-align: left;"
        "  padding-left: 12px;"
        "  margin: 0px 12px;"
        "  font-size: 14px;"
        "  font-weight: 600;"
        "  min-height: 40px;"
        "  max-height: 40px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #3f3f3f;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #4f4f4f;"
        "}");
}

}  // namespace

LeftNavDrawer::LeftNavDrawer(QWidget *parent)
    : QWidget(parent), m_animation(new QPropertyAnimation(this, "slideProgress", this)) {
    setupUi();

    m_animation->setDuration(kAnimationDurationMs);
    m_animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_animation, &QPropertyAnimation::finished, this, &LeftNavDrawer::onAnimationFinished);

    if (parent) {
        parent->installEventFilter(this);
        syncGeometry();
    }

    hide();
    setSlideProgress(0.0);
}

LeftNavDrawer::~LeftNavDrawer() = default;

void LeftNavDrawer::setupUi() {
    setAttribute(Qt::WA_NoSystemBackground, true);
    setFocusPolicy(Qt::StrongFocus);

    // Left sliding panel
    m_panelWidget = new QWidget(this);
    m_panelWidget->setObjectName("drawerPanel");
    m_panelWidget->setAttribute(Qt::WA_StyledBackground, true);
    m_panelWidget->setFixedWidth(kDrawerWidth);
    m_panelWidget->setStyleSheet(
        "#drawerPanel { background-color: #0f0f0f; border-right: 1px solid #1f1f1f; }");

    auto *panelLayout = new QVBoxLayout(m_panelWidget);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->setSpacing(0);

    // 1. Header (56px height, aligns with MainWindow masthead)
    auto *headerWidget = new QWidget(m_panelWidget);
    headerWidget->setFixedHeight(kHeaderHeight);
    headerWidget->setStyleSheet("background-color: #0f0f0f; border: none;");
    auto *headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(16, 0, 16, 0);
    headerLayout->setSpacing(10);

    auto *closeBtn = new QPushButton(headerWidget);
    closeBtn->setFixedSize(40, 40);
    closeBtn->setIcon(icons::menu(QColor(0xf1, 0xf1, 0xf1), 20));
    closeBtn->setIconSize(QSize(20, 20));
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setToolTip("Guide");
    closeBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: transparent; border: none; border-radius: 20px; padding: 0px;"
        "}"
        "QPushButton:hover { background-color: rgba(255, 255, 255, 0.1); }"
        "QPushButton:pressed { background-color: rgba(255, 255, 255, 0.2); }");
    connect(closeBtn, &QPushButton::clicked, this, &LeftNavDrawer::closeDrawer);
    headerLayout->addWidget(closeBtn);

    auto *brandBtn = new QPushButton("QTYT", headerWidget);
    brandBtn->setFixedHeight(36);
    brandBtn->setCursor(Qt::PointingHandCursor);
    brandBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: transparent; color: #f1f1f1; border: none; border-radius: 8px;"
        "  padding: 0px 12px; min-height: 36px; max-height: 36px; font-size: 18px; font-weight: 700;"
        "}"
        "QPushButton:hover { background-color: rgba(255, 255, 255, 0.08); }");
    connect(brandBtn, &QPushButton::clicked, this, [this]() {
        closeDrawer();
        emit entryClicked("home");
    });
    headerLayout->addWidget(brandBtn);
    headerLayout->addStretch(1);

    panelLayout->addWidget(headerWidget);

    // 2. Scrollable Section Container
    m_scrollArea = new QScrollArea(m_panelWidget);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setStyleSheet(
        "QScrollArea { background-color: #0f0f0f; border: none; }"
        "QScrollBar:vertical { background: transparent; width: 8px; margin: 0; }"
        "QScrollBar::handle:vertical { background: #717171; min-height: 24px; border-radius: 4px; }"
        "QScrollBar::handle:vertical:hover { background: #aaaaaa; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }");

    m_scrollContent = new QWidget(m_scrollArea);
    m_scrollContent->setStyleSheet("background-color: #0f0f0f;");
    auto *contentLayout = new QVBoxLayout(m_scrollContent);
    contentLayout->setContentsMargins(0, 8, 0, 16);
    contentLayout->setSpacing(0);

    // Section 1: Primary items (Home, Subscriptions)
    m_primaryLayout = new QVBoxLayout();
    m_primaryLayout->setContentsMargins(0, 0, 0, 0);
    m_primaryLayout->setSpacing(2);
    contentLayout->addLayout(m_primaryLayout);

    // Divider between Primary and You
    m_dividerPrimaryYou = new QFrame(m_scrollContent);
    m_dividerPrimaryYou->setFrameShape(QFrame::HLine);
    m_dividerPrimaryYou->setFixedHeight(1);
    m_dividerPrimaryYou->setStyleSheet(
        "background-color: #303030; border: none; margin: 12px 0px;");
    contentLayout->addWidget(m_dividerPrimaryYou);

    // Section 2: "You >" Section Header
    m_youHeaderWidget = new QWidget(m_scrollContent);
    m_youHeaderWidget->setFixedHeight(36);
    auto *youHeaderLayout = new QHBoxLayout(m_youHeaderWidget);
    youHeaderLayout->setContentsMargins(24, 0, 16, 0);
    youHeaderLayout->setSpacing(6);

    auto *youLabel = new QLabel("You", m_youHeaderWidget);
    youLabel->setStyleSheet("color: #f1f1f1; font-size: 16px; font-weight: 600; border: none;");
    youHeaderLayout->addWidget(youLabel);

    auto *chevronLabel = new QLabel(m_youHeaderWidget);
    chevronLabel->setPixmap(icons::chevronRight(QColor(0xf1, 0xf1, 0xf1), 14).pixmap(14, 14));
    chevronLabel->setStyleSheet("border: none;");
    youHeaderLayout->addWidget(chevronLabel);
    youHeaderLayout->addStretch(1);

    contentLayout->addWidget(m_youHeaderWidget);

    // Section 2 Items: You items (History, Playlists, Watch later, Liked videos)
    m_youLayout = new QVBoxLayout();
    m_youLayout->setContentsMargins(0, 4, 0, 0);
    m_youLayout->setSpacing(2);
    contentLayout->addLayout(m_youLayout);

    // Section 3: Extensible Custom Layout
    m_customLayout = new QVBoxLayout();
    m_customLayout->setContentsMargins(0, 8, 0, 0);
    m_customLayout->setSpacing(2);
    contentLayout->addLayout(m_customLayout);

    contentLayout->addStretch(1);

    m_scrollArea->setWidget(m_scrollContent);
    panelLayout->addWidget(m_scrollArea, 1);
}

void LeftNavDrawer::setSlideProgress(qreal progress) {
    m_slideProgress = std::clamp(progress, 0.0, 1.0);
    updatePanelGeometry();
    update();
}

void LeftNavDrawer::updatePanelGeometry() {
    if (!m_panelWidget)
        return;
    int targetX = static_cast<int>((m_slideProgress - 1.0) * kDrawerWidth);
    m_panelWidget->setGeometry(targetX, 0, kDrawerWidth, height());
}

void LeftNavDrawer::syncGeometry() {
    if (parentWidget()) {
        setGeometry(parentWidget()->rect());
        updatePanelGeometry();
    }
}

void LeftNavDrawer::openDrawer() {
    syncGeometry();
    show();
    raise();
    setFocus();
    m_isOpen = true;

    m_animation->stop();
    m_animation->setStartValue(m_slideProgress);
    m_animation->setEndValue(1.0);
    m_animation->start();

    emit drawerOpened();
}

void LeftNavDrawer::closeDrawer() {
    m_isOpen = false;

    m_animation->stop();
    m_animation->setStartValue(m_slideProgress);
    m_animation->setEndValue(0.0);
    m_animation->start();
}

void LeftNavDrawer::toggleDrawer() {
    if (m_isOpen) {
        closeDrawer();
    } else {
        openDrawer();
    }
}

void LeftNavDrawer::onAnimationFinished() {
    if (m_slideProgress <= 0.0) {
        hide();
        emit drawerClosed();
    }
}

void LeftNavDrawer::addEntry(const QString &id, const QString &title, const QIcon &icon,
                             NavSectionId section) {
    if (m_entryButtons.contains(id)) {
        return;
    }

    auto *btn = new QPushButton(m_scrollContent);
    btn->setText(title);
    btn->setIcon(icon);
    btn->setIconSize(QSize(24, 24));
    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(inactiveItemStyle());

    connect(btn, &QPushButton::clicked, this, [this, id]() {
        setActiveEntry(id);
        closeDrawer();
        emit entryClicked(id);
    });

    m_entryButtons.insert(id, btn);

    if (section == NavSectionId::Primary) {
        m_primaryLayout->addWidget(btn);
    } else if (section == NavSectionId::You) {
        m_youLayout->addWidget(btn);
    } else {
        m_customLayout->addWidget(btn);
    }
}

void LeftNavDrawer::setActiveEntry(const QString &id) {
    m_activeId = id;
    updateItemStyles();
}

void LeftNavDrawer::updateItemStyles() {
    for (auto it = m_entryButtons.begin(); it != m_entryButtons.end(); ++it) {
        bool isActive = (it.key() == m_activeId);
        it.value()->setStyleSheet(isActive ? activeItemStyle() : inactiveItemStyle());
    }
}

void LeftNavDrawer::paintEvent(QPaintEvent * /*event*/) {
    if (m_slideProgress <= 0.0)
        return;

    QPainter painter(this);
    int alpha = static_cast<int>(m_slideProgress * 128);  // Max ~50% black scrim
    painter.fillRect(rect(), QColor(0, 0, 0, alpha));
}

void LeftNavDrawer::mousePressEvent(QMouseEvent *event) {
    if (!m_panelWidget) {
        QWidget::mousePressEvent(event);
        return;
    }

    // If clicked outside panel widget (on the dimmed backdrop), dismiss the drawer
    if (!m_panelWidget->geometry().contains(event->pos())) {
        closeDrawer();
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void LeftNavDrawer::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        closeDrawer();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool LeftNavDrawer::eventFilter(QObject *watched, QEvent *event) {
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        syncGeometry();
    }
    return QWidget::eventFilter(watched, event);
}

}  // namespace lightyt::ui

