#include "renderer_opengl_gl1_5_gles1_1.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../core/db_core.h"
#include "../renderer_bands_common.h"

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#define BACKEND_NAME "renderer_opengl_gl1_5_gles1_1"
#define TRI_VERTS_PER_BAND DB_BAND_TRI_VERTS_PER_BAND
#define VERT_FLOATS DB_BAND_VERT_FLOATS
#define STRIDE_BYTES ((GLsizei)(sizeof(float) * VERT_FLOATS))
#define POS_OFFSET_FLOATS 0U
#define COLOR_OFFSET_FLOATS 2U
#define MODE_VBO "vbo"
#define MODE_CLIENT_ARRAY "client_array"
#define BASE10 10

typedef struct {
    int use_vbo;
    GLuint vbo;
    float vertices[BENCH_BANDS * TRI_VERTS_PER_BAND * VERT_FLOATS];
} renderer_state_t;

static renderer_state_t g_state = {0};

static int has_gl15_vbo_support(void) {
    const char *version_text = (const char *)glGetString(GL_VERSION);
    if (version_text == NULL) {
        return 0;
    }

    char *parse_end = NULL;
    long major_l = strtol(version_text, &parse_end, BASE10);
    if ((parse_end == version_text) || (*parse_end != '.')) {
        return 0;
    }
    const char *minor_start = parse_end + 1;
    long minor_l = strtol(minor_start, &parse_end, BASE10);
    if (parse_end == minor_start) {
        return 0;
    }

    int major = (int)major_l;
    int minor = (int)minor_l;
    return (major > 1) || ((major == 1) && (minor >= 5));
}

static const GLvoid *vbo_offset_ptr(size_t byte_offset) {
    return (const GLvoid *)((const char *)NULL + byte_offset);
}

void db_renderer_opengl_gl1_5_gles1_1_init(void) {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    g_state.use_vbo = has_gl15_vbo_support();
    g_state.vbo = 0U;

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    if (g_state.use_vbo) {
        glGenBuffers(1, &g_state.vbo);
        if (g_state.vbo != 0U) {
            glBindBuffer(GL_ARRAY_BUFFER, g_state.vbo);
            glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(g_state.vertices),
                         NULL, GL_STREAM_DRAW);
            glVertexPointer(2, GL_FLOAT, STRIDE_BYTES,
                            vbo_offset_ptr(sizeof(float) * POS_OFFSET_FLOATS));
            glColorPointer(3, GL_FLOAT, STRIDE_BYTES,
                           vbo_offset_ptr(sizeof(float) * COLOR_OFFSET_FLOATS));
            db_infof(BACKEND_NAME, "using capability mode: %s", MODE_VBO);
            return;
        }
    }

    g_state.use_vbo = 0;
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    db_infof(BACKEND_NAME, "using capability mode: %s", MODE_CLIENT_ARRAY);
}

void db_renderer_opengl_gl1_5_gles1_1_render_frame(double time_s) {
    db_fill_band_vertices_pos_rgb(g_state.vertices, BENCH_BANDS, time_s);

    if (g_state.use_vbo) {
        glBindBuffer(GL_ARRAY_BUFFER, g_state.vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        (GLsizeiptr)sizeof(g_state.vertices), g_state.vertices);
        glVertexPointer(2, GL_FLOAT, STRIDE_BYTES,
                        vbo_offset_ptr(sizeof(float) * POS_OFFSET_FLOATS));
        glColorPointer(3, GL_FLOAT, STRIDE_BYTES,
                       vbo_offset_ptr(sizeof(float) * COLOR_OFFSET_FLOATS));
    } else {
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glVertexPointer(2, GL_FLOAT, STRIDE_BYTES, &g_state.vertices[0]);
        glColorPointer(3, GL_FLOAT, STRIDE_BYTES,
                       &g_state.vertices[COLOR_OFFSET_FLOATS]);
    }

    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(BENCH_BANDS * TRI_VERTS_PER_BAND));
}

void db_renderer_opengl_gl1_5_gles1_1_shutdown(void) {
    if (g_state.vbo != 0U) {
        glDeleteBuffers(1, &g_state.vbo);
        g_state.vbo = 0U;
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    (void)memset(&g_state, 0, sizeof(g_state));
}

const char *db_renderer_opengl_gl1_5_gles1_1_capability_mode(void) {
    return g_state.use_vbo ? MODE_VBO : MODE_CLIENT_ARRAY;
}
