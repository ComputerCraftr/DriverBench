#ifndef DRIVERBENCH_DISPLAY_GL_RENDERER_SELECT_COMMON_H
#define DRIVERBENCH_DISPLAY_GL_RENDERER_SELECT_COMMON_H

#include <stdint.h>

#include "../renderers/opengl_gl1_5_gles1_1/renderer_opengl_gl1_5_gles1_1.h"
#include "../renderers/opengl_gl3_3/renderer_opengl_gl3_3.h"
#include "../renderers/renderer_identity.h"
#include "display_types.h"

typedef struct {
    db_gl_renderer_t renderer;
    const char *renderer_name;
    void (*draw_stats)(uint64_t *full_draw_frames, uint64_t *dirty_draw_frames);
    void (*init)(void);
    const char *(*runtime_capability_mode)(void);
    uint64_t (*state_hash)(void);
    void (*shutdown)(void);
    uint32_t (*work_unit_count)(void);
} db_display_gl_renderer_ops_t;

static inline void
db_display_gl_render_frame_glfw(db_gl_renderer_t renderer, uint32_t frame_index,
                                int viewport_width_px, int viewport_height_px,
                                uint32_t preserved_framebuffer_count) {
    if (renderer == DB_GL_RENDERER_GL1_5_GLES1_1) {
        db_renderer_opengl_gl1_5_gles1_1_render_frame(
            frame_index, viewport_width_px, viewport_height_px,
            preserved_framebuffer_count);
        return;
    }
    db_renderer_opengl_gl3_3_render_frame(frame_index, viewport_width_px,
                                          viewport_height_px);
}

static inline void
db_display_gl_render_frame_kms(db_gl_renderer_t renderer, uint32_t frame_index,
                               uint32_t preserved_framebuffer_count) {
    if (renderer == DB_GL_RENDERER_GL1_5_GLES1_1) {
        db_renderer_opengl_gl1_5_gles1_1_render_frame(
            frame_index, 0, 0, preserved_framebuffer_count);
        return;
    }
    db_renderer_opengl_gl3_3_render_frame(frame_index, 0, 0);
}

static inline void
db_display_gl_render_frame(db_gl_renderer_t renderer, uint32_t frame_index,
                           int viewport_width_px, int viewport_height_px,
                           uint32_t preserved_framebuffer_count, int use_kms) {
    if (use_kms != 0) {
        db_display_gl_render_frame_kms(renderer, frame_index,
                                       preserved_framebuffer_count);
        return;
    }
    db_display_gl_render_frame_glfw(renderer, frame_index, viewport_width_px,
                                    viewport_height_px,
                                    preserved_framebuffer_count);
}

static inline uint32_t
db_display_gl_default_preserved_framebuffer_count(db_gl_renderer_t renderer) {
    return (renderer == DB_GL_RENDERER_GL1_5_GLES1_1) ? 2U : 0U;
}

static inline db_display_gl_renderer_ops_t
db_display_gl_select_renderer_ops(db_gl_renderer_t renderer) {
    if (renderer == DB_GL_RENDERER_GL1_5_GLES1_1) {
        return (db_display_gl_renderer_ops_t){
            .renderer = renderer,
            .renderer_name = db_renderer_name_opengl_gl1_5_gles1_1(),
            .draw_stats = db_renderer_opengl_gl1_5_gles1_1_draw_stats,
            .init = db_renderer_opengl_gl1_5_gles1_1_init,
            .runtime_capability_mode =
                db_renderer_opengl_gl1_5_gles1_1_capability_mode,
            .state_hash = db_renderer_opengl_gl1_5_gles1_1_state_hash,
            .shutdown = db_renderer_opengl_gl1_5_gles1_1_shutdown,
            .work_unit_count = db_renderer_opengl_gl1_5_gles1_1_work_unit_count,
        };
    }
    return (db_display_gl_renderer_ops_t){
        .renderer = renderer,
        .renderer_name = db_renderer_name_opengl_gl3_3(),
        .draw_stats = db_renderer_opengl_gl3_3_draw_stats,
        .init = db_renderer_opengl_gl3_3_init,
        .runtime_capability_mode = db_renderer_opengl_gl3_3_capability_mode,
        .state_hash = db_renderer_opengl_gl3_3_state_hash,
        .shutdown = db_renderer_opengl_gl3_3_shutdown,
        .work_unit_count = db_renderer_opengl_gl3_3_work_unit_count,
    };
}

#endif
