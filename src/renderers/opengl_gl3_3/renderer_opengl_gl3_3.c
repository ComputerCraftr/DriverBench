#include "renderer_opengl_gl3_3.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "../../config/benchmark_config.h"
#include "../../core/db_core.h"
#include "../renderer_benchmark_common.h"
#include "../renderer_gl_common.h"
#include "../renderer_snake_common.h"

#ifndef DB_HAS_OPENGL_DESKTOP
#error "renderer_opengl_gl3_3 requires desktop OpenGL support"
#endif

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#include <OpenGL/gltypes.h>
#else
#define GL_GLEXT_PROTOTYPES
#ifdef __has_include
#if __has_include(<GL/glcorearb.h>)
#include <GL/glcorearb.h>
#else
#include <GL/gl.h>
#include <GL/glext.h>
#endif
#else
#include <GL/gl.h>
#include <GL/glext.h>
#endif
#endif

#if !defined(OPENGL_GL3_3_VERT_SHADER_PATH) ||                                 \
    !defined(OPENGL_GL3_3_FRAG_SHADER_PATH)
#error "OpenGL GL3.3 shader paths must be provided by the build system."
#endif

#define BACKEND_NAME "renderer_opengl_gl3_3"
#define ATTR_COLOR_COMPONENTS 3
#define ATTR_COLOR_LOC 1U
#define ATTR_POSITION_COMPONENTS 2
#define ATTR_POSITION_LOC 0U
#define DB_CAP_MODE_OPENGL_SHADER_VBO "opengl_shader_vbo"
#define DB_CAP_MODE_OPENGL_SHADER_HISTORY_DIRTY_DRAW                           \
    "opengl_shader_history_dirty_draw"
#define DB_CAP_MODE_OPENGL_SHADER_VBO_MAP_BUFFER "opengl_shader_vbo_map_buffer"
#define DB_CAP_MODE_OPENGL_SHADER_VBO_MAP_RANGE "opengl_shader_vbo_map_range"
#define DB_CAP_MODE_OPENGL_SHADER_VBO_PERSISTENT "opengl_shader_vbo_persistent"
#define SHADER_LOG_MSG_CAPACITY 1024
#define failf(...) db_failf(BACKEND_NAME, __VA_ARGS__)
#define infof(...) db_infof(BACKEND_NAME, __VA_ARGS__)

typedef struct {
    GLuint fallback_tex;
    uint64_t state_hash;
    uint32_t frame_index;
    db_benchmark_runtime_init_t runtime;
    db_gl_vertex_init_t vertex;
    GLint u_gradient_head_row;
    GLint u_gradient_window_rows;
    GLint u_band_count;
    GLint u_grid_base_color;
    GLint u_grid_cols;
    GLint u_grid_rows;
    GLint u_grid_target_color;
    GLint u_history_tex;
    GLint u_mode_phase_flag;
    GLint u_palette_cycle;
    GLint u_pattern_seed;
    GLint u_render_mode;
    GLint u_snake_batch_size;
    GLint u_snake_cursor;
    GLint u_snake_shape_index;
    GLint u_frame_index;
    GLint u_viewport_width;
    int history_height;
    GLuint history_fbo[2];
    int history_initialized;
    int history_read_index;
    GLuint history_tex[2];
    int history_width;
    GLuint program;
    int uniform_mode_phase_flag_cache;
    uint32_t uniform_gradient_head_row_cache;
    int uniform_gradient_head_row_cache_valid;
    uint32_t uniform_palette_cycle_cache;
    int uniform_palette_cycle_cache_valid;
    uint32_t uniform_snake_batch_size_cache;
    int uniform_snake_batch_size_cache_valid;
    uint32_t uniform_snake_cursor_cache;
    int uniform_snake_cursor_cache_valid;
    uint32_t uniform_snake_shape_index_cache;
    int uniform_snake_shape_index_cache_valid;
    GLuint vao;
    GLuint vbo;
    size_t vbo_bytes;
} renderer_state_t;

static renderer_state_t g_state = {0};

