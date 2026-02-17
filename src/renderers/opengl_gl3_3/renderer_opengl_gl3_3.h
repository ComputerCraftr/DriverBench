#ifndef DRIVERBENCH_RENDERER_OPENGL_GL3_3_H
#define DRIVERBENCH_RENDERER_OPENGL_GL3_3_H

#include <stdint.h>

void db_renderer_opengl_gl3_3_init(const char *vert_shader_path,
                                   const char *frag_shader_path);
void db_renderer_opengl_gl3_3_render_frame(double time_s);
void db_renderer_opengl_gl3_3_shutdown(void);
const char *db_renderer_opengl_gl3_3_capability_mode(void);
uint32_t db_renderer_opengl_gl3_3_work_unit_count(void);

#endif
