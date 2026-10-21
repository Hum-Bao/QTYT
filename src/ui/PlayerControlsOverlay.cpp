#include "PlayerControlsOverlay.h"

#include "YouTubeIcons.h"

#include <QEnterEvent>
#include <QFontMetrics>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QStackedWidget>
#include <QStyle>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace {

class DynamicStackedWidget : public QStackedWidget {
public:
    using QStackedWidget::QStackedWidget;

    [[nodiscard]] QSize sizeHint() const override {
        if (currentWidget()) {
            return currentWidget()->sizeHint();
        }
        return QStackedWidget::sizeHint();
    }

    [[nodiscard]] QSize minimumSizeHint() const override {
        if (currentWidget()) {
            return currentWidget()->minimumSizeHint();
        }
        return QStackedWidget::minimumSizeHint();
    }
};

const char *kIconBtnQss = "QPushButton {"
                          "  background-color: transparent;"
                          "  border: none;"
                          "  border-radius: 18px;"
                          "  outline: none;"
                          "  padding: 0px;"
                          "  margin: 0px;"
                          "}"
                          "QPushButton:hover {"
                          "  background-color: rgba(255, 255, 255, 0.15);"
                          "}"
                          "QPushButton:pressed {"
                          "  background-color: rgba(255, 255, 255, 0.25);"
                          "}"
                          "QPushButton:focus { outline: none; }";

const char *kVolumeSliderQss =
    "QSlider { min-height: 20px; max-height: 20px; background: transparent; }"
    "QSlider::groove:horizontal { height: 3px; background: rgba(255, 255, 255, 0.3); border-radius: 1px; }"
    "QSlider::sub-page:horizontal { height: 3px; background: #ffffff; border-radius: 1px; }"
    "QSlider::add-page:horizontal { height: 3px; background: rgba(255, 255, 255, 0.3); border-radius: 1px; }"
    "QSlider::handle:horizontal { background: #ffffff; width: 12px; height: 12px; margin: -5px 0px; border-radius: "
    "6px; }"
    "QSlider::handle:horizontal:hover { width: 14px; height: 14px; margin: -6px 0px; border-radius: 7px; }";

}  // namespace

// ============================================================================
// PlayerScrubber Implementation
// ============================================================================

PlayerScrubber::PlayerScrubber(QWidget *parent) : QWidget(parent) {
    setFixedHeight(38);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void PlayerScrubber::setDuration(double seconds) {
    m_duration = std::max(0.0, seconds);
    update();
}

void PlayerScrubber::setPosition(double seconds) {
    if (!m_isDragging) {
        m_position = std::clamp(seconds, 0.0, m_duration > 0.0 ? m_duration : seconds);
        update();
    }
}

void PlayerScrubber::setBufferedPosition(double seconds) {
    m_bufferedPosition = std::clamp(seconds, 0.0, m_duration > 0.0 ? m_duration : seconds);
    update();
}

double PlayerScrubber::secondsFromX(int x) const noexcept {
    if (width() <= 0 || m_duration <= 0.0)
        return 0.0;
    double ratio = std::clamp(static_cast<double>(x) / static_cast<double>(width()), 0.0, 1.0);
    return ratio * m_duration;
}

QString PlayerScrubber::formatTime(double seconds) {
    if (seconds < 0.0)
        seconds = 0.0;
    int totalSec = static_cast<int>(seconds);
    int sec = totalSec % 60;
    int min = (totalSec / 60) % 60;
    int hr = totalSec / 3600;

    if (hr > 0) {
        return QString::asprintf("%d:%02d:%02d", hr, min, sec);
    }
    return QString::asprintf("%d:%02d", min, sec);
}

void PlayerScrubber::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const bool active = m_isHovered || m_isDragging;
    const double trackHeight = active ? 5.0 : 3.0;
    const double trackY = height() - 8.0 - (trackHeight - 3.0) / 2.0;
    const double totalW = static_cast<double>(width());

    // 1. Background Groove
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 50));
    p.drawRoundedRect(QRectF(0, trackY, totalW, trackHeight), trackHeight / 2.0, trackHeight / 2.0);

    // 2. Buffered Progress Track
    if (m_duration > 0.0 && m_bufferedPosition > 0.0) {
        double bufRatio = std::clamp(m_bufferedPosition / m_duration, 0.0, 1.0);
        double bufW = bufRatio * totalW;
        if (bufW > 0.0) {
            p.setBrush(QColor(255, 255, 255, 90));
            p.drawRoundedRect(QRectF(0, trackY, bufW, trackHeight), trackHeight / 2.0, trackHeight / 2.0);
        }
    }

    // 3. Hover Progress Highlight (subtle white track up to cursor)
    if (active && m_duration > 0.0) {
        double hoverRatio = std::clamp(m_hoverPosition / m_duration, 0.0, 1.0);
        double hoverW = hoverRatio * totalW;
        if (hoverW > 0.0) {
            p.setBrush(QColor(255, 255, 255, 130));
            p.drawRoundedRect(QRectF(0, trackY, hoverW, trackHeight), trackHeight / 2.0, trackHeight / 2.0);
        }
    }

    // 4. Played Progress Track (YouTube Red #ff0000)
    double playRatio = m_duration > 0.0 ? std::clamp(m_position / m_duration, 0.0, 1.0) : 0.0;
    double playW = playRatio * totalW;
    if (playW > 0.0) {
        p.setBrush(QColor(0xff, 0x00, 0x00));
        p.drawRoundedRect(QRectF(0, trackY, playW, trackHeight), trackHeight / 2.0, trackHeight / 2.0);
    }

    // 5. Scrubber Handle (Red Circle)
    if (active && m_duration > 0.0) {
        constexpr double handleRadius = 6.5;
        p.setBrush(QColor(0xff, 0x00, 0x00));
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(playW, trackY + trackHeight / 2.0), handleRadius, handleRadius);
    }

    // 6. Floating Time Preview Badge / Tooltip
    // Positioned strictly within the widget bounds (badgeY >= 2) so text and background are never clipped
    if (active && m_duration > 0.0) {
        QString timeStr = formatTime(m_hoverPosition);
        QFont font = p.font();
        font.setPointSize(10);
        font.setBold(true);
        p.setFont(font);

        QFontMetrics fm(font);
        int textW = fm.horizontalAdvance(timeStr);
        int badgeW = textW + 14;
        int badgeH = 22;

        int badgeX = std::clamp(m_hoverMouseX - badgeW / 2, 4, width() - badgeW - 4);
        int badgeY = static_cast<int>(trackY) - badgeH - 4;

        QPainterPath badgePath;
        badgePath.addRoundedRect(QRectF(badgeX, badgeY, badgeW, badgeH), 4.0, 4.0);
        p.fillPath(badgePath, QColor(24, 24, 24, 235));
        p.setPen(QColor(255, 255, 255, 45));
        p.drawPath(badgePath);

        p.setPen(QColor(255, 255, 255));
        p.drawText(QRectF(badgeX, badgeY, badgeW, badgeH), Qt::AlignCenter, timeStr);
    }
}

