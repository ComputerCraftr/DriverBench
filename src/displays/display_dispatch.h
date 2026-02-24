#ifndef DRIVERBENCH_DISPLAY_DISPATCH_H
#define DRIVERBENCH_DISPLAY_DISPATCH_H

typedef struct db_cli_config db_cli_config_t;

typedef enum {
    DB_API_CPU = 0,
    DB_API_OPENGL = 1,
    DB_API_VULKAN = 2,
} db_api_t;

typedef enum {
    DB_DISPLAY_GLFW_WINDOW = 0,
    DB_DISPLAY_LINUX_KMS_ATOMIC = 1,
    DB_DISPLAY_OFFSCREEN = 2,
} db_display_t;

typedef enum {
    DB_GL_RENDERER_GL1_5_GLES1_1 = 0,
    DB_GL_RENDERER_GL3_3 = 1,
} db_gl_renderer_t;

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

static inline int db_dispatch_api_is_compiled(db_api_t api) {
    if (api == DB_API_CPU) {
        return 1;
    }
    if (api == DB_API_OPENGL) {
#ifdef DB_HAS_OPENGL_API
        return 1;
#else
        return 0;
#endif
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
    if (display == DB_DISPLAY_OFFSCREEN) {
        return 1;
    }
    if (display == DB_DISPLAY_GLFW_WINDOW) {
#ifdef DB_HAS_GLFW
        return 1;
#else
        return 0;
#endif
    }
    if (display == DB_DISPLAY_LINUX_KMS_ATOMIC) {
#ifdef DB_HAS_LINUX_KMS_ATOMIC
        return 1;
#else
        return 0;
#endif
    }
    return 0;
}

static inline int db_dispatch_display_supports_api(db_display_t display,
                                                   db_api_t api) {
    if ((db_dispatch_display_is_compiled(display) == 0) ||
        (db_dispatch_api_is_compiled(api) == 0)) {
        return 0;
    }
    if (display == DB_DISPLAY_LINUX_KMS_ATOMIC) {
        return (api == DB_API_CPU) || (api == DB_API_OPENGL);
    }
    if (display == DB_DISPLAY_OFFSCREEN) {
#ifdef DB_HAS_GLFW
        return 1;
#else
        return (api == DB_API_CPU);
#endif
    }
    if (display == DB_DISPLAY_GLFW_WINDOW) {
        if (api == DB_API_CPU) {
#ifdef DB_HAS_OPENGL_API
            return 1;
#else
            return 0;
#endif
        }
        if (api == DB_API_OPENGL) {
#ifdef DB_HAS_OPENGL_API
            return 1;
#else
            return 0;
#endif
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
    return 0;
}

static inline int db_dispatch_renderer_is_compiled(db_gl_renderer_t renderer) {
    if (renderer == DB_GL_RENDERER_GL1_5_GLES1_1) {
#ifdef DB_HAS_OPENGL_API
        return 1;
#else
        return 0;
#endif
    }
    if (renderer == DB_GL_RENDERER_GL3_3) {
#ifdef DB_HAS_OPENGL_DESKTOP
        return 1;
#else
        return 0;
#endif
    }
    return 0;
}

static inline int db_dispatch_display_has_any_api(db_display_t display) {
    return (db_dispatch_display_supports_api(display, DB_API_VULKAN) != 0) ||
           (db_dispatch_display_supports_api(display, DB_API_OPENGL) != 0) ||
           (db_dispatch_display_supports_api(display, DB_API_CPU) != 0);
}

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
