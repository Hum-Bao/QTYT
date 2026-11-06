#include "NavSidebar.h"

#include "YouTubeIcons.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

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

QString inactiveMinimalItemStyle() {
    return QStringLiteral(
        "QToolButton {"
        "  background-color: transparent;"
        "  color: #f1f1f1;"
        "  border: none;"
        "  border-radius: 10px;"
        "  font-size: 10px;"
        "  font-weight: 400;"
        "  padding: 0px;"
        "  margin: 0px;"
        "}"
        "QToolButton:hover {"
        "  background-color: rgba(255, 255, 255, 0.1);"
        "}"
        "QToolButton:pressed {"
        "  background-color: rgba(255, 255, 255, 0.15);"
        "}");
}

QString activeMinimalItemStyle() {
    return QStringLiteral(
        "QToolButton {"
        "  background-color: #272727;"
        "  color: #ffffff;"
        "  border: none;"
        "  border-radius: 10px;"
        "  font-size: 10px;"
        "  font-weight: 500;"
        "  padding: 0px;"
        "  margin: 0px;"
        "}"
        "QToolButton:hover {"
        "  background-color: #3f3f3f;"
        "}"
        "QToolButton:pressed {"
        "  background-color: #4f4f4f;"
        "}");
}

}  // namespace

NavSidebar::NavSidebar(QWidget *parent)
    : QWidget(parent) {
    setupUi();
}

NavSidebar::~NavSidebar() = default;

void NavSidebar::setupUi() {
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName("navSidebar");
    setStyleSheet(
        "#navSidebar { background-color: #0f0f0f; border-right: 1px solid #1f1f1f; }");

    auto *rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // 1. Expanded Widget (240px)
    m_expandedWidget = new QWidget(this);
    m_expandedWidget->setObjectName("expandedNavWidget");
    m_expandedWidget->setFixedWidth(kExpandedWidth);
    m_expandedWidget->setStyleSheet("#expandedNavWidget { background-color: #0f0f0f; border: none; }");
    auto *expandedLayout = new QVBoxLayout(m_expandedWidget);
    expandedLayout->setContentsMargins(0, 0, 0, 0);
    expandedLayout->setSpacing(0);

    m_expandedScrollArea = new QScrollArea(m_expandedWidget);
    m_expandedScrollArea->setWidgetResizable(true);
    m_expandedScrollArea->setFrameShape(QFrame::NoFrame);
    m_expandedScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_expandedScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_expandedScrollArea->setStyleSheet(
        "QScrollArea { background-color: #0f0f0f; border: none; }"
        "QScrollBar:vertical { background: transparent; width: 8px; margin: 0; }"
        "QScrollBar::handle:vertical { background: #717171; min-height: 24px; border-radius: 4px; }"
        "QScrollBar::handle:vertical:hover { background: #aaaaaa; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }");

    m_expandedScrollContent = new QWidget(m_expandedScrollArea);
    m_expandedScrollContent->setStyleSheet("background-color: #0f0f0f; border: none;");
    auto *contentLayout = new QVBoxLayout(m_expandedScrollContent);
    contentLayout->setContentsMargins(0, 8, 0, 16);
    contentLayout->setSpacing(0);

    // Section 1: Primary items (Home, Subscriptions)
    m_primaryLayout = new QVBoxLayout();
    m_primaryLayout->setContentsMargins(0, 0, 0, 0);
    m_primaryLayout->setSpacing(2);
    contentLayout->addLayout(m_primaryLayout);

    // Divider between Primary and You
    m_dividerPrimaryYou = new QFrame(m_expandedScrollContent);
    m_dividerPrimaryYou->setFrameShape(QFrame::HLine);
    m_dividerPrimaryYou->setFixedHeight(1);
    m_dividerPrimaryYou->setStyleSheet(
        "background-color: #303030; border: none; margin: 12px 0px;");
    contentLayout->addWidget(m_dividerPrimaryYou);

    // Section 2: "You >" Section Header
    m_youHeaderWidget = new QWidget(m_expandedScrollContent);
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

    m_expandedScrollArea->setWidget(m_expandedScrollContent);
    expandedLayout->addWidget(m_expandedScrollArea, 1);
    rootLayout->addWidget(m_expandedWidget);

    // 2. Minimal Widget (72px)
    m_minimalWidget = new QWidget(this);
    m_minimalWidget->setObjectName("minimalNavWidget");
    m_minimalWidget->setFixedWidth(kMinimalWidth);
    m_minimalWidget->setStyleSheet("#minimalNavWidget { background-color: #0f0f0f; border: none; }");
    auto *minimalLayout = new QVBoxLayout(m_minimalWidget);
    minimalLayout->setContentsMargins(0, 0, 0, 0);
    minimalLayout->setSpacing(0);

    m_minimalScrollArea = new QScrollArea(m_minimalWidget);
    m_minimalScrollArea->setWidgetResizable(true);
    m_minimalScrollArea->setFrameShape(QFrame::NoFrame);
    m_minimalScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_minimalScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_minimalScrollArea->setStyleSheet("QScrollArea { background-color: #0f0f0f; border: none; }");

    m_minimalScrollContent = new QWidget(m_minimalScrollArea);
    m_minimalScrollContent->setStyleSheet("background-color: #0f0f0f; border: none;");
    auto *minimalContentLayout = new QVBoxLayout(m_minimalScrollContent);
    minimalContentLayout->setContentsMargins(4, 4, 4, 4);
    minimalContentLayout->setSpacing(0);

    m_minimalButtonsLayout = new QVBoxLayout();
    m_minimalButtonsLayout->setContentsMargins(0, 0, 0, 0);
    m_minimalButtonsLayout->setSpacing(4);
    minimalContentLayout->addLayout(m_minimalButtonsLayout);
    minimalContentLayout->addStretch(1);

    m_minimalScrollArea->setWidget(m_minimalScrollContent);
    minimalLayout->addWidget(m_minimalScrollArea, 1);
    rootLayout->addWidget(m_minimalWidget);

    // Initialize in Expanded mode (240px)
    setMinimal(false);
}