// NOLINTBEGIN(performance-no-int-to-ptr)
static const void *vbo_offset_ptr(size_t byte_offset) {
    return (const void *)(uintptr_t)byte_offset;
}
// NOLINTEND(performance-no-int-to-ptr)

static GLsizei db_draw_vertex_count_glsizei(void) {
    return (GLsizei)db_checked_u32_to_i32(BACKEND_NAME, "draw_vertex_count",
                                          g_state.vertex.draw_vertex_count);
}

static void db_set_uniform1i_if_changed(GLint location, int *cache, int value) {
    if ((location >= 0) && (*cache != value)) {
        glUniform1i(location, value);
        *cache = value;
    }
}

static void db_set_uniform1ui_u32_if_changed(GLint location, uint32_t *cache,
                                             int *cache_valid, uint32_t value) {
    if ((location >= 0) && ((*cache_valid == 0) || (*cache != value))) {
        glUniform1ui(location, value);
        *cache = value;
        *cache_valid = 1;
    }
}

static void db_gl3_destroy_history_targets(void) {
    for (size_t i = 0U; i < 2U; i++) {
        if (g_state.history_fbo[i] != 0U) {
            glDeleteFramebuffers(1, &g_state.history_fbo[i]);
            g_state.history_fbo[i] = 0U;
        }
        db_gl_texture_delete_if_valid((unsigned int *)&g_state.history_tex[i]);
    }
    g_state.history_width = 0;
    g_state.history_height = 0;
    g_state.history_initialized = 0;
    g_state.history_read_index = -1;
}

static void db_gl3_create_fallback_texture(void) {
    if (g_state.fallback_tex != 0U) {
        return;
    }
    static const unsigned char fallback_rgba[4] = {
        (unsigned char)(BENCH_GRID_PHASE0_R * 255.0F),
        (unsigned char)(BENCH_GRID_PHASE0_G * 255.0F),
        (unsigned char)(BENCH_GRID_PHASE0_B * 255.0F), 255U};
    if (db_gl_texture_create_rgba((unsigned int *)&g_state.fallback_tex, 1, 1,
                                  GL_RGBA8, fallback_rgba) == 0) {
        failf("failed to create fallback history texture");
    }
}

