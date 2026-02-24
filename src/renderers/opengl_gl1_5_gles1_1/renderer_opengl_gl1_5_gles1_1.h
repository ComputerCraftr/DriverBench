#ifndef DRIVERBENCH_RENDERER_OPENGL_GL1_5_GLES1_1_H
#define DRIVERBENCH_RENDERER_OPENGL_GL1_5_GLES1_1_H

#include <stdint.h>

void db_renderer_opengl_gl1_5_gles1_1_init(void);
void db_renderer_opengl_gl1_5_gles1_1_render_frame(double time_s);
void db_renderer_opengl_gl1_5_gles1_1_shutdown(void);
const char *db_renderer_opengl_gl1_5_gles1_1_capability_mode(void);
uint32_t db_renderer_opengl_gl1_5_gles1_1_work_unit_count(void);
uint64_t db_renderer_opengl_gl1_5_gles1_1_state_hash(void);

#endif
