#ifndef DRIVERBENCH_RENDERER_OPENGL_GL3_3_H
#define DRIVERBENCH_RENDERER_OPENGL_GL3_3_H

#include <stdint.h>

void db_renderer_opengl_gl3_3_init(void);
void db_renderer_opengl_gl3_3_render_frame(uint32_t frame_index,
                                           int viewport_width_px,
                                           int viewport_height_px);
void db_renderer_opengl_gl3_3_shutdown(void);
const char *db_renderer_opengl_gl3_3_capability_mode(void);
uint32_t db_renderer_opengl_gl3_3_work_unit_count(void);
uint64_t db_renderer_opengl_gl3_3_state_hash(void);
void db_renderer_opengl_gl3_3_draw_stats(uint64_t *full_draw_frames,
                                         uint64_t *dirty_draw_frames);

#endif