// ============================================================================
// VolumeSlider Implementation
// ============================================================================

VolumeSlider::VolumeSlider(Qt::Orientation orientation, QWidget *parent) : QSlider(orientation, parent) {}

void VolumeSlider::updateValueFromPos(int x) {
    int w = width();
    if (w <= 0)
        return;
    double ratio = std::clamp(static_cast<double>(x) / static_cast<double>(w), 0.0, 1.0);
    int val = static_cast<int>(std::round(minimum() + ratio * (maximum() - minimum())));
    setValue(val);
}

void VolumeSlider::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        updateValueFromPos(event->pos().x());
        setSliderDown(true);
        event->accept();
        return;
    }
    QSlider::mousePressEvent(event);
}

void VolumeSlider::mouseMoveEvent(QMouseEvent *event) {
    if (isSliderDown()) {
        updateValueFromPos(event->pos().x());
        event->accept();
        return;
    }
    QSlider::mouseMoveEvent(event);
}

void VolumeSlider::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && isSliderDown()) {
        setSliderDown(false);
        emit sliderReleased();
        event->accept();
        return;
    }
    QSlider::mouseReleaseEvent(event);
}

void PlayerScrubber::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_isDragging = true;
        m_hoverMouseX = event->pos().x();
        m_hoverPosition = secondsFromX(m_hoverMouseX);
        m_position = m_hoverPosition;
        emit seekingChanged(true);
        emit seekRequested(m_position);
        update();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void PlayerScrubber::mouseMoveEvent(QMouseEvent *event) {
    m_hoverMouseX = event->pos().x();
    m_hoverPosition = secondsFromX(m_hoverMouseX);
    if (m_isDragging) {
        m_position = m_hoverPosition;
        emit seekLiveMoved(m_position);
    }
    update();
    event->accept();
}

