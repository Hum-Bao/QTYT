#include "MpvGlWidget.h"

#include "../player/MpvApi.h"
#include "../player/PlayerController.h"
#include "../utils/Logger.h"

#include <QByteArray>
#include <QMetaObject>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>
#include <QTimer>
#include <cmath>
#include <mpv/render.h>
#include <mpv/render_gl.h>

namespace {
void *getProcAddressCallback(void *ctx, const char *name) {
    if (!name || name[0] == '\0')
        return nullptr;
    QOpenGLContext *glCtx = QOpenGLContext::currentContext();
    if (!glCtx) {
        auto *widget = static_cast<MpvGlWidget *>(ctx);
        if (widget)
            glCtx = widget->context();
    }
    if (!glCtx)
        return nullptr;
    return reinterpret_cast<void *>(glCtx->getProcAddress(QByteArray(name)));
}
}  // namespace

MpvGlWidget::MpvGlWidget(QWidget *parent) : QOpenGLWidget(parent) {
    QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
    fmt.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    fmt.setSwapInterval(1);
    setFormat(fmt);
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
    setCursor(Qt::PointingHandCursor);

    connect(this, &QOpenGLWidget::frameSwapped, this, &MpvGlWidget::onFrameSwapped);

    m_clickTimer = new QTimer(this);
    m_clickTimer->setSingleShot(true);
    connect(m_clickTimer, &QTimer::timeout, this, &MpvGlWidget::onClickTimerTimeout);
}

MpvGlWidget::~MpvGlWidget() {
    if (m_clickTimer) {
        m_clickTimer->stop();
    }
    destroyRenderContext();
}

void MpvGlWidget::setPlayer(PlayerController *player) {
    m_player = player;
    if (m_glInitialized && m_player && !m_renderCtx) {
        initRenderContext();
    }
}

void MpvGlWidget::onMpvUpdate(void *ctx) {
    auto *widget = static_cast<MpvGlWidget *>(ctx);
    if (widget) {
        QMetaObject::invokeMethod(widget, &MpvGlWidget::handleMpvUpdate, Qt::QueuedConnection);
    }
}

void MpvGlWidget::handleMpvUpdate() {
    if (!m_renderCtx || !m_player)
        return;
    const auto &api = lightyt::getMpvApi();
    if (api.render_context_update) {
        uint64_t flags = api.render_context_update(m_renderCtx);
        if (flags & MPV_RENDER_UPDATE_FRAME) {
            update();
        }
    } else {
        update();
    }
}

void MpvGlWidget::onFrameSwapped() {
    if (!m_renderCtx)
        return;
    const auto &api = lightyt::getMpvApi();
    if (api.render_context_report_swap) {
        api.render_context_report_swap(m_renderCtx);
    }
}

