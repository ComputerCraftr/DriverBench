#include "renderer_opengl_gl3_3.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "../../core/db_core.h"
#include "../../displays/bench_config.h"

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#ifdef __has_include
#if __has_include(<GL/glcorearb.h>)
#include <GL/glcorearb.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#endif
#else
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#endif
#endif

#define BACKEND_NAME "renderer_opengl_gl3_3"
#define SHADER_LOG_MSG_CAPACITY 1024
#define TRI_VERTS_PER_BAND 6U
#define POS_FLOATS 2U
#define COLOR_FLOATS 3U
#define VERT_FLOATS (POS_FLOATS + COLOR_FLOATS)
#define VERT_TRI_IDX_2 2U
#define VERT_TRI_IDX_3 3U
#define VERT_TRI_IDX_4 4U
#define VERT_TRI_IDX_5 5U
#define ATTR_POSITION_LOC 0U
#define ATTR_COLOR_LOC 1U
#define ATTR_POSITION_COMPONENTS 2
#define ATTR_COLOR_COMPONENTS 3
#define BUFFER_OFFSET_BYTES(byte_count)                                        \
    ((const void *)((const char *)NULL + (byte_count)))
#define failf(...) db_failf(BACKEND_NAME, __VA_ARGS__)

typedef struct {
    GLuint vao;
    GLuint vbo;
    GLuint program;
    float vertices[BENCH_BANDS * TRI_VERTS_PER_BAND * VERT_FLOATS];
} ShaderRendererState;

static ShaderRendererState g_state = {0};

static GLuint compile_shader(GLenum shader_type, const char *source) {
    GLuint shader = glCreateShader(shader_type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_FALSE) {
        char log_msg[SHADER_LOG_MSG_CAPACITY];
        GLsizei msg_len = 0;
        glGetShaderInfoLog(shader, (GLsizei)sizeof(log_msg), &msg_len, log_msg);
        failf("Shader compile failed (%u): %.*s", (unsigned)shader_type,
              (int)msg_len, log_msg);
    }
    return shader;
}

static GLuint build_program_from_files(const char *vert_shader_path,
                                       const char *frag_shader_path) {
    char *vert_src = db_read_text_file_or_fail(BACKEND_NAME, vert_shader_path);
    char *frag_src = db_read_text_file_or_fail(BACKEND_NAME, frag_shader_path);

    GLuint vert = compile_shader(GL_VERTEX_SHADER, vert_src);
    GLuint frag = compile_shader(GL_FRAGMENT_SHADER, frag_src);
    free(vert_src);
    free(frag_src);

    GLuint program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);
    glDeleteShader(vert);
    glDeleteShader(frag);

    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok == GL_FALSE) {
        char log_msg[SHADER_LOG_MSG_CAPACITY];
        GLsizei msg_len = 0;
        glGetProgramInfoLog(program, (GLsizei)sizeof(log_msg), &msg_len,
                            log_msg);
        failf("Program link failed: %.*s", (int)msg_len, log_msg);
    }
    return program;
}

static void fill_vertices(float *verts, double time_s) {
    for (uint32_t band_index = 0; band_index < BENCH_BANDS; band_index++) {
        const float band_f = (float)band_index;
        float x0 = ((2.0F * (float)band_index) / (float)BENCH_BANDS) - 1.0F;
        float x1 =
            ((2.0F * (float)(band_index + 1U)) / (float)BENCH_BANDS) - 1.0F;
        float pulse =
            BENCH_PULSE_BASE_F +
            (BENCH_PULSE_AMP_F * sinf((float)((time_s * BENCH_PULSE_FREQ_F) +
                                              (band_f * BENCH_PULSE_PHASE_F))));
        float color_r =
            pulse * (BENCH_COLOR_R_BASE_F +
                     BENCH_COLOR_R_SCALE_F * band_f / (float)BENCH_BANDS);
        float color_g = pulse * BENCH_COLOR_G_SCALE_F;
        float color_b = 1.0F - color_r;

        const float p0[VERT_FLOATS] = {x0, -1.0F, color_r, color_g, color_b};
        const float p1[VERT_FLOATS] = {x1, -1.0F, color_r, color_g, color_b};
        const float p2[VERT_FLOATS] = {x1, 1.0F, color_r, color_g, color_b};
        const float p3[VERT_FLOATS] = {x0, 1.0F, color_r, color_g, color_b};

        uint32_t base = band_index * TRI_VERTS_PER_BAND * VERT_FLOATS;
        for (uint32_t i = 0; i < VERT_FLOATS; i++) {
            verts[base + i] = p0[i];
            verts[base + VERT_FLOATS + i] = p1[i];
            verts[base + (VERT_TRI_IDX_2 * VERT_FLOATS) + i] = p2[i];
            verts[base + (VERT_TRI_IDX_3 * VERT_FLOATS) + i] = p0[i];
            verts[base + (VERT_TRI_IDX_4 * VERT_FLOATS) + i] = p2[i];
            verts[base + (VERT_TRI_IDX_5 * VERT_FLOATS) + i] = p3[i];
        }
    }
}

void db_renderer_opengl_gl3_3_init(const char *vert_shader_path,
                                   const char *frag_shader_path) {
    glGenVertexArrays(1, &g_state.vao);
    glGenBuffers(1, &g_state.vbo);
    glBindVertexArray(g_state.vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_state.vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(g_state.vertices),
                 g_state.vertices, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(ATTR_POSITION_LOC);
    glVertexAttribPointer(ATTR_POSITION_LOC, ATTR_POSITION_COMPONENTS, GL_FLOAT,
                          GL_FALSE, (GLsizei)(VERT_FLOATS * sizeof(float)),
                          (const void *)0);
    glEnableVertexAttribArray(ATTR_COLOR_LOC);
    glVertexAttribPointer(ATTR_COLOR_LOC, ATTR_COLOR_COMPONENTS, GL_FLOAT,
                          GL_FALSE, (GLsizei)(VERT_FLOATS * sizeof(float)),
                          BUFFER_OFFSET_BYTES(POS_FLOATS * sizeof(float)));

    g_state.program =
        build_program_from_files(vert_shader_path, frag_shader_path);
    glUseProgram(g_state.program);
}

void db_renderer_opengl_gl3_3_render_frame(double time_s) {
    fill_vertices(g_state.vertices, time_s);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)sizeof(g_state.vertices),
                    g_state.vertices);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(BENCH_BANDS * TRI_VERTS_PER_BAND));
}

void db_renderer_opengl_gl3_3_shutdown(void) {
    glDeleteProgram(g_state.program);
    glDeleteBuffers(1, &g_state.vbo);
    glDeleteVertexArrays(1, &g_state.vao);
    g_state = (ShaderRendererState){0};
}