void PlayerScrubber::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && m_isDragging) {
        m_isDragging = false;
        m_hoverMouseX = event->pos().x();
        m_position = secondsFromX(m_hoverMouseX);
        emit seekRequested(m_position);
        emit seekingChanged(false);
        update();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void PlayerScrubber::enterEvent(QEnterEvent *event) {
    m_isHovered = true;
    m_hoverMouseX = static_cast<int>(event->position().x());
    m_hoverPosition = secondsFromX(m_hoverMouseX);
    update();
    QWidget::enterEvent(event);
}

void PlayerScrubber::leaveEvent(QEvent *event) {
    m_isHovered = false;
    update();
    QWidget::leaveEvent(event);
}

// ============================================================================
// PlayerSettingsPopup Implementation
// ============================================================================

PlayerSettingsPopup::PlayerSettingsPopup(QWidget *parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint) {
    setAttribute(Qt::WA_TranslucentBackground, false);
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(205);
    setStyleSheet("PlayerSettingsPopup {"
                  "  background-color: #212121;"
                  "  border: 1px solid rgba(255, 255, 255, 0.15);"
                  "  border-radius: 8px;"
                  "}");
    setupUi();
}

QSize PlayerSettingsPopup::sizeHint() const {
    if (m_stack && m_stack->currentWidget()) {
        int h = m_stack->currentWidget()->sizeHint().height() + 8;
        return QSize(205, h);
    }
    return QSize(205, 88);
}

QSize PlayerSettingsPopup::minimumSizeHint() const {
    return sizeHint();
}

void PlayerSettingsPopup::setupUi() {
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 4, 0, 4);
    rootLayout->setSpacing(0);

    m_stack = new DynamicStackedWidget(this);
    m_stack->setStyleSheet("background: transparent;");
    rootLayout->addWidget(m_stack);

    buildMainMenu();

    // Quality Page
    m_qualityPage = new QWidget(this);
    auto *qPageLay = new QVBoxLayout(m_qualityPage);
    qPageLay->setContentsMargins(0, 0, 0, 0);
    qPageLay->setSpacing(0);

    auto *qHeaderBtn = new QPushButton(m_qualityPage);
    qHeaderBtn->setCursor(Qt::PointingHandCursor);
    qHeaderBtn->setFocusPolicy(Qt::NoFocus);
    qHeaderBtn->setFixedHeight(34);
    qHeaderBtn->setStyleSheet(
        "QPushButton { background: transparent; border: none; text-align: left; padding: 0px 8px; color: #f1f1f1; "
        "font-size: 13px; font-weight: bold; border-radius: 4px; margin: 1px 4px; }"
        "QPushButton:hover { background-color: rgba(255, 255, 255, 0.1); }"
        "QPushButton:pressed { background-color: rgba(255, 255, 255, 0.2); }");
    qHeaderBtn->setIcon(lightyt::ui::icons::chevronLeft(14, QColor(0xf1, 0xf1, 0xf1)));
    qHeaderBtn->setIconSize(QSize(14, 14));
    qHeaderBtn->setText("  Quality");
    connect(qHeaderBtn, &QPushButton::clicked, this, [this]() {
        m_stack->setCurrentWidget(m_mainPage);
        reposition();
    });
    qPageLay->addWidget(qHeaderBtn);

    auto *qSep = new QFrame(m_qualityPage);
    qSep->setFrameShape(QFrame::HLine);
    qSep->setFixedHeight(1);
    qSep->setStyleSheet("background-color: rgba(255, 255, 255, 0.12); border: none; margin: 2px 4px;");
    qPageLay->addWidget(qSep);

    m_qualityListContainer = new QWidget(m_qualityPage);
    m_qualityListLayout = new QVBoxLayout(m_qualityListContainer);
    m_qualityListLayout->setContentsMargins(0, 0, 0, 0);
    m_qualityListLayout->setSpacing(0);
    qPageLay->addWidget(m_qualityListContainer);
    qPageLay->addStretch(1);

    m_stack->addWidget(m_qualityPage);

    // Speed Page
    m_speedPage = new QWidget(this);
    auto *sPageLay = new QVBoxLayout(m_speedPage);
    sPageLay->setContentsMargins(0, 0, 0, 0);
    sPageLay->setSpacing(0);

    auto *sHeaderBtn = new QPushButton(m_speedPage);
    sHeaderBtn->setCursor(Qt::PointingHandCursor);
    sHeaderBtn->setFocusPolicy(Qt::NoFocus);
    sHeaderBtn->setFixedHeight(34);
    sHeaderBtn->setStyleSheet(
        "QPushButton { background: transparent; border: none; text-align: left; padding: 0px 8px; color: #f1f1f1; "
        "font-size: 13px; font-weight: bold; border-radius: 4px; margin: 1px 4px; }"
        "QPushButton:hover { background-color: rgba(255, 255, 255, 0.1); }"
        "QPushButton:pressed { background-color: rgba(255, 255, 255, 0.2); }");
    sHeaderBtn->setIcon(lightyt::ui::icons::chevronLeft(14, QColor(0xf1, 0xf1, 0xf1)));
    sHeaderBtn->setIconSize(QSize(14, 14));
    sHeaderBtn->setText("  Playback speed");
    connect(sHeaderBtn, &QPushButton::clicked, this, [this]() {
        m_stack->setCurrentWidget(m_mainPage);
        reposition();
    });
    sPageLay->addWidget(sHeaderBtn);

    auto *sSep = new QFrame(m_speedPage);
    sSep->setFrameShape(QFrame::HLine);
    sSep->setFixedHeight(1);
    sSep->setStyleSheet("background-color: rgba(255, 255, 255, 0.12); border: none; margin: 2px 4px;");
    sPageLay->addWidget(sSep);

    m_speedListContainer = new QWidget(m_speedPage);
    m_speedListLayout = new QVBoxLayout(m_speedListContainer);
    m_speedListLayout->setContentsMargins(0, 0, 0, 0);
    m_speedListLayout->setSpacing(0);
    sPageLay->addWidget(m_speedListContainer);
    sPageLay->addStretch(1);

    m_stack->addWidget(m_speedPage);

    rebuildQualityMenu();
    rebuildSpeedMenu();
}