bool MpvGlWidget::initRenderContext() {
    if (m_renderCtx != nullptr) {
        return true;
    }
    if (!m_glInitialized || !m_player) {
        return false;
    }

    mpv_handle *mpv = m_player->getMpvHandle();
    if (!mpv) {
        return false;
    }

    auto &api = lightyt::getMpvApi();
    if (!api.hasRenderContext()) {
        LOG_WARN("[MpvGlWidget] mpv shared library does not export render context API.");
        return false;
    }

    makeCurrent();

    mpv_opengl_init_params glInitParams{.get_proc_address = getProcAddressCallback, .get_proc_address_ctx = this};

    mpv_render_param params[] = {{MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
                                 {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInitParams},
                                 {MPV_RENDER_PARAM_INVALID, nullptr}};

    int err = api.render_context_create(&m_renderCtx, mpv, params);
    if (err < 0 || m_renderCtx == nullptr) {
        LOG_WARN("[MpvGlWidget] Failed to create mpv_render_context (error code: {})", err);
        m_renderCtx = nullptr;
        doneCurrent();
        return false;
    }

    api.render_context_set_update_callback(m_renderCtx, onMpvUpdate, this);
    doneCurrent();

    LOG_INFO("[MpvGlWidget] Successfully initialized mpv_render_context with QOpenGLWidget.");
    update();
    return true;
}

void MpvGlWidget::destroyRenderContext() {
    if (m_renderCtx != nullptr) {
        if (isValid()) {
            makeCurrent();
        }
        auto &api = lightyt::getMpvApi();
        if (api.render_context_set_update_callback != nullptr) {
            api.render_context_set_update_callback(m_renderCtx, nullptr, nullptr);
        }
        if (api.render_context_free != nullptr) {
            api.render_context_free(m_renderCtx);
        }
        m_renderCtx = nullptr;
        if (isValid()) {
            doneCurrent();
        }
    }
}

void MpvGlWidget::initializeGL() {
    m_glInitialized = true;
    auto *f = QOpenGLContext::currentContext() ? QOpenGLContext::currentContext()->functions() : nullptr;
    if (f) {
        f->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    }
    if (m_player && !m_renderCtx) {
        initRenderContext();
    }
}

void MpvGlWidget::paintGL() {
    auto *f = QOpenGLContext::currentContext() ? QOpenGLContext::currentContext()->functions() : nullptr;
    if (!m_renderCtx) {
        if (f) {
            f->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            f->glClear(GL_COLOR_BUFFER_BIT);
        }
        return;
    }

    auto &api = lightyt::getMpvApi();
    if (api.render_context_render == nullptr) {
        return;
    }

    if (api.render_context_update != nullptr) {
        (void)api.render_context_update(m_renderCtx);
    }

    qreal dpr = devicePixelRatio();
    int fboWidth = static_cast<int>(std::lround(width() * dpr));
    int fboHeight = static_cast<int>(std::lround(height() * dpr));
    if (fboWidth <= 0 || fboHeight <= 0) {
        return;
    }

    int fboId = static_cast<int>(defaultFramebufferObject());
    if (fboId < 0) {
        return;
    }

    if (f) {
        while (f->glGetError() != GL_NO_ERROR) {
        }
        f->glViewport(0, 0, fboWidth, fboHeight);
    }

    mpv_opengl_fbo fbo{.fbo = fboId, .w = fboWidth, .h = fboHeight, .internal_format = 0};

    int flipY = 1;
    int blockForTargetTime = 0;
    mpv_render_param params[] = {{MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
                                 {MPV_RENDER_PARAM_FLIP_Y, &flipY},
                                 {MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &blockForTargetTime},
                                 {MPV_RENDER_PARAM_INVALID, nullptr}};

    int err = api.render_context_render(m_renderCtx, params);
    if (err < 0) {
        LOG_WARN("[MpvGlWidget] mpv_render_context_render returned: {}", err);
    }
}

void MpvGlWidget::resizeGL(int w, int h) {
    if (w > 0 && h > 0) {
        auto *f = QOpenGLContext::currentContext() ? QOpenGLContext::currentContext()->functions() : nullptr;
        if (f) {
            qreal dpr = devicePixelRatio();
            f->glViewport(0, 0, static_cast<int>(std::lround(w * dpr)), static_cast<int>(std::lround(h * dpr)));
        }
        update();
    }
}

void MpvGlWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        event->accept();
        return;
    }
    QOpenGLWidget::mousePressEvent(event);
}

void MpvGlWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_waitingForDouble = true;
        m_clickTimer->start(220);
        event->accept();
        return;
    }
    QOpenGLWidget::mouseReleaseEvent(event);
}

void MpvGlWidget::mouseDoubleClickEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_waitingForDouble = false;
        m_clickTimer->stop();
        emit doubleClicked();
        event->accept();
        return;
    }
    QOpenGLWidget::mouseDoubleClickEvent(event);
}

void MpvGlWidget::onClickTimerTimeout() {
    if (m_waitingForDouble) {
        m_waitingForDouble = false;
        emit clicked();
    }
}