void NavSidebar::addEntry(const QString &id, const QString &title, const QIcon &icon,
                          NavSectionId section) {
    if (m_expandedButtons.contains(id) || m_minimalButtons.contains(id)) {
        return;
    }

    // 1. Expanded button (40px high, icon on left, title on right)
    auto *btn = new QPushButton(m_expandedScrollContent);
    btn->setText(title);
    btn->setIcon(icon);
    btn->setIconSize(QSize(24, 24));
    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(inactiveItemStyle());

    connect(btn, &QPushButton::clicked, this, [this, id]() {
        setActiveEntry(id);
        emit entryClicked(id);
    });

    m_expandedButtons.insert(id, btn);

    if (section == NavSectionId::Primary) {
        m_primaryLayout->addWidget(btn);
    } else if (section == NavSectionId::You) {
        m_youLayout->addWidget(btn);
    } else {
        m_customLayout->addWidget(btn);
    }

    // 2. Minimal button (Rail: 64x72 with 24x24 icon and 10px text)
    auto *toolBtn = new QToolButton(m_minimalScrollContent);
    toolBtn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    toolBtn->setFixedSize(64, 72);
    toolBtn->setIcon(icon);
    toolBtn->setIconSize(QSize(24, 24));
    toolBtn->setCursor(Qt::PointingHandCursor);
    toolBtn->setToolTip(title);

    QString minimalTitle = title;
    if (id == "liked_videos" && title == "Liked videos") {
        minimalTitle = QStringLiteral("Liked");
    }
    toolBtn->setText(minimalTitle);
    toolBtn->setStyleSheet(inactiveMinimalItemStyle());

    connect(toolBtn, &QToolButton::clicked, this, [this, id]() {
        setActiveEntry(id);
        emit entryClicked(id);
    });

    m_minimalButtons.insert(id, toolBtn);
    m_minimalButtonsLayout->addWidget(toolBtn, 0, Qt::AlignHCenter);

    if (id == m_activeId) {
        btn->setStyleSheet(activeItemStyle());
        toolBtn->setStyleSheet(activeMinimalItemStyle());
    }
}

void NavSidebar::setActiveEntry(const QString &id) {
    m_activeId = id;
    updateItemStyles();
}

void NavSidebar::updateItemStyles() {
    for (auto it = m_expandedButtons.begin(); it != m_expandedButtons.end(); ++it) {
        bool isActive = (it.key() == m_activeId);
        it.value()->setStyleSheet(isActive ? activeItemStyle() : inactiveItemStyle());
    }
    for (auto it = m_minimalButtons.begin(); it != m_minimalButtons.end(); ++it) {
        bool isActive = (it.key() == m_activeId);
        it.value()->setStyleSheet(isActive ? activeMinimalItemStyle() : inactiveMinimalItemStyle());
    }
}

void NavSidebar::setMinimal(bool minimal) {
    m_isMinimal = minimal;
    if (m_isMinimal) {
        m_expandedWidget->hide();
        m_minimalWidget->show();
        setFixedWidth(kMinimalWidth);
    } else {
        m_minimalWidget->hide();
        m_expandedWidget->show();
        setFixedWidth(kExpandedWidth);
    }
    updateGeometry();
}

void NavSidebar::toggleMinimal() {
    setMinimal(!m_isMinimal);
}

QSize NavSidebar::sizeHint() const {
    return QSize(m_isMinimal ? kMinimalWidth : kExpandedWidth, 0);
}

QSize NavSidebar::minimumSizeHint() const {
    return QSize(m_isMinimal ? kMinimalWidth : kExpandedWidth, 0);
}

QPushButton *NavSidebar::expandedButton(const QString &id) const {
    return m_expandedButtons.value(id, nullptr);
}

QToolButton *NavSidebar::minimalButton(const QString &id) const {
    return m_minimalButtons.value(id, nullptr);
}

}  // namespace lightyt::ui

