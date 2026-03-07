#ifndef DRIVERBENCH_DISPLAY_DISPATCH_H
#define DRIVERBENCH_DISPLAY_DISPATCH_H

#include <stddef.h>

#include "../driverbench_config.h"

typedef struct {
    int compiled;
    int supports_cpu;
    int supports_opengl;
    int supports_vulkan;
    int supports_gl1;
    int supports_gl3;
} db_display_backend_capabilities_t;

#define DB_DISPLAY_CAPABILITY_TEXT_MAX 128U

typedef enum {
    DB_DISPLAY_OFFSCREEN_GL_ROUTE_NONE = 0,
    DB_DISPLAY_OFFSCREEN_GL_ROUTE_GLFW_HIDDEN = 1,
    DB_DISPLAY_OFFSCREEN_GL_ROUTE_GL3_FBO = 2,
} db_display_offscreen_gl_route_t;

static inline const char *db_dispatch_api_name(db_api_t api) {
    if (api == DB_API_CPU) {
        return "CPU";
    }
    if (api == DB_API_OPENGL) {
        return "OpenGL";
    }
    if (api == DB_API_VULKAN) {
        return "Vulkan";
    }
    return "Unknown";
}

static inline const char *
db_dispatch_gl_renderer_name(db_gl_renderer_t renderer) {
    if (renderer == DB_GL_RENDERER_GL1_5_GLES1_1) {
        return "gl1_5_gles1_1";
    }
    if (renderer == DB_GL_RENDERER_GL3_3) {
        return "gl3_3";
    }
    return "unknown";
}

static inline db_display_backend_capabilities_t
db_dispatch_display_capabilities(db_display_t display) {
    db_display_backend_capabilities_t caps = {0};
    if (display == DB_DISPLAY_OFFSCREEN) {
        caps.compiled = 1;
        caps.supports_cpu = 1;
#ifdef DB_HAS_GLFW
        caps.supports_opengl = 1;
        caps.supports_gl1 = 1;
        caps.supports_gl3 = 1;
#endif
        return caps;
    }
    if (display == DB_DISPLAY_GLFW_WINDOW) {
#ifdef DB_HAS_GLFW
        caps.compiled = 1;
        caps.supports_cpu = 1;
        caps.supports_opengl = 1;
        caps.supports_gl1 = 1;
        caps.supports_gl3 = 1;
#ifdef DB_HAS_VULKAN_API
        caps.supports_vulkan = 1;
#endif
#endif
        return caps;
    }
    if (display == DB_DISPLAY_LINUX_KMS_ATOMIC) {
#ifdef DB_HAS_LINUX_KMS_ATOMIC
        caps.compiled = 1;
        caps.supports_cpu = 1;
        caps.supports_opengl = 1;
        caps.supports_gl1 = 1;
        caps.supports_gl3 = 1;
#endif
        return caps;
    }
    return caps;
}

static inline int db_dispatch_api_is_compiled(db_api_t api) {
    if (api == DB_API_CPU) {
        return 1;
    }
    if (api == DB_API_OPENGL) {
        return 1;
    }
    if (api == DB_API_VULKAN) {
#ifdef DB_HAS_VULKAN_API
        return 1;
#else
        return 0;
#endif
    }
    return 0;
}

static inline int db_dispatch_display_is_compiled(db_display_t display) {
    return db_dispatch_display_capabilities(display).compiled;
}

static inline int db_dispatch_display_supports_api(db_display_t display,
                                                   db_api_t api) {
    const db_display_backend_capabilities_t caps =
        db_dispatch_display_capabilities(display);
    if ((caps.compiled == 0) || (db_dispatch_api_is_compiled(api) == 0)) {
        return 0;
    }
    if (api == DB_API_CPU) {
        return caps.supports_cpu;
    }
    if (api == DB_API_OPENGL) {
        return caps.supports_opengl;
    }
    if (api == DB_API_VULKAN) {
        return caps.supports_vulkan;
    }
    return 0;
}

static inline int
db_dispatch_display_supports_gl_renderer(db_display_t display,
                                         db_gl_renderer_t renderer) {
    const db_display_backend_capabilities_t caps =
        db_dispatch_display_capabilities(display);
    if (caps.compiled == 0) {
        return 0;
    }
    if (renderer == DB_GL_RENDERER_GL1_5_GLES1_1) {
        return caps.supports_gl1;
    }
    if (renderer == DB_GL_RENDERER_GL3_3) {
        return caps.supports_gl3;
    }
    return 0;
}

static inline int
db_dispatch_display_supports_backend(db_display_t display, db_api_t api,
                                     db_gl_renderer_t renderer) {
    if (db_dispatch_display_supports_api(display, api) == 0) {
        return 0;
    }
    if (api != DB_API_OPENGL) {
        return 1;
    }
    return db_dispatch_display_supports_gl_renderer(display, renderer);
}

static inline int db_dispatch_display_has_any_api(db_display_t display) {
    const db_display_backend_capabilities_t caps =
        db_dispatch_display_capabilities(display);
    return (caps.supports_vulkan != 0) || (caps.supports_opengl != 0) ||
           (caps.supports_cpu != 0);
}

static inline db_api_t
db_dispatch_display_preferred_auto_api(db_display_t display) {
    const db_display_backend_capabilities_t caps =
        db_dispatch_display_capabilities(display);
    if (caps.supports_vulkan != 0) {
        return DB_API_VULKAN;
    }
    if (caps.supports_opengl != 0) {
        return DB_API_OPENGL;
    }
    return DB_API_CPU;
}

static inline db_display_offscreen_gl_route_t
db_dispatch_offscreen_gl_route(db_gl_renderer_t renderer) {
    if (renderer == DB_GL_RENDERER_GL1_5_GLES1_1) {
#ifdef DB_HAS_GLFW
        return DB_DISPLAY_OFFSCREEN_GL_ROUTE_GLFW_HIDDEN;
#else
        return DB_DISPLAY_OFFSCREEN_GL_ROUTE_NONE;
#endif
    }
    if (renderer == DB_GL_RENDERER_GL3_3) {
#ifdef DB_HAS_GLFW
        return DB_DISPLAY_OFFSCREEN_GL_ROUTE_GL3_FBO;
#else
        return DB_DISPLAY_OFFSCREEN_GL_ROUTE_NONE;
#endif
    }
    return DB_DISPLAY_OFFSCREEN_GL_ROUTE_NONE;
}

int db_dispatch_format_display_capabilities(db_display_t display, char *buffer,
                                            size_t buffer_size);
void db_dispatch_log_display_capabilities(const char *backend,
                                          db_display_t display);
void db_dispatch_validate_backend_or_fail(const char *backend,
                                          db_display_t display, db_api_t api,
                                          db_gl_renderer_t renderer);

int db_run_display(db_display_t display, db_api_t api,
                   db_gl_renderer_t renderer, const char *kms_card_path,
                   const db_cli_config_t *cfg);
int db_run_display_auto(db_display_t display, db_gl_renderer_t renderer,
                        const char *kms_card_path, const db_cli_config_t *cfg);
int db_run_glfw_window(db_api_t api, db_gl_renderer_t renderer,
                       const db_cli_config_t *cfg);
int db_run_linux_kms_atomic(db_api_t api, db_gl_renderer_t renderer,
                            const char *card_path, const db_cli_config_t *cfg);
int db_run_offscreen(db_api_t api, db_gl_renderer_t renderer,
                     const db_cli_config_t *cfg);

#endif
