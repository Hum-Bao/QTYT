#include "StatsForNerdsWidget.h"
#include "../player/PlayerController.h"

#include <QApplication>
#include <QCursor>
#include <QRandomGenerator>
#include <algorithm>
#include <cmath>

StatsForNerdsWidget::StatsForNerdsWidget(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(410);
    setStyleSheet("StatsForNerdsWidget {"
                  "  background-color: #1a1a1a;"
                  "  border: 1px solid rgba(255, 255, 255, 0.22);"
                  "  border-radius: 8px;"
                  "}");

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(1000);
    connect(m_refreshTimer, &QTimer::timeout, this, &StatsForNerdsWidget::refreshStats);

    generateCpn();
    setupUi();
}

QSize StatsForNerdsWidget::sizeHint() const {
    if (layout()) {
        return layout()->sizeHint();
    }
    return QSize(410, 270);
}

QSize StatsForNerdsWidget::minimumSizeHint() const {
    return sizeHint();
}

void StatsForNerdsWidget::generateCpn() {
    static constexpr char kChars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    auto *rng = QRandomGenerator::global();
    m_cpn.clear();
    m_cpn.reserve(19);
    for (int i = 0; i < 16; ++i) {
        if (i > 0 && i % 4 == 0) {
            m_cpn.push_back(' ');
        }
        m_cpn.push_back(kChars[rng->bounded(static_cast<int>(sizeof(kChars) - 1))]);
    }
}

void StatsForNerdsWidget::setupUi() {
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setSizeConstraint(QLayout::SetFixedSize);
    rootLayout->setContentsMargins(14, 10, 14, 12);
    rootLayout->setSpacing(8);

    // Header bar (Title + Close Button)
    auto *headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(4);

    auto *titleLabel = new QLabel("Stats for nerds", this);
    titleLabel->setStyleSheet("font-size: 12px; font-weight: bold; color: #f1f1f1; background: transparent;");
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch(1);

    auto *closeBtn = new QPushButton("✕", this);
    closeBtn->setFixedSize(20, 20);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setFocusPolicy(Qt::NoFocus);
    closeBtn->setStyleSheet("QPushButton {"
                            "  background: transparent;"
                            "  border: none;"
                            "  color: #aaaaaa;"
                            "  font-size: 13px;"
                            "  font-weight: bold;"
                            "  border-radius: 10px;"
                            "}"
                            "QPushButton:hover {"
                            "  background-color: rgba(255, 255, 255, 0.15);"
                            "  color: #ffffff;"
                            "}"
                            "QPushButton:pressed {"
                            "  background-color: rgba(255, 255, 255, 0.25);"
                            "}");
    connect(closeBtn, &QPushButton::clicked, this, [this]() {
        hide();
        emit closed();
    });
    headerLayout->addWidget(closeBtn);
    rootLayout->addLayout(headerLayout);

    // Separator line
    auto *sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setFixedHeight(1);
    sep->setStyleSheet("background-color: rgba(255, 255, 255, 0.12); border: none; margin: 0px;");
    rootLayout->addWidget(sep);

    // Diagnostic grid rows
    auto *grid = new QGridLayout();
    grid->setContentsMargins(0, 2, 0, 0);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(5);
    grid->setColumnMinimumWidth(0, 135);

    const QString labelStyle = "color: #aaaaaa; font-family: 'Roboto Mono', 'Consolas', 'Courier New', monospace; "
                               "font-size: 11px; background: transparent;";
    const QString valStyle = "color: #ffffff; font-family: 'Roboto Mono', 'Consolas', 'Courier New', monospace; "
                             "font-size: 11px; font-weight: bold; background: transparent;";

    auto addRow = [&](int row, const QString &labelTxt, QLabel *&valMember) {
        auto *lbl = new QLabel(labelTxt, this);
        lbl->setStyleSheet(labelStyle);
        grid->addWidget(lbl, row, 0, Qt::AlignLeft);

        valMember = new QLabel("—", this);
        valMember->setStyleSheet(valStyle);
        valMember->setTextInteractionFlags(Qt::TextSelectableByMouse);
        grid->addWidget(valMember, row, 1, Qt::AlignLeft);
    };

    addRow(0, "Video ID / sCPN", m_valVideoIdCpn);
    addRow(1, "Viewport / Frames", m_valViewportFrames);
    addRow(2, "Current / Optimal Res", m_valCurrentOptimalRes);
    addRow(3, "Volume / Normalized", m_valVolume);
    addRow(4, "Codecs", m_valCodecs);
    addRow(5, "Color", m_valColor);
    addRow(6, "Connection Speed", m_valConnectionSpeed);
    addRow(7, "Network Activity", m_valNetworkActivity);
    addRow(8, "Buffer Health", m_valBufferHealth);
    addRow(9, "Date", m_valDate);

    rootLayout->addLayout(grid);
}

void StatsForNerdsWidget::setVideoContext(const std::string &videoId, const std::string &videoItag,
                                         const std::string &audioItag, const QString &optimalRes) {
    m_videoId = videoId;
    m_videoItag = videoItag;
    m_audioItag = audioItag;
    if (!optimalRes.isEmpty()) {
        m_optimalRes = optimalRes;
    }
    generateCpn();
    if (isVisible()) {
        refreshStats();
    }
}