static void db_gl3_ensure_history_targets(void) {
    if (db_pattern_uses_history_texture(g_state.runtime.pattern) == 0) {
        return;
    }

    int viewport_width = 0;
    int viewport_height = 0;
    if (db_gl_get_viewport_size(&viewport_width, &viewport_height) == 0) {
        return;
    }
    GLint prev_read_fbo = 0;
    GLint prev_draw_fbo = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_read_fbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_draw_fbo);
    if ((g_state.history_width == viewport_width) &&
        (g_state.history_height == viewport_height) &&
        (g_state.history_tex[0] != 0U) && (g_state.history_tex[1] != 0U) &&
        (g_state.history_fbo[0] != 0U) && (g_state.history_fbo[1] != 0U) &&
        (g_state.history_read_index >= 0)) {
        return;
    }

    const int old_width = g_state.history_width;
    const int old_height = g_state.history_height;
    const int old_read_index = g_state.history_read_index;
    const int old_initialized = g_state.history_initialized;
    const GLuint old_tex0 = g_state.history_tex[0];
    const GLuint old_tex1 = g_state.history_tex[1];
    const GLuint old_fbo0 = g_state.history_fbo[0];
    const GLuint old_fbo1 = g_state.history_fbo[1];

    GLuint new_tex[2] = {0U, 0U};
    GLuint new_fbo[2] = {0U, 0U};
    for (size_t i = 0U; i < 2U; i++) {
        if (db_gl_texture_create_rgba((unsigned int *)&new_tex[i],
                                      viewport_width, viewport_height, GL_RGBA8,
                                      NULL) == 0) {
            failf("failed to create history texture");
        }

        glGenFramebuffers(1, &new_fbo[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, new_fbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, new_tex[i], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
            GL_FRAMEBUFFER_COMPLETE) {
            failf("history framebuffer incomplete");
        }
    }

    int copied_history = 0;
    if ((old_initialized != 0) && (old_width > 0) && (old_height > 0) &&
        ((old_read_index == 0) || (old_read_index == 1))) {
        const GLuint old_read_tex = (old_read_index == 0) ? old_tex0 : old_tex1;
        if (old_read_tex != 0U) {
            GLuint read_fbo = 0U;
            glGenFramebuffers(1, &read_fbo);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, read_fbo);
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D, old_read_tex, 0);
            if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) ==
                GL_FRAMEBUFFER_COMPLETE) {
                copied_history = 1;
                for (size_t i = 0U; i < 2U; i++) {
                    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, new_fbo[i]);
                    glBlitFramebuffer(0, 0, old_width, old_height, 0, 0,
                                      viewport_width, viewport_height,
                                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
                    if (glGetError() != GL_NO_ERROR) {
                        copied_history = 0;
                        break;
                    }
                }
            }
            glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prev_read_fbo);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prev_draw_fbo);
            if (read_fbo != 0U) {
                glDeleteFramebuffers(1, &read_fbo);
            }
        }
    }

    if (copied_history == 0) {
        for (size_t i = 0U; i < 2U; i++) {
            glBindFramebuffer(GL_FRAMEBUFFER, new_fbo[i]);
            glClearColor(BENCH_GRID_PHASE0_R, BENCH_GRID_PHASE0_G,
                         BENCH_GRID_PHASE0_B, 1.0F);
            glClear(GL_COLOR_BUFFER_BIT);
        }
    }

    if (old_fbo0 != 0U) {
        glDeleteFramebuffers(1, &old_fbo0);
    }
    if (old_fbo1 != 0U) {
        glDeleteFramebuffers(1, &old_fbo1);
    }
    if (old_tex0 != 0U) {
        unsigned int old_tex0_u32 = (unsigned int)old_tex0;
        db_gl_texture_delete_if_valid(&old_tex0_u32);
    }
    if (old_tex1 != 0U) {
        unsigned int old_tex1_u32 = (unsigned int)old_tex1;
        db_gl_texture_delete_if_valid(&old_tex1_u32);
    }

    g_state.history_tex[0] = new_tex[0];
    g_state.history_tex[1] = new_tex[1];
    g_state.history_fbo[0] = new_fbo[0];
    g_state.history_fbo[1] = new_fbo[1];
    g_state.history_width = viewport_width;
    g_state.history_height = viewport_height;
    g_state.history_initialized = 1;
    g_state.history_read_index = 0;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prev_read_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prev_draw_fbo);
}

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
        const int msg_len_i32 =
            db_checked_int_to_i32(BACKEND_NAME, "shader_log_msg_len", msg_len);
        failf("Shader compile failed (%u): %.*s", (unsigned)shader_type,
              msg_len_i32, log_msg);
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
        const int msg_len_i32 =
            db_checked_int_to_i32(BACKEND_NAME, "program_log_msg_len", msg_len);
        failf("Program link failed: %.*s", msg_len_i32, log_msg);
    }
    return program;
}

static int db_init_vertices_for_mode(void) {
    db_benchmark_runtime_init_t runtime_state = {0};
    db_gl_vertex_init_t init_state = {0};
    if (!db_init_benchmark_runtime_common(BACKEND_NAME, &runtime_state)) {
        return 0;
    }
    if (!db_init_vertices_for_runtime_common_with_stride(
            BACKEND_NAME, &init_state, &runtime_state,
            DB_VERTEX_FLOAT_STRIDE)) {
        return 0;
    }

    g_state.vertex = init_state;
    g_state.runtime = runtime_state;
    g_state.runtime.snake_shape_index = 0U;
    return 1;
}

