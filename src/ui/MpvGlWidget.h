#pragma once

#include <QOpenGLWidget>
#include <QPointer>
#include <cstdint>

class PlayerController;

typedef struct mpv_render_context mpv_render_context;
typedef struct mpv_handle mpv_handle;

class MpvGlWidget : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit MpvGlWidget(QWidget *parent = nullptr);
    ~MpvGlWidget() override;

    // Attach to player controller
    void setPlayer(PlayerController *player);

    // Initialize/destroy render context
    bool initRenderContext();
    void destroyRenderContext();

    [[nodiscard]] bool hasActiveRenderContext() const noexcept { return m_renderCtx != nullptr; }

signals:
    void clicked();
    void doubleClicked();

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private slots:
    void handleMpvUpdate();
    void onFrameSwapped();
    void onClickTimerTimeout();

private:
    static void onMpvUpdate(void *ctx);

    mpv_render_context *m_renderCtx = nullptr;
    PlayerController *m_player = nullptr;
    bool m_glInitialized = false;
    class QTimer *m_clickTimer = nullptr;
    bool m_waitingForDouble = false;
};