void PlayerSettingsPopup::buildMainMenu() {
    m_mainPage = new QWidget(this);
    auto *lay = new QVBoxLayout(m_mainPage);
    lay->setContentsMargins(0, 2, 0, 2);
    lay->setSpacing(2);

    // Row 1: Playback speed
    auto *speedBtn = new QPushButton(m_mainPage);
    speedBtn->setCursor(Qt::PointingHandCursor);
    speedBtn->setFocusPolicy(Qt::NoFocus);
    speedBtn->setFixedHeight(36);
    speedBtn->setStyleSheet(
        "QPushButton { background: transparent; border: none; padding: 0px 8px; border-radius: 4px; margin: 1px 4px; }"
        "QPushButton:hover { background-color: rgba(255, 255, 255, 0.1); }"
        "QPushButton:pressed { background-color: rgba(255, 255, 255, 0.2); }");
    auto *speedRow = new QHBoxLayout(speedBtn);
    speedRow->setContentsMargins(4, 0, 4, 0);
    speedRow->setSpacing(4);
    auto *speedTitle = new QLabel("Playback speed", speedBtn);
    speedTitle->setStyleSheet("color: #f1f1f1; font-size: 13px; font-weight: 500; background: transparent;");
    m_speedValLabel =
        new QLabel(std::abs(m_currentSpeed - 1.0) < 0.01 ? "Normal" : QString::number(m_currentSpeed) + "x", speedBtn);
    m_speedValLabel->setStyleSheet("color: #aaaaaa; font-size: 12px; background: transparent; padding-right: 2px;");
    auto *speedChevron = new QLabel(speedBtn);
    speedChevron->setPixmap(lightyt::ui::icons::chevronRight(14, QColor(0xaa, 0xaa, 0xaa)).pixmap(14, 14));
    speedChevron->setStyleSheet("background: transparent;");

    speedRow->addWidget(speedTitle);
    speedRow->addStretch(1);
    speedRow->addWidget(m_speedValLabel);
    speedRow->addWidget(speedChevron);
    connect(speedBtn, &QPushButton::clicked, this, [this]() {
        rebuildSpeedMenu();
        m_stack->setCurrentWidget(m_speedPage);
        reposition();
    });
    lay->addWidget(speedBtn);

    // Row 2: Quality
    auto *qualityBtn = new QPushButton(m_mainPage);
    qualityBtn->setCursor(Qt::PointingHandCursor);
    qualityBtn->setFocusPolicy(Qt::NoFocus);
    qualityBtn->setFixedHeight(36);
    qualityBtn->setStyleSheet(
        "QPushButton { background: transparent; border: none; padding: 0px 8px; border-radius: 4px; margin: 1px 4px; }"
        "QPushButton:hover { background-color: rgba(255, 255, 255, 0.1); }"
        "QPushButton:pressed { background-color: rgba(255, 255, 255, 0.2); }");
    auto *qualityRow = new QHBoxLayout(qualityBtn);
    qualityRow->setContentsMargins(4, 0, 4, 0);
    qualityRow->setSpacing(4);
    auto *qualityTitle = new QLabel("Quality", qualityBtn);
    qualityTitle->setStyleSheet("color: #f1f1f1; font-size: 13px; font-weight: 500; background: transparent;");
    m_qualityValLabel = new QLabel(m_currentQuality, qualityBtn);
    m_qualityValLabel->setStyleSheet("color: #aaaaaa; font-size: 12px; background: transparent; padding-right: 2px;");
    auto *qualityChevron = new QLabel(qualityBtn);
    qualityChevron->setPixmap(lightyt::ui::icons::chevronRight(14, QColor(0xaa, 0xaa, 0xaa)).pixmap(14, 14));
    qualityChevron->setStyleSheet("background: transparent;");

    qualityRow->addWidget(qualityTitle);
    qualityRow->addStretch(1);
    qualityRow->addWidget(m_qualityValLabel);
    qualityRow->addWidget(qualityChevron);
    connect(qualityBtn, &QPushButton::clicked, this, [this]() {
        rebuildQualityMenu();
        m_stack->setCurrentWidget(m_qualityPage);
        reposition();
    });
    lay->addWidget(qualityBtn);

    // Row 3: Stats for nerds
    auto *statsBtn = new QPushButton(m_mainPage);
    statsBtn->setCursor(Qt::PointingHandCursor);
    statsBtn->setFocusPolicy(Qt::NoFocus);
    statsBtn->setFixedHeight(36);
    statsBtn->setStyleSheet(
        "QPushButton { background: transparent; border: none; padding: 0px 8px; border-radius: 4px; margin: 1px 4px; }"
        "QPushButton:hover { background-color: rgba(255, 255, 255, 0.1); }"
        "QPushButton:pressed { background-color: rgba(255, 255, 255, 0.2); }");
    auto *statsRow = new QHBoxLayout(statsBtn);
    statsRow->setContentsMargins(4, 0, 4, 0);
    statsRow->setSpacing(4);
    auto *statsTitle = new QLabel("Stats for nerds", statsBtn);
    statsTitle->setStyleSheet("color: #f1f1f1; font-size: 13px; font-weight: 500; background: transparent;");
    statsRow->addWidget(statsTitle);
    statsRow->addStretch(1);
    connect(statsBtn, &QPushButton::clicked, this, [this]() {
        emit statsForNerdsToggled();
        close();
    });
    lay->addWidget(statsBtn);

    m_stack->addWidget(m_mainPage);
}

void PlayerSettingsPopup::rebuildQualityMenu() {
    if (!m_qualityListLayout)
        return;

    QLayoutItem *child;
    while ((child = m_qualityListLayout->takeAt(0)) != nullptr) {
        delete child->widget();
        delete child;
    }

    for (const auto &q : m_availableQualities) {
        auto *btn = new QPushButton(m_qualityListContainer);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFocusPolicy(Qt::NoFocus);
        btn->setFixedHeight(32);
        btn->setStyleSheet("QPushButton { background: transparent; border: none; border-radius: 4px; margin: 1px 4px; "
                           "padding: 0 4px; }"
                           "QPushButton:hover { background-color: rgba(255, 255, 255, 0.1); }"
                           "QPushButton:pressed { background-color: rgba(255, 255, 255, 0.2); }");
        auto *row = new QHBoxLayout(btn);
        row->setContentsMargins(4, 0, 4, 0);

        auto *iconLabel = new QLabel(btn);
        iconLabel->setFixedSize(18, 18);
        iconLabel->setStyleSheet("background: transparent;");
        if (q == m_currentQuality) {
            iconLabel->setPixmap(lightyt::ui::icons::checkMark(14, QColor(0xf1, 0xf1, 0xf1)).pixmap(14, 14));
        }
        row->addWidget(iconLabel);

        auto *label = new QLabel(q, btn);
        label->setStyleSheet(
            "color: #f1f1f1; font-size: 13px; font-weight: 500; background: transparent; padding-left: 4px;");
        row->addWidget(label);
        row->addStretch(1);

        connect(btn, &QPushButton::clicked, this, [this, q]() {
            m_currentQuality = q;
            if (m_qualityValLabel)
                m_qualityValLabel->setText(q);
            emit qualitySelected(q);
            close();
        });
        m_qualityListLayout->addWidget(btn);
    }
}