void db_renderer_opengl_gl3_3_init(void) {
    if (!db_init_vertices_for_mode()) {
        failf("failed to allocate benchmark vertex buffers");
    }

    glGenVertexArrays(1, &g_state.vao);
    unsigned int vbo_u32 = 0U;
    if (db_gl_vbo_create_or_zero(&vbo_u32) != 0) {
        g_state.vbo = (GLuint)vbo_u32;
    }
    if (g_state.vbo == 0U) {
        failf("failed to create GL array buffer");
    }
    glBindVertexArray(g_state.vao);
    if (db_gl_vbo_bind((unsigned int)g_state.vbo) == 0) {
        failf("failed to bind GL array buffer");
    }
    g_state.vbo_bytes = (size_t)g_state.vertex.draw_vertex_count *
                        DB_VERTEX_FLOAT_STRIDE * sizeof(float);
    if (db_gl_vbo_init_data(g_state.vbo_bytes, g_state.vertex.vertices,
                            GL_DYNAMIC_DRAW) == 0) {
        failf("failed to initialize GL array buffer");
    }

    glEnableVertexAttribArray(ATTR_POSITION_LOC);
    glVertexAttribPointer(
        ATTR_POSITION_LOC, ATTR_POSITION_COMPONENTS, GL_FLOAT, GL_FALSE,
        (GLsizei)(DB_VERTEX_FLOAT_STRIDE * sizeof(float)), (const void *)0);
    glEnableVertexAttribArray(ATTR_COLOR_LOC);
    glVertexAttribPointer(
        ATTR_COLOR_LOC, ATTR_COLOR_COMPONENTS, GL_FLOAT, GL_FALSE,
        (GLsizei)(DB_VERTEX_FLOAT_STRIDE * sizeof(float)),
        vbo_offset_ptr(DB_VERTEX_POSITION_FLOAT_COUNT * sizeof(float)));

    g_state.vertex.upload = (db_gl_upload_probe_result_t){0};
    db_gl_upload_probe_result_t probe_result = {0};
    db_gl_probe_upload_capabilities(g_state.vbo_bytes, g_state.vertex.vertices,
                                    &probe_result);
    g_state.vertex.upload = probe_result;
    infof("using capability mode: %s",
          db_renderer_opengl_gl3_3_capability_mode());

    g_state.program = build_program_from_files(OPENGL_GL3_3_VERT_SHADER_PATH,
                                               OPENGL_GL3_3_FRAG_SHADER_PATH);
    glUseProgram(g_state.program);
    g_state.u_render_mode =
        glGetUniformLocation(g_state.program, "u_render_mode");
    g_state.u_band_count =
        glGetUniformLocation(g_state.program, "u_band_count");
    g_state.u_mode_phase_flag =
        glGetUniformLocation(g_state.program, "u_mode_phase_flag");
    g_state.u_snake_cursor =
        glGetUniformLocation(g_state.program, "u_snake_cursor");
    g_state.u_snake_batch_size =
        glGetUniformLocation(g_state.program, "u_snake_batch_size");
    g_state.u_grid_cols = glGetUniformLocation(g_state.program, "u_grid_cols");
    g_state.u_grid_rows = glGetUniformLocation(g_state.program, "u_grid_rows");
    g_state.u_gradient_head_row =
        glGetUniformLocation(g_state.program, "u_gradient_head_row");
    g_state.u_snake_shape_index =
        glGetUniformLocation(g_state.program, "u_snake_shape_index");
    g_state.u_frame_index =
        glGetUniformLocation(g_state.program, "u_frame_index");
    g_state.u_viewport_width =
        glGetUniformLocation(g_state.program, "u_viewport_width");
    g_state.u_gradient_window_rows =
        glGetUniformLocation(g_state.program, "u_gradient_window_rows");
    g_state.u_palette_cycle =
        glGetUniformLocation(g_state.program, "u_palette_cycle");
    g_state.u_pattern_seed =
        glGetUniformLocation(g_state.program, "u_pattern_seed");
    g_state.u_grid_base_color =
        glGetUniformLocation(g_state.program, "u_grid_base_color");
    g_state.u_grid_target_color =
        glGetUniformLocation(g_state.program, "u_grid_target_color");
    g_state.u_history_tex =
        glGetUniformLocation(g_state.program, "u_history_tex");
    g_state.uniform_mode_phase_flag_cache = -1;
    g_state.uniform_snake_cursor_cache = 0U;
    g_state.uniform_snake_cursor_cache_valid = 0;
    g_state.uniform_snake_batch_size_cache = 0U;
    g_state.uniform_snake_batch_size_cache_valid = 0;
    g_state.uniform_gradient_head_row_cache = 0U;
    g_state.uniform_gradient_head_row_cache_valid = 0;
    g_state.uniform_snake_shape_index_cache = 0U;
    g_state.uniform_snake_shape_index_cache_valid = 0;
    g_state.uniform_palette_cycle_cache = 0U;
    g_state.uniform_palette_cycle_cache_valid = 0;

    if (g_state.u_render_mode >= 0) {
        glUniform1ui(g_state.u_render_mode,
                     db_checked_int_to_u32(BACKEND_NAME, "u_render_mode",
                                           g_state.runtime.pattern));
    }
    glUniform3f(g_state.u_grid_base_color, BENCH_GRID_PHASE0_R,
                BENCH_GRID_PHASE0_G, BENCH_GRID_PHASE0_B);
    glUniform3f(g_state.u_grid_target_color, BENCH_GRID_PHASE1_R,
                BENCH_GRID_PHASE1_G, BENCH_GRID_PHASE1_B);
    if (g_state.u_band_count >= 0) {
        glUniform1ui(g_state.u_band_count, BENCH_BANDS);
    }
    glUniform1ui(g_state.u_grid_cols, db_grid_cols_effective());
    glUniform1ui(g_state.u_grid_rows, db_grid_rows_effective());
    glUniform1ui(g_state.u_gradient_window_rows,
                 db_gradient_window_rows_effective());
    if (g_state.u_pattern_seed >= 0) {
        glUniform1ui(g_state.u_pattern_seed, g_state.runtime.pattern_seed);
    }
    if (g_state.u_viewport_width >= 0) {
        int viewport_width = 0;
        int viewport_height = 0;
        if (db_gl_get_viewport_size(&viewport_width, &viewport_height) != 0) {
            glUniform1ui(g_state.u_viewport_width,
                         db_checked_int_to_u32(BACKEND_NAME, "viewport_width",
                                               viewport_width));
        }
    }
    if (g_state.u_history_tex >= 0) {
        glUniform1i(g_state.u_history_tex, 0);
    }
    db_gl3_create_fallback_texture();
    if (g_state.fallback_tex != 0U) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_state.fallback_tex);
    }

    if ((g_state.runtime.pattern == DB_PATTERN_BANDS) &&
        (g_state.u_frame_index >= 0)) {
        glUniform1ui(g_state.u_frame_index, 0);
    }

    db_set_uniform1ui_u32_if_changed(
        g_state.u_gradient_head_row, &g_state.uniform_gradient_head_row_cache,
        &g_state.uniform_gradient_head_row_cache_valid,
        g_state.runtime.gradient_head_row);
    db_set_uniform1ui_u32_if_changed(
        g_state.u_snake_shape_index, &g_state.uniform_snake_shape_index_cache,
        &g_state.uniform_snake_shape_index_cache_valid,
        g_state.runtime.snake_shape_index);
    db_set_uniform1ui_u32_if_changed(g_state.u_palette_cycle,
                                     &g_state.uniform_palette_cycle_cache,
                                     &g_state.uniform_palette_cycle_cache_valid,
                                     g_state.runtime.gradient_cycle);
    db_set_uniform1i_if_changed(g_state.u_mode_phase_flag,
                                &g_state.uniform_mode_phase_flag_cache,
                                g_state.runtime.mode_phase_flag);
}

