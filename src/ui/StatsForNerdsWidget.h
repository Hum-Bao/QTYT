#pragma once

#include <QDateTime>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QShowEvent>
#include <QString>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <cstdint>
#include <string>

class PlayerController;

class StatsForNerdsWidget : public QWidget {
    Q_OBJECT
public:
    explicit StatsForNerdsWidget(QWidget *parent = nullptr);
    ~StatsForNerdsWidget() override = default;

    void setPlayerController(PlayerController *controller) noexcept { m_player = controller; }
    void setVideoContext(const std::string &videoId, const std::string &videoItag, const std::string &audioItag,
                         const QString &optimalRes);
    void setViewportDimensions(int w, int h, qreal dpr = 1.0);
    void clampToParent();
    void refreshStats();

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

signals:
    void closed();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void setupUi();
    void generateCpn();

    PlayerController *m_player = nullptr;
    QTimer *m_refreshTimer = nullptr;

    // Stream & context data
    std::string m_videoId;
    std::string m_cpn;
    std::string m_videoItag;
    std::string m_audioItag;
    QString m_optimalRes{"1920x1080@60"};

    int m_viewportW = 0;
    int m_viewportH = 0;
    qreal m_dpr = 1.0;
    int64_t m_lastConnectionSpeedKbps = 0;

    // Drag tracking
    bool m_isDragging = false;
    QPoint m_dragOffset;

    // UI Labels (Values)
    QLabel *m_valVideoIdCpn = nullptr;
    QLabel *m_valViewportFrames = nullptr;
    QLabel *m_valCurrentOptimalRes = nullptr;
    QLabel *m_valVolume = nullptr;
    QLabel *m_valCodecs = nullptr;
    QLabel *m_valColor = nullptr;
    QLabel *m_valConnectionSpeed = nullptr;
    QLabel *m_valNetworkActivity = nullptr;
    QLabel *m_valBufferHealth = nullptr;
    QLabel *m_valDate = nullptr;
};