void PlayerSettingsPopup::rebuildSpeedMenu() {
    if (!m_speedListLayout)
        return;

    QLayoutItem *child;
    while ((child = m_speedListLayout->takeAt(0)) != nullptr) {
        delete child->widget();
        delete child;
    }

    const std::vector<std::pair<double, QString>> speeds = {
        {1.0, "1x (Normal)"}, {1.25, "1.25x"}, {1.5, "1.5x"}, {1.75, "1.75x"}, {2.0, "2x"}};

    for (const auto &[s, name] : speeds) {
        auto *btn = new QPushButton(m_speedListContainer);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFocusPolicy(Qt::NoFocus);
        btn->setFixedHeight(32);
        btn->setStyleSheet("QPushButton { background: transparent; border: none; border-radius: 4px; margin: 1px 4px; "
                           "padding: 0 4px; }"
                           "QPushButton:hover { background-color: rgba(255, 255, 255, 0.1); }"
                           "QPushButton:pressed { background-color: rgba(255, 255, 255, 0.2); }");
        auto *row = new QHBoxLayout(btn);
        row->setContentsMargins(4, 0, 4, 0);

        auto *iconLabel = new QLabel(btn);
        iconLabel->setFixedSize(18, 18);
        iconLabel->setStyleSheet("background: transparent;");
        if (std::abs(s - m_currentSpeed) < 0.01) {
            iconLabel->setPixmap(lightyt::ui::icons::checkMark(14, QColor(0xf1, 0xf1, 0xf1)).pixmap(14, 14));
        }
        row->addWidget(iconLabel);

        auto *label = new QLabel(name, btn);
        label->setStyleSheet(
            "color: #f1f1f1; font-size: 13px; font-weight: 500; background: transparent; padding-left: 4px;");
        row->addWidget(label);
        row->addStretch(1);

        connect(btn, &QPushButton::clicked, this, [this, s]() {
            m_currentSpeed = s;
            if (m_speedValLabel) {
                m_speedValLabel->setText(std::abs(s - 1.0) < 0.01 ? "Normal" : QString::number(s) + "x");
            }
            emit playbackSpeedSelected(s);
            close();
        });
        m_speedListLayout->addWidget(btn);
    }
}

void PlayerSettingsPopup::setQuality(const QString &quality) {
    m_currentQuality = quality;
    if (m_qualityValLabel) {
        m_qualityValLabel->setText(quality);
    }
}

void PlayerSettingsPopup::setAvailableQualities(const QStringList &qualities) {
    if (!qualities.isEmpty()) {
        m_availableQualities = qualities;
    }
}

void PlayerSettingsPopup::setPlaybackSpeed(double speed) {
    m_currentSpeed = speed;
    if (m_speedValLabel) {
        m_speedValLabel->setText(std::abs(speed - 1.0) < 0.01 ? "Normal" : QString::number(speed) + "x");
    }
}

void PlayerSettingsPopup::showAbove(QWidget *target) {
    if (!target)
        return;
    m_targetBtn = target;
    m_stack->setCurrentWidget(m_mainPage);
    reposition();
    show();
}

void PlayerSettingsPopup::reposition() {
    if (!m_targetBtn)
        return;
    if (m_stack) {
        m_stack->adjustSize();
    }
    adjustSize();

    QPoint globalPos = m_targetBtn->mapToGlobal(QPoint(0, 0));
    int x = globalPos.x() + m_targetBtn->width() - width();
    int y = globalPos.y() - height() - 8;

    QScreen *scr = m_targetBtn->screen();
    if (scr) {
        QRect geo = scr->availableGeometry();
        if (x < geo.left() + 8)
            x = geo.left() + 8;
        if (x + width() > geo.right() - 8)
            x = geo.right() - 8 - width();
        if (y < geo.top() + 8) {
            y = globalPos.y() + m_targetBtn->height() + 8;
        }
    }
    move(x, y);
}

void PlayerSettingsPopup::hideEvent(QHideEvent *event) {
    QWidget::hideEvent(event);
    emit popupHidden();
}

void PlayerSettingsPopup::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        if (m_stack && m_stack->currentWidget() != m_mainPage) {
            m_stack->setCurrentWidget(m_mainPage);
            reposition();
            event->accept();
            return;
        }
    }
    QWidget::keyPressEvent(event);
}

// ============================================================================
// PlayerControlsOverlay Implementation
// ============================================================================

PlayerControlsOverlay::PlayerControlsOverlay(QWidget *parent) : QWidget(parent) {
    setObjectName("playerControlsOverlay");
    setMouseTracking(true);
    setupUi();
}

