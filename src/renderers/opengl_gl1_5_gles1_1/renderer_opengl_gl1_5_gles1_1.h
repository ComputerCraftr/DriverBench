#ifndef DRIVERBENCH_RENDERER_OPENGL_GL1_5_GLES1_1_H
#define DRIVERBENCH_RENDERER_OPENGL_GL1_5_GLES1_1_H

#include <stdint.h>

#include "../renderer_gl_common.h"

void db_renderer_opengl_gl1_5_gles1_1_init(void);
void db_renderer_opengl_gl1_5_gles1_1_render_frame(
    uint32_t frame_index, int viewport_width_px, int viewport_height_px,
    uint32_t preserved_framebuffer_count);
void db_renderer_opengl_gl1_5_gles1_1_shutdown(void);
const char *db_renderer_opengl_gl1_5_gles1_1_capability_mode(void);
uint32_t db_renderer_opengl_gl1_5_gles1_1_work_unit_count(void);
uint64_t db_renderer_opengl_gl1_5_gles1_1_state_hash(void);
void db_renderer_opengl_gl1_5_gles1_1_draw_stats(
    db_renderer_draw_path_stats_t *stats);

#endif
