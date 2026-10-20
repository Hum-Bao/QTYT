#ifndef MPV_RENDER_GL_H_
#define MPV_RENDER_GL_H_

#include <mpv/render.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MPV_RENDER_API_TYPE_OPENGL "opengl"

typedef struct mpv_opengl_init_params {
    void *(*get_proc_address)(void *ctx, const char *name);
    void *get_proc_address_ctx;
} mpv_opengl_init_params;

typedef struct mpv_opengl_fbo {
    int fbo;
    int w;
    int h;
    int internal_format;
} mpv_opengl_fbo;

#ifdef __cplusplus
}
#endif

#endif