void PlayerControlsOverlay::setupUi() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 1. Scrubber Container (horizontal padding 12px)
    auto *scrubberContainer = new QWidget(this);
    scrubberContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *scrubLay = new QHBoxLayout(scrubberContainer);
    scrubLay->setContentsMargins(12, 0, 12, 0);
    scrubLay->setSpacing(0);

    m_scrubber = new PlayerScrubber(scrubberContainer);
    connect(m_scrubber, &PlayerScrubber::seekRequested, this, &PlayerControlsOverlay::seekRequested);
    connect(m_scrubber, &PlayerScrubber::seekLiveMoved, this, [this](double sec) {
        m_position = sec;
        updateTimeLabel();
        emit seekLiveMoved(sec);
    });
    connect(m_scrubber, &PlayerScrubber::seekingChanged, this, &PlayerControlsOverlay::seekingChanged);
    scrubLay->addWidget(m_scrubber);
    mainLayout->addWidget(scrubberContainer);

    // 2. Bottom Controls Row
    auto *controlsContainer = new QWidget(this);
    controlsContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    controlsContainer->setFixedHeight(40);
    auto *controlsRow = new QHBoxLayout(controlsContainer);
    controlsRow->setContentsMargins(12, 0, 12, 4);
    controlsRow->setSpacing(4);
    controlsRow->setAlignment(Qt::AlignVCenter);

    // 2a. Play / Pause Button (36x36)
    m_playPauseBtn = new QPushButton(controlsContainer);
    m_playPauseBtn->setFocusPolicy(Qt::NoFocus);
    m_playPauseBtn->setCursor(Qt::PointingHandCursor);
    m_playPauseBtn->setFixedSize(36, 36);
    m_playPauseBtn->setIcon(lightyt::ui::icons::pause(20, QColor(0xf1, 0xf1, 0xf1)));
    m_playPauseBtn->setIconSize(QSize(20, 20));
    m_playPauseBtn->setStyleSheet(kIconBtnQss);
    m_playPauseBtn->setToolTip("Play (k)");
    connect(m_playPauseBtn, &QPushButton::clicked, this, &PlayerControlsOverlay::playPauseClicked);
    controlsRow->addWidget(m_playPauseBtn, 0, Qt::AlignVCenter);

    // 2b. Expanding Volume Area (fixed height 36, starts at 36px wide for mute button only)
    m_volumeArea = new QWidget(controlsContainer);
    m_volumeArea->setFixedSize(36, 36);
    m_volumeArea->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto *volLay = new QHBoxLayout(m_volumeArea);
    volLay->setContentsMargins(0, 0, 0, 0);
    volLay->setSpacing(0);
    volLay->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_muteBtn = new QPushButton(m_volumeArea);
    m_muteBtn->setFocusPolicy(Qt::NoFocus);
    m_muteBtn->setCursor(Qt::PointingHandCursor);
    m_muteBtn->setFixedSize(36, 36);
    m_muteBtn->setIcon(lightyt::ui::icons::volume(20, QColor(0xf1, 0xf1, 0xf1), false));
    m_muteBtn->setIconSize(QSize(20, 20));
    m_muteBtn->setStyleSheet(kIconBtnQss);
    m_muteBtn->setToolTip("Mute (m)");
    connect(m_muteBtn, &QPushButton::clicked, this, &PlayerControlsOverlay::onMuteClicked);
    volLay->addWidget(m_muteBtn, 0, Qt::AlignLeft | Qt::AlignVCenter);

    m_sliderContainer = new QWidget(m_volumeArea);
    m_sliderContainer->setFixedHeight(36);
    m_sliderContainer->setFixedWidth(0);
    m_sliderContainer->hide();
    auto *sliderInnerLay = new QHBoxLayout(m_sliderContainer);
    sliderInnerLay->setContentsMargins(4, 8, 6, 8);
    sliderInnerLay->setSpacing(0);
    sliderInnerLay->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_volumeSlider = new VolumeSlider(Qt::Horizontal, m_sliderContainer);
    m_volumeSlider->setFocusPolicy(Qt::NoFocus);
    m_volumeSlider->setCursor(Qt::PointingHandCursor);
    m_volumeSlider->setFixedHeight(20);
    m_volumeSlider->setFixedWidth(56);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(100);
    m_volumeSlider->setStyleSheet(kVolumeSliderQss);
    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int val) {
        if (val > 0) {
            m_preMuteVolume = val;
        }
        updateVolumeIcon();
        emit volumeChanged(val);
    });
    connect(m_volumeSlider, &QSlider::sliderReleased, this, [this]() {
        if (!m_volumeArea->underMouse()) {
            animateVolumeExpand(false);
        }
    });
    sliderInnerLay->addWidget(m_volumeSlider, 0, Qt::AlignLeft | Qt::AlignVCenter);
    volLay->addWidget(m_sliderContainer, 0, Qt::AlignLeft | Qt::AlignVCenter);

    m_volExpandAnimation = new QVariantAnimation(this);
    m_volExpandAnimation->setDuration(160);
    m_volExpandAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_volExpandAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &val) {
        int w = val.toInt();
        m_sliderContainer->setFixedWidth(w);
        m_volumeArea->setFixedWidth(36 + w);
    });
    connect(m_volExpandAnimation, &QVariantAnimation::finished, this, [this]() {
        if (!m_isVolumeExpanded) {
            m_sliderContainer->setFixedWidth(0);
            m_sliderContainer->hide();
            m_volumeArea->setFixedWidth(36);
        }
    });

    m_volCollapseTimer = new QTimer(this);
    m_volCollapseTimer->setSingleShot(true);
    m_volCollapseTimer->setInterval(100);
    connect(m_volCollapseTimer, &QTimer::timeout, this, [this]() {
        if (!m_volumeArea->underMouse() && (!m_volumeSlider || !m_volumeSlider->isSliderDown())) {
            animateVolumeExpand(false);
        }
    });

    m_volumeArea->installEventFilter(this);
    m_muteBtn->installEventFilter(this);
    m_sliderContainer->installEventFilter(this);
    m_volumeSlider->installEventFilter(this);
    controlsRow->addWidget(m_volumeArea, 0, Qt::AlignVCenter);

    // 2c. Time Display Label
    m_timeLabel = new QLabel("0:00 / 0:00", controlsContainer);
    m_timeLabel->setStyleSheet(
        "color: #dddddd; font-size: 12px; font-weight: 500; padding-left: 6px; background: transparent;");
    controlsRow->addWidget(m_timeLabel, 0, Qt::AlignVCenter);

    controlsRow->addStretch(1);

    // 2d. Settings / Gear Button (36x36)
    m_settingsBtn = new QPushButton(controlsContainer);
    m_settingsBtn->setFocusPolicy(Qt::NoFocus);
    m_settingsBtn->setCursor(Qt::PointingHandCursor);
    m_settingsBtn->setFixedSize(36, 36);
    m_settingsBtn->setIcon(lightyt::ui::icons::gear(20, QColor(0xf1, 0xf1, 0xf1)));
    m_settingsBtn->setIconSize(QSize(20, 20));
    m_settingsBtn->setStyleSheet(kIconBtnQss);
    m_settingsBtn->setToolTip("Settings");
    connect(m_settingsBtn, &QPushButton::clicked, this, &PlayerControlsOverlay::onSettingsClicked);
    controlsRow->addWidget(m_settingsBtn, 0, Qt::AlignVCenter);

    // 2e. Fullscreen Button (36x36)
    m_fullscreenBtn = new QPushButton(controlsContainer);
    m_fullscreenBtn->setFocusPolicy(Qt::NoFocus);
    m_fullscreenBtn->setCursor(Qt::PointingHandCursor);
    m_fullscreenBtn->setFixedSize(36, 36);
    m_fullscreenBtn->setIcon(lightyt::ui::icons::fullscreen(20, QColor(0xf1, 0xf1, 0xf1), false));
    m_fullscreenBtn->setIconSize(QSize(20, 20));
    m_fullscreenBtn->setStyleSheet(kIconBtnQss);
    m_fullscreenBtn->setToolTip("Full screen (f)");
    connect(m_fullscreenBtn, &QPushButton::clicked, this, &PlayerControlsOverlay::fullscreenToggleRequested);
    controlsRow->addWidget(m_fullscreenBtn, 0, Qt::AlignVCenter);

    mainLayout->addWidget(controlsContainer);

    m_settingsPopup = new PlayerSettingsPopup(this);
    connect(m_settingsPopup, &PlayerSettingsPopup::qualitySelected, this, [this](const QString &q) {
        m_quality = q;
        emit qualityChanged(q);
    });
    connect(m_settingsPopup, &PlayerSettingsPopup::playbackSpeedSelected, this, [this](double s) {
        m_playbackSpeed = s;
        emit playbackSpeedChanged(s);
    });
    connect(m_settingsPopup, &PlayerSettingsPopup::statsForNerdsToggled, this,
            &PlayerControlsOverlay::statsForNerdsToggled);
    connect(m_settingsPopup, &PlayerSettingsPopup::popupHidden, this, [this]() {
        m_isMenuOpen = false;
        if (m_isPlaying) {
            scheduleHide(2500);
        }
    });

    // 3. Smooth Opacity Effect and Fade Animation
    m_opacityEffect = new QGraphicsOpacityEffect(this);
    m_opacityEffect->setOpacity(1.0);
    setGraphicsEffect(m_opacityEffect);

    m_fadeAnimation = new QPropertyAnimation(m_opacityEffect, "opacity", this);
    m_fadeAnimation->setDuration(200);
    m_fadeAnimation->setEasingCurve(QEasingCurve::InOutQuad);
    connect(m_fadeAnimation, &QPropertyAnimation::finished, this, [this]() {
        if (m_opacityEffect->opacity() <= 0.01) {
            hide();
            emit controlsVisibilityChanged(false);
        }
    });

    // 4. Inactivity Auto-Hide Timer
    m_hideTimer = new QTimer(this);
    m_hideTimer->setSingleShot(true);
    connect(m_hideTimer, &QTimer::timeout, this, &PlayerControlsOverlay::onHideTimerTimeout);
}

