#ifndef DRIVERBENCH_RENDERER_OPENGL_GL3_3_H
#define DRIVERBENCH_RENDERER_OPENGL_GL3_3_H

void db_renderer_opengl_gl3_3_init(const char *vert_shader_path,
                                   const char *frag_shader_path);
void db_renderer_opengl_gl3_3_render_frame(double time_s);
void db_renderer_opengl_gl3_3_shutdown(void);

#endif
