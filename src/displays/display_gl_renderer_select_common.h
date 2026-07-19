#ifndef DRIVERBENCH_DISPLAY_GL_RENDERER_SELECT_COMMON_H
#define DRIVERBENCH_DISPLAY_GL_RENDERER_SELECT_COMMON_H

#include <stdint.h>

#include "../core/db_core.h"
#include "../core/db_frame_plan.h"
#include "../renderers/gl_common.h"
#include "../renderers/opengl_gl1_5_gles1_1/gl1_renderer.h"
#include "../renderers/opengl_gl3_3/gl3_renderer.h"
#include "../renderers/renderer_identity.h"
#include "display_presentation_policy.h"
#include "display_types.h"

typedef struct {
    db_gl_renderer_t renderer;
    const char *renderer_name;
    void (*draw_stats)(db_renderer_draw_path_stats_t *stats);
    void (*execution_report)(db_render_execution_report_t *report);
    const db_renderer_qualification_ops_t *qualification_ops;
    void (*init)(const db_renderer_runtime_contract_t *resolved_runtime);
    const char *(*runtime_capability_mode)(void);
    uint64_t (*state_hash)(void);
    uint64_t (*working_hash)(void);
    void (*shutdown)(void);
    uint32_t (*work_unit_count)(void);
} db_display_gl_renderer_ops_t;

static inline void
db_display_gl_render_frame(db_gl_renderer_t renderer,
                           const db_frame_plan_t *plan,
                           const db_gl_presentation_frame_t *presentation) {
    const int viewport_width_px =
        (presentation != NULL)
            ? db_checked_u32_to_int("display_gl_renderer", "viewport_width",
                                    presentation->destination_width)
            : 0;
    const int viewport_height_px =
        (presentation != NULL)
            ? db_checked_u32_to_int("display_gl_renderer", "viewport_height",
                                    presentation->destination_height)
            : 0;
    const db_pixel_block_view_t damage = (presentation != NULL)
                                             ? presentation->damage
                                             : (db_pixel_block_view_t){0};
    const int force_full_presentation =
        (presentation != NULL) ? presentation->force_full : 1;
    if (renderer == DB_GL_RENDERER_GL1_5_GLES1_1) {
        db_gl1_render_frame(plan, viewport_width_px, viewport_height_px, damage,
                            force_full_presentation);
        return;
    }
    db_gl3_render_frame(plan, viewport_width_px, viewport_height_px);
}

static inline uint32_t db_display_gl_default_preserved_framebuffer_count() {
    return 0U;
}

static inline uint32_t
db_display_gl_max_preserved_framebuffer_count(db_gl_renderer_t renderer) {
    return (renderer == DB_GL_RENDERER_GL1_5_GLES1_1)
               ? DB_GL_MAX_PRESERVED_FRAMEBUFFER_COUNT
               : 0U;
}

static inline db_display_gl_renderer_ops_t
db_display_gl_select_renderer_ops(db_gl_renderer_t renderer) {
    if (renderer == DB_GL_RENDERER_GL1_5_GLES1_1) {
        return (db_display_gl_renderer_ops_t){
            .renderer = renderer,
            .renderer_name = db_renderer_name_opengl_gl1_5_gles1_1(),
            .draw_stats = db_gl1_draw_stats,
            .execution_report = db_gl1_execution_report,
            .qualification_ops = db_gl1_qualification_ops(),
            .init = db_gl1_init,
            .runtime_capability_mode = db_gl1_capability_mode,
            .state_hash = db_gl1_state_hash,
            .working_hash = db_gl1_working_hash,
            .shutdown = db_gl1_shutdown,
            .work_unit_count = db_gl1_work_unit_count,
        };
    }
    return (db_display_gl_renderer_ops_t){
        .renderer = renderer,
        .renderer_name = db_renderer_name_opengl_gl3_3(),
        .draw_stats = db_gl3_draw_stats,
        .execution_report = db_gl3_execution_report,
        .qualification_ops = db_gl3_qualification_ops(),
        .init = db_gl3_init,
        .runtime_capability_mode = db_gl3_capability_mode,
        .state_hash = db_gl3_state_hash,
        .working_hash = db_gl3_working_hash,
        .shutdown = db_gl3_shutdown,
        .work_unit_count = db_gl3_work_unit_count,
    };
}

#endif