void PlayerControlsOverlay::paintEvent(QPaintEvent *) {
    QPainter p(this);
    // YouTube bottom shadow gradient: transparent at top to deep translucent black at bottom
    QLinearGradient grad(0, 0, 0, height());
    grad.setColorAt(0.0, QColor(0, 0, 0, 0));
    grad.setColorAt(0.25, QColor(0, 0, 0, 50));
    grad.setColorAt(0.65, QColor(0, 0, 0, 130));
    grad.setColorAt(1.0, QColor(0, 0, 0, 210));
    p.fillRect(rect(), grad);
}

bool PlayerControlsOverlay::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_volumeArea || watched == m_muteBtn || watched == m_sliderContainer || watched == m_volumeSlider) {
        if (event->type() == QEvent::Enter) {
            if (m_volCollapseTimer) {
                m_volCollapseTimer->stop();
            }
            animateVolumeExpand(true);
        } else if (event->type() == QEvent::Leave) {
            if (m_volCollapseTimer) {
                m_volCollapseTimer->start();
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void PlayerControlsOverlay::animateVolumeExpand(bool expand) {
    if (!m_sliderContainer || !m_volExpandAnimation)
        return;
    if (m_isVolumeExpanded == expand && m_volExpandAnimation->state() != QAbstractAnimation::Running) {
        return;
    }
    m_isVolumeExpanded = expand;

    constexpr int kTargetSliderW = 66;  // 56px slider + margins
    int curW = m_sliderContainer->width();
    int endW = expand ? kTargetSliderW : 0;

    if (expand && !m_sliderContainer->isVisible()) {
        m_sliderContainer->show();
    }

    m_volExpandAnimation->stop();
    m_volExpandAnimation->setStartValue(curW);
    m_volExpandAnimation->setEndValue(endW);
    m_volExpandAnimation->start();
}

void PlayerControlsOverlay::setPlaying(bool playing) {
    m_isPlaying = playing;
    m_playPauseBtn->setIcon(playing ? lightyt::ui::icons::pause(20, QColor(0xf1, 0xf1, 0xf1))
                                    : lightyt::ui::icons::play(20, QColor(0xf1, 0xf1, 0xf1)));
    m_playPauseBtn->setToolTip(playing ? "Pause (k)" : "Play (k)");

    if (!playing) {
        showControlsAnimated();
        m_hideTimer->stop();
    } else {
        m_hideTimer->start(2500);
    }
}

void PlayerControlsOverlay::setDuration(double seconds) {
    m_duration = std::max(0.0, seconds);
    if (m_scrubber) {
        m_scrubber->setDuration(seconds);
    }
    updateTimeLabel();
}

void PlayerControlsOverlay::setPosition(double seconds) {
    m_position = std::max(0.0, seconds);
    if (m_scrubber) {
        m_scrubber->setPosition(seconds);
    }
    updateTimeLabel();
}

void PlayerControlsOverlay::setBufferedPosition(double seconds) {
    if (m_scrubber) {
        m_scrubber->setBufferedPosition(seconds);
    }
}

void PlayerControlsOverlay::setVolume(int volume) {
    int clamped = std::clamp(volume, 0, 100);
    if (clamped > 0) {
        m_preMuteVolume = clamped;
    }
    if (m_volumeSlider && m_volumeSlider->value() != clamped) {
        m_volumeSlider->blockSignals(true);
        m_volumeSlider->setValue(clamped);
        m_volumeSlider->blockSignals(false);
    }
    updateVolumeIcon();
}

void PlayerControlsOverlay::setFullscreen(bool isFullscreen) {
    m_isFullscreen = isFullscreen;
    m_fullscreenBtn->setIcon(lightyt::ui::icons::fullscreen(20, QColor(0xf1, 0xf1, 0xf1), isFullscreen));
    m_fullscreenBtn->setToolTip(isFullscreen ? "Exit full screen (f)" : "Full screen (f)");
}

void PlayerControlsOverlay::onMuteClicked() {
    int cur = m_volumeSlider ? m_volumeSlider->value() : 0;
    int target = (cur > 0) ? 0 : (m_preMuteVolume > 0 ? m_preMuteVolume : 100);
    setVolume(target);
    emit volumeChanged(target);
}

void PlayerControlsOverlay::updateVolumeIcon() {
    int cur = m_volumeSlider ? m_volumeSlider->value() : 0;
    bool isMuted = (cur == 0);
    m_muteBtn->setIcon(lightyt::ui::icons::volume(20, QColor(0xf1, 0xf1, 0xf1), isMuted));
    m_muteBtn->setToolTip(isMuted ? "Unmute (m)" : "Mute (m)");
}

void PlayerControlsOverlay::updateTimeLabel() {
    if (!m_timeLabel)
        return;
    QString curText = formatTime(m_position);
    QString durText = formatTime(m_duration);
    m_timeLabel->setText(curText + " / " + durText);
}

QString PlayerControlsOverlay::formatTime(double seconds) {
    if (seconds < 0.0)
        seconds = 0.0;
    int totalSec = static_cast<int>(seconds);
    int sec = totalSec % 60;
    int min = (totalSec / 60) % 60;
    int hr = totalSec / 3600;

    if (hr > 0) {
        return QString::asprintf("%d:%02d:%02d", hr, min, sec);
    }
    return QString::asprintf("%d:%02d", min, sec);
}

bool PlayerControlsOverlay::isInteracting() const noexcept {
    if (m_isMenuOpen)
        return true;
    if (m_scrubber && m_scrubber->isDragging())
        return true;
    if (m_volumeSlider && m_volumeSlider->isSliderDown())
        return true;
    if (m_volumeArea && m_volumeArea->underMouse())
        return true;
    if (m_isVolumeExpanded)
        return true;
    if (underMouse())
        return true;
    return false;
}

bool PlayerControlsOverlay::isControlsVisible() const noexcept {
    return isVisible() && m_opacityEffect && m_opacityEffect->opacity() > 0.05;
}

void PlayerControlsOverlay::showControlsAnimated() {
    if (!isVisible()) {
        show();
    }
    m_fadeAnimation->stop();
    m_fadeAnimation->setStartValue(m_opacityEffect->opacity());
    m_fadeAnimation->setEndValue(1.0);
    m_fadeAnimation->start();
    emit controlsVisibilityChanged(true);

    if (m_isPlaying) {
        m_hideTimer->start(2500);
    } else {
        m_hideTimer->stop();
    }
}

void PlayerControlsOverlay::hideControlsAnimated() {
    if (!isVisible() || isInteracting())
        return;
    animateVolumeExpand(false);
    m_fadeAnimation->stop();
    m_fadeAnimation->setStartValue(m_opacityEffect->opacity());
    m_fadeAnimation->setEndValue(0.0);
    m_fadeAnimation->start();
}

void PlayerControlsOverlay::scheduleHide(int delayMs) {
    if (!m_isPlaying || isInteracting())
        return;
    m_hideTimer->start(delayMs);
}

void PlayerControlsOverlay::onHideTimerTimeout() {
    if (m_isPlaying && !isInteracting()) {
        hideControlsAnimated();
    }
}

void PlayerControlsOverlay::enterEvent(QEnterEvent *event) {
    showControlsAnimated();
    QWidget::enterEvent(event);
}

void PlayerControlsOverlay::leaveEvent(QEvent *event) {
    if (m_isPlaying && !isInteracting()) {
        scheduleHide(150);
    }
    QWidget::leaveEvent(event);
}

void PlayerControlsOverlay::mouseMoveEvent(QMouseEvent *event) {
    showControlsAnimated();
    QWidget::mouseMoveEvent(event);
}

void PlayerControlsOverlay::setQuality(const QString &quality) {
    m_quality = quality;
    if (m_settingsPopup) {
        m_settingsPopup->setQuality(quality);
    }
}

void PlayerControlsOverlay::setAvailableQualities(const QStringList &qualities) {
    if (!qualities.isEmpty()) {
        m_availableQualities = qualities;
        if (m_settingsPopup) {
            m_settingsPopup->setAvailableQualities(qualities);
        }
    }
}

void PlayerControlsOverlay::setPlaybackSpeed(double speed) {
    m_playbackSpeed = speed;
    if (m_settingsPopup) {
        m_settingsPopup->setPlaybackSpeed(speed);
    }
}

void PlayerControlsOverlay::onSettingsClicked() {
    if (!m_settingsPopup)
        return;
    if (m_settingsPopup->isVisible()) {
        m_settingsPopup->close();
        return;
    }
    m_isMenuOpen = true;
    m_hideTimer->stop();
    m_settingsPopup->setQuality(m_quality);
    m_settingsPopup->setAvailableQualities(m_availableQualities);
    m_settingsPopup->setPlaybackSpeed(m_playbackSpeed);
    m_settingsPopup->showAbove(m_settingsBtn);
}