void StatsForNerdsWidget::setViewportDimensions(int w, int h, qreal dpr) {
    m_viewportW = w;
    m_viewportH = h;
    m_dpr = dpr;
    if (isVisible()) {
        refreshStats();
    }
}

void StatsForNerdsWidget::clampToParent() {
    if (!parentWidget()) {
        return;
    }
    int maxX = std::max(0, parentWidget()->width() - width());
    int maxY = std::max(0, parentWidget()->height() - height());
    int curX = std::clamp(x(), 0, maxX);
    int curY = std::clamp(y(), 0, maxY);
    move(curX, curY);
}

void StatsForNerdsWidget::refreshStats() {
    PlayerPlaybackStats stats;
    int curVol = 100;
    double curPos = 0.0;
    if (m_player) {
        stats = m_player->getPlaybackStats();
        curVol = m_player->getVolume();
        curPos = m_player->getPosition();
    }

    // 1. Video ID / sCPN
    QString idStr = m_videoId.empty() ? "none" : QString::fromStdString(m_videoId);
    m_valVideoIdCpn->setText(idStr + " / " + QString::fromStdString(m_cpn));

    // 2. Viewport / Frames
    QString vpStr;
    if (m_viewportW > 0 && m_viewportH > 0) {
        if (std::abs(m_dpr - 1.0) > 0.01) {
            vpStr = QString("%1x%2*%3").arg(m_viewportW).arg(m_viewportH).arg(m_dpr, 0, 'f', 1);
        } else {
            vpStr = QString("%1x%2").arg(m_viewportW).arg(m_viewportH);
        }
    } else {
        vpStr = "—";
    }

    int64_t totalFrames = 0;
    if (stats.fps > 0.0 && curPos > 0.0) {
        totalFrames = static_cast<int64_t>(std::round(stats.fps * curPos));
    }
    m_valViewportFrames->setText(QString("%1 / %2 dropped of %3").arg(vpStr).arg(stats.droppedFrames).arg(totalFrames));

    // 3. Current / Optimal Res
    QString curResStr;
    if (stats.videoWidth > 0 && stats.videoHeight > 0) {
        int fpsInt = static_cast<int>(std::round(stats.fps));
        if (fpsInt > 0) {
            curResStr = QString("%1x%2@%3").arg(stats.videoWidth).arg(stats.videoHeight).arg(fpsInt);
        } else {
            curResStr = QString("%1x%2").arg(stats.videoWidth).arg(stats.videoHeight);
        }
    } else {
        curResStr = "—";
    }
    m_valCurrentOptimalRes->setText(curResStr + " / " + m_optimalRes);

    // 4. Volume / Normalized
    m_valVolume->setText(QString("%1%/%2%").arg(curVol).arg(curVol));

    // 5. Codecs
    std::string vCodec = stats.videoCodec.empty() ? "—" : stats.videoCodec;
    if (!m_videoItag.empty()) {
        vCodec += " (" + m_videoItag + ")";
    }
    std::string aCodec = stats.audioCodec.empty() ? "—" : stats.audioCodec;
    if (!m_audioItag.empty()) {
        aCodec += " (" + m_audioItag + ")";
    }
    m_valCodecs->setText(QString::fromStdString(vCodec + " / " + aCodec));

    // 6. Color
    std::string colorM = stats.colorMatrix.empty() ? "bt709" : stats.colorMatrix;
    std::string colorP = stats.colorPrimaries.empty() ? "bt709" : stats.colorPrimaries;
    m_valColor->setText(QString::fromStdString(colorM + " / " + colorP));

    // 7. Connection Speed
    int64_t totalBitrateKbps = (stats.videoBitrate + stats.audioBitrate) / 1000;
    m_valConnectionSpeed->setText(QString::number(std::max<int64_t>(0, totalBitrateKbps)) + " Kbps");

    // 8. Network Activity
    m_valNetworkActivity->setText("0 KB");

    // 9. Buffer Health
    m_valBufferHealth->setText(QString::number(std::max(0.0, stats.bufferSeconds), 'f', 2) + " s");

    // 10. Date
    m_valDate->setText(QDateTime::currentDateTime().toString("ddd MMM dd yyyy hh:mm:ss"));
}

void StatsForNerdsWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_isDragging = true;
        m_dragOffset = event->pos();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void StatsForNerdsWidget::mouseMoveEvent(QMouseEvent *event) {
    if (m_isDragging && (event->buttons() & Qt::LeftButton)) {
        if (parentWidget()) {
            QPoint newPos = mapToParent(event->pos() - m_dragOffset);
            int maxX = std::max(0, parentWidget()->width() - width());
            int maxY = std::max(0, parentWidget()->height() - height());
            move(std::clamp(newPos.x(), 0, maxX), std::clamp(newPos.y(), 0, maxY));
        }
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void StatsForNerdsWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_isDragging = false;
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void StatsForNerdsWidget::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    adjustSize();
    clampToParent();
    refreshStats();
    m_refreshTimer->start();
}

void StatsForNerdsWidget::hideEvent(QHideEvent *event) {
    m_refreshTimer->stop();
    QWidget::hideEvent(event);
}