void db_renderer_opengl_gl3_3_render_frame(uint32_t frame_index) {
    db_gl3_ensure_history_targets();
    if (g_state.u_viewport_width >= 0) {
        int viewport_width = 0;
        int viewport_height = 0;
        if (db_gl_get_viewport_size(&viewport_width, &viewport_height) != 0) {
            glUniform1ui(g_state.u_viewport_width,
                         db_checked_int_to_u32(BACKEND_NAME, "viewport_width",
                                               viewport_width));
        }
    }

    if ((g_state.runtime.pattern == DB_PATTERN_SNAKE_GRID) ||
        (g_state.runtime.pattern == DB_PATTERN_SNAKE_RECT) ||
        (g_state.runtime.pattern == DB_PATTERN_SNAKE_SHAPES)) {
        const int is_grid = (g_state.runtime.pattern == DB_PATTERN_SNAKE_GRID);
        const db_snake_plan_request_t request = db_snake_plan_request_make(
            is_grid, g_state.runtime.pattern_seed,
            g_state.runtime.snake_shape_index, g_state.runtime.snake_cursor, 0U,
            0U, g_state.runtime.mode_phase_flag,
            g_state.runtime.bench_speed_step);
        const db_snake_plan_t plan = db_snake_plan_next_step(&request);
        const db_snake_step_target_t target = db_snake_step_target_from_plan(
            is_grid, g_state.runtime.pattern_seed, &plan);
        g_state.runtime.snake_batch_size = plan.batch_size;
        if (target.has_next_mode_phase_flag != 0) {
            db_set_uniform1i_if_changed(g_state.u_mode_phase_flag,
                                        &g_state.uniform_mode_phase_flag_cache,
                                        plan.clearing_phase);
            g_state.runtime.mode_phase_flag = target.next_mode_phase_flag;
        }
        if (target.has_next_shape_index != 0) {
            db_set_uniform1ui_u32_if_changed(
                g_state.u_snake_shape_index,
                &g_state.uniform_snake_shape_index_cache,
                &g_state.uniform_snake_shape_index_cache_valid,
                plan.active_shape_index);
            g_state.runtime.snake_shape_index = target.next_shape_index;
        }
        db_set_uniform1ui_u32_if_changed(
            g_state.u_snake_cursor, &g_state.uniform_snake_cursor_cache,
            &g_state.uniform_snake_cursor_cache_valid, plan.active_cursor);
        db_set_uniform1ui_u32_if_changed(
            g_state.u_snake_batch_size, &g_state.uniform_snake_batch_size_cache,
            &g_state.uniform_snake_batch_size_cache_valid,
            g_state.runtime.snake_batch_size);
        g_state.runtime.snake_cursor = plan.next_cursor;
    } else if ((g_state.runtime.pattern == DB_PATTERN_GRADIENT_SWEEP) ||
               (g_state.runtime.pattern == DB_PATTERN_GRADIENT_FILL)) {
        const db_gradient_step_t gradient_step = db_gradient_step_from_runtime(
            g_state.runtime.pattern, g_state.runtime.gradient_head_row,
            g_state.runtime.mode_phase_flag, g_state.runtime.gradient_cycle,
            g_state.runtime.bench_speed_step);
        const db_gradient_damage_plan_t *plan = &gradient_step.plan;
        db_gradient_apply_step_to_runtime(&g_state.runtime, &gradient_step);
        db_set_uniform1ui_u32_if_changed(
            g_state.u_gradient_head_row,
            &g_state.uniform_gradient_head_row_cache,
            &g_state.uniform_gradient_head_row_cache_valid,
            plan->render_head_row);
        db_set_uniform1i_if_changed(g_state.u_mode_phase_flag,
                                    &g_state.uniform_mode_phase_flag_cache,
                                    gradient_step.render_direction_down);
        db_set_uniform1ui_u32_if_changed(
            g_state.u_palette_cycle, &g_state.uniform_palette_cycle_cache,
            &g_state.uniform_palette_cycle_cache_valid,
            plan->render_cycle_index);
    } else if (g_state.runtime.pattern == DB_PATTERN_BANDS) {
        if (g_state.u_frame_index >= 0) {
            glUniform1ui(g_state.u_frame_index, frame_index);
        }
    }

    if (db_pattern_uses_history_texture(g_state.runtime.pattern) == 0) {
        if (g_state.fallback_tex != 0U) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, g_state.fallback_tex);
        }
        glDrawArrays(GL_TRIANGLES, 0, db_draw_vertex_count_glsizei());
        g_state.state_hash = db_benchmark_runtime_state_hash(
            &g_state.runtime, g_state.frame_index, db_grid_cols_effective(),
            db_grid_rows_effective());
        g_state.frame_index++;
        return;
    }
    if ((g_state.history_read_index < 0) || (g_state.history_width <= 0) ||
        (g_state.history_height <= 0)) {
        g_state.state_hash = db_benchmark_runtime_state_hash(
            &g_state.runtime, g_state.frame_index, db_grid_cols_effective(),
            db_grid_rows_effective());
        g_state.frame_index++;
        return;
    }

    const int read_index = g_state.history_read_index;
    const int write_index = (read_index == 0) ? 1 : 0;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_state.history_tex[read_index]);

    GLint prev_read_fbo = 0;
    GLint prev_draw_fbo = 0;
    GLint prev_viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_read_fbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_draw_fbo);
    glGetIntegerv(GL_VIEWPORT, prev_viewport);

    glBindFramebuffer(GL_FRAMEBUFFER, g_state.history_fbo[write_index]);
    glViewport(0, 0, g_state.history_width, g_state.history_height);
    glDrawArrays(GL_TRIANGLES, 0, db_draw_vertex_count_glsizei());

    glBindFramebuffer(GL_READ_FRAMEBUFFER, g_state.history_fbo[write_index]);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, g_state.history_width, g_state.history_height, 0, 0,
                      g_state.history_width, g_state.history_height,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prev_read_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prev_draw_fbo);
    glViewport(prev_viewport[0], prev_viewport[1], prev_viewport[2],
               prev_viewport[3]);
    g_state.history_read_index = write_index;
    g_state.state_hash = db_benchmark_runtime_state_hash(
        &g_state.runtime, g_state.frame_index, db_grid_cols_effective(),
        db_grid_rows_effective());
    g_state.frame_index++;
}

void db_renderer_opengl_gl3_3_shutdown(void) {
    if (g_state.vertex.upload.persistent_mapped_ptr != NULL) {
        (void)db_gl_vbo_bind((unsigned int)g_state.vbo);
        db_gl_unmap_current_array_buffer();
    }
    db_gl3_destroy_history_targets();
    db_gl_texture_delete_if_valid((unsigned int *)&g_state.fallback_tex);
    glDeleteProgram(g_state.program);
    db_gl_vbo_delete_if_valid((unsigned int)g_state.vbo);
    glDeleteVertexArrays(1, &g_state.vao);
    free(g_state.vertex.vertices);
    g_state = (renderer_state_t){0};
}

const char *db_renderer_opengl_gl3_3_capability_mode(void) {
    if (db_pattern_uses_history_texture(g_state.runtime.pattern) != 0) {
        return DB_CAP_MODE_OPENGL_SHADER_HISTORY_DIRTY_DRAW;
    }
    if (g_state.vertex.upload.use_persistent_upload != 0) {
        return DB_CAP_MODE_OPENGL_SHADER_VBO_PERSISTENT;
    }
    if (g_state.vertex.upload.use_map_range_upload != 0) {
        return DB_CAP_MODE_OPENGL_SHADER_VBO_MAP_RANGE;
    }
    if (g_state.vertex.upload.use_map_buffer_upload != 0) {
        return DB_CAP_MODE_OPENGL_SHADER_VBO_MAP_BUFFER;
    }
    return DB_CAP_MODE_OPENGL_SHADER_VBO;
}

uint32_t db_renderer_opengl_gl3_3_work_unit_count(void) {
    return (g_state.runtime.work_unit_count != 0U)
               ? g_state.runtime.work_unit_count
               : BENCH_BANDS;
}

uint64_t db_renderer_opengl_gl3_3_state_hash(void) {
    return g_state.state_hash;
}
