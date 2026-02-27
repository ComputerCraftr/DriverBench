#include "renderer_opengl_gl3_3.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "../../config/benchmark_config.h"
#include "../../core/db_core.h"
#include "../renderer_benchmark_common.h"
#include "../renderer_gl_common.h"
#include "../renderer_snake_common.h"
#include "../renderer_snake_shape_common.h"

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
#define SHADER_LOG_MSG_CAPACITY 1024
#define failf(...) db_failf(BACKEND_NAME, __VA_ARGS__)
#define infof(...) db_infof(BACKEND_NAME, __VA_ARGS__)

typedef struct {
    char capability_mode[DB_GL_CAPABILITY_MODE_MAX];
    GLuint fallback_tex;
    uint64_t full_draw_frames;
    uint64_t dirty_draw_frames;
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
    int history_height;
    GLuint history_fbo[2];
    int history_initialized;
    int history_read_index;
    GLuint history_tex[2];
    int history_width;
    GLint last_viewport_w;
    GLint last_viewport_h;
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
    unsigned int bound_array_buffer;
    size_t vbo_bytes;
} renderer_state_t;

static renderer_state_t g_state = {0};

static const char *db_gl3_cap_upload_string(void) {
    return DB_GL_CAP_UPLOAD_VBO;
}

static void db_gl3_refresh_capability_mode(void) {
    const char *draw_mode =
        (db_pattern_uses_history_texture(g_state.runtime.pattern) != 0)
            ? DB_GL_CAP_MODE_OPENGL_SHADER_HISTORY_DIRTY_DRAW
            : DB_GL_CAP_DRAW_TILES_FULL;
    db_gl_capability_mode_compose(g_state.capability_mode,
                                  sizeof(g_state.capability_mode), draw_mode,
                                  db_gl3_cap_upload_string(), 0);
}

static void db_set_uniform1i_if_changed(GLint location, int *cache, int value) {
    if ((location >= 0) && (*cache != value)) {
        glUniform1i(location, value);
        *cache = value;
    }
}

static void db_set_uniform1ui_if_changed(GLint location, uint32_t *cache,
                                         int *cache_valid, uint32_t value) {
    if ((location >= 0) && ((*cache_valid == 0) || (*cache != value))) {
        glUniform1ui(location, value);
        *cache = value;
        *cache_valid = 1;
    }
}

static int db_gl3_step_span_row_range(uint32_t region_width,
                                      uint32_t region_height,
                                      uint32_t span_start, uint32_t span_count,
                                      db_dirty_row_range_t *out_range) {
    if ((out_range == NULL) || (region_width == 0U) || (region_height == 0U) ||
        (span_count == 0U)) {
        return 0;
    }
    const uint64_t total_tiles = (uint64_t)region_width * region_height;
    uint64_t start = span_start;
    if (start > total_tiles) {
        start = total_tiles;
    }
    uint64_t end = start + span_count;
    if (end > total_tiles) {
        end = total_tiles;
    }
    if (end <= start) {
        return 0;
    }

    const uint32_t row_start = db_checked_u64_to_u32(
        BACKEND_NAME, "gl3_dirty_row_start", start / region_width);
    const uint32_t row_end_exclusive =
        db_checked_u64_to_u32(BACKEND_NAME, "gl3_dirty_row_end",
                              (end - 1U) / region_width) +
        1U;
    out_range->row_start = row_start;
    out_range->row_count = row_end_exclusive - row_start;
    return (out_range->row_count > 0U) ? 1 : 0;
}

static size_t db_gl3_coalesce_dirty_row_ranges(db_dirty_row_range_t *ranges,
                                               size_t range_count) {
    if ((ranges == NULL) || (range_count == 0U)) {
        return 0U;
    }
    for (size_t i = 1U; i < range_count; i++) {
        db_dirty_row_range_t key = ranges[i];
        size_t insert_index = i;
        while ((insert_index > 0U) &&
               (ranges[insert_index - 1U].row_start > key.row_start)) {
            ranges[insert_index] = ranges[insert_index - 1U];
            insert_index--;
        }
        ranges[insert_index] = key;
    }

    size_t out_count = 0U;
    for (size_t i = 0U; i < range_count; i++) {
        if (ranges[i].row_count == 0U) {
            continue;
        }
        if (out_count == 0U) {
            ranges[out_count++] = ranges[i];
            continue;
        }
        db_dirty_row_range_t *tail = &ranges[out_count - 1U];
        const uint32_t tail_end = tail->row_start + tail->row_count;
        const uint32_t curr_end = ranges[i].row_start + ranges[i].row_count;
        if (ranges[i].row_start <= tail_end) {
            if (curr_end > tail_end) {
                tail->row_count = curr_end - tail->row_start;
            }
        } else {
            ranges[out_count++] = ranges[i];
        }
    }
    return out_count;
}

static size_t db_gl3_collect_snake_dirty_rows(const db_snake_plan_t *plan,
                                              const db_snake_region_t *region,
                                              db_dirty_row_range_t out[4]) {
    if ((plan == NULL) || (region == NULL) || (out == NULL) ||
        (region->width == 0U) || (region->height == 0U)) {
        return 0U;
    }

    size_t range_count = 0U;
    db_dirty_row_range_t local = {0U, 0U};
    if ((range_count < 4U) &&
        db_gl3_step_span_row_range(region->width, region->height,
                                   plan->prev_start, plan->prev_count,
                                   &local) != 0) {
        local.row_start += region->y;
        out[range_count++] = local;
    }
    if ((range_count < 4U) &&
        db_gl3_step_span_row_range(region->width, region->height,
                                   plan->active_cursor, plan->batch_size,
                                   &local) != 0) {
        local.row_start += region->y;
        out[range_count++] = local;
    }

    return db_gl3_coalesce_dirty_row_ranges(out, range_count);
}

static int db_gl3_scissor_from_row_range(const db_dirty_row_range_t *range,
                                         GLint viewport_width,
                                         GLint viewport_height, GLint *x_out,
                                         GLint *y_out, GLsizei *width_out,
                                         GLsizei *height_out) {
    if ((range == NULL) || (x_out == NULL) || (y_out == NULL) ||
        (width_out == NULL) || (height_out == NULL) || (viewport_width <= 0) ||
        (viewport_height <= 0)) {
        return 0;
    }

    const uint32_t total_rows = db_grid_rows_effective();
    if ((total_rows == 0U) || (range->row_count == 0U)) {
        return 0;
    }

    const uint32_t row_start = db_u32_min(range->row_start, total_rows);
    const uint32_t row_end =
        db_u32_min(row_start + range->row_count, total_rows);
    if (row_end <= row_start) {
        return 0;
    }

    GLint py_top = (GLint)(((uint64_t)row_start * (uint64_t)viewport_height) /
                           (uint64_t)total_rows);
    GLint py_bottom = (GLint)(((uint64_t)row_end * (uint64_t)viewport_height) /
                              (uint64_t)total_rows);
    if (row_end == total_rows) {
        py_bottom = viewport_height;
    }
    if (py_top < 0) {
        py_top = 0;
    }
    if (py_bottom > viewport_height) {
        py_bottom = viewport_height;
    }
    if (py_bottom <= py_top) {
        return 0;
    }

    GLint rect_y = viewport_height - py_bottom;
    GLsizei rect_h = (GLsizei)(py_bottom - py_top);
    if (rect_y < 0) {
        rect_h = (GLsizei)(rect_h + rect_y);
        rect_y = 0;
    }
    if ((rect_y + (GLint)rect_h) > viewport_height) {
        rect_h = (GLsizei)(viewport_height - rect_y);
    }
    if (rect_h <= 0) {
        return 0;
    }

    *x_out = 0;
    *y_out = rect_y;
    *width_out = (GLsizei)viewport_width;
    *height_out = rect_h;
    return 1;
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

static void db_gl3_ensure_history_targets(int viewport_width_px,
                                          int viewport_height_px) {
    if (db_pattern_uses_history_texture(g_state.runtime.pattern) == 0) {
        return;
    }

    const int viewport_width = viewport_width_px;
    const int viewport_height = viewport_height_px;
    if ((viewport_width <= 0) || (viewport_height <= 0)) {
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
    g_state.runtime.snake.shape_index = 0U;
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
    if (db_gl_bind_array_buffer_cached((unsigned int)g_state.vbo,
                                       &g_state.bound_array_buffer) == 0) {
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
        db_gl_vbo_offset_ptr(DB_VERTEX_POSITION_FLOAT_COUNT * sizeof(float)));

    g_state.vertex.upload = (db_gl_upload_probe_result_t){0};
    db_gl_upload_probe_result_t probe_result = {0};
    db_gl_probe_upload_capabilities(g_state.vbo_bytes, g_state.vertex.vertices,
                                    &probe_result);
    g_state.vertex.upload = probe_result;
    db_gl3_refresh_capability_mode();
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

    db_set_uniform1ui_if_changed(g_state.u_gradient_head_row,
                                 &g_state.uniform_gradient_head_row_cache,
                                 &g_state.uniform_gradient_head_row_cache_valid,
                                 g_state.runtime.gradient.head_row);
    db_set_uniform1ui_if_changed(g_state.u_snake_shape_index,
                                 &g_state.uniform_snake_shape_index_cache,
                                 &g_state.uniform_snake_shape_index_cache_valid,
                                 g_state.runtime.snake.shape_index);
    db_set_uniform1ui_if_changed(g_state.u_palette_cycle,
                                 &g_state.uniform_palette_cycle_cache,
                                 &g_state.uniform_palette_cycle_cache_valid,
                                 g_state.runtime.gradient.cycle_index);
    db_set_uniform1i_if_changed(g_state.u_mode_phase_flag,
                                &g_state.uniform_mode_phase_flag_cache,
                                g_state.runtime.mode_phase_flag);
}

void db_renderer_opengl_gl3_3_render_frame(uint32_t frame_index,
                                           int viewport_width_px,
                                           int viewport_height_px) {
    if (viewport_width_px <= 0 || viewport_height_px <= 0) {
        viewport_width_px = db_checked_u32_to_i32(
            BACKEND_NAME, "viewport_width_px", db_grid_cols_effective());
        viewport_height_px = db_checked_u32_to_i32(
            BACKEND_NAME, "viewport_height_px", db_grid_rows_effective());
    }

    const int viewport_changed =
        (g_state.last_viewport_w != viewport_width_px) ||
        (g_state.last_viewport_h != viewport_height_px);

    if (viewport_changed != 0) {
        g_state.last_viewport_w = viewport_width_px;
        g_state.last_viewport_h = viewport_height_px;
    }

    db_gl3_ensure_history_targets(g_state.last_viewport_w,
                                  g_state.last_viewport_h);
    db_snake_plan_t snake_plan = {0};
    db_snake_step_target_t snake_target = {0};
    int snake_plan_valid = 0;
    if ((g_state.runtime.pattern == DB_PATTERN_SNAKE_GRID) ||
        (g_state.runtime.pattern == DB_PATTERN_SNAKE_RECT) ||
        (g_state.runtime.pattern == DB_PATTERN_SNAKE_SHAPES)) {
        const int is_grid = (g_state.runtime.pattern == DB_PATTERN_SNAKE_GRID);
        const db_snake_plan_request_t request = db_snake_plan_request_make(
            is_grid, g_state.runtime.pattern_seed,
            g_state.runtime.snake.shape_index, g_state.runtime.snake.cursor, 0U,
            0U, g_state.runtime.mode_phase_flag,
            g_state.runtime.bench_speed_step);
        snake_plan = db_snake_plan_next_step(&request);
        snake_target = db_snake_step_target_from_plan(
            is_grid, g_state.runtime.pattern_seed, &snake_plan);
        snake_plan_valid = 1;
        g_state.runtime.snake.batch_size = snake_plan.batch_size;
        if (snake_target.has_next_mode_phase_flag != 0) {
            db_set_uniform1i_if_changed(g_state.u_mode_phase_flag,
                                        &g_state.uniform_mode_phase_flag_cache,
                                        snake_plan.clearing_phase);
            g_state.runtime.mode_phase_flag = snake_target.next_mode_phase_flag;
        }
        if (snake_target.has_next_shape_index != 0) {
            db_set_uniform1ui_if_changed(
                g_state.u_snake_shape_index,
                &g_state.uniform_snake_shape_index_cache,
                &g_state.uniform_snake_shape_index_cache_valid,
                snake_plan.active_shape_index);
            g_state.runtime.snake.shape_index = snake_target.next_shape_index;
        }
        db_set_uniform1ui_if_changed(g_state.u_snake_cursor,
                                     &g_state.uniform_snake_cursor_cache,
                                     &g_state.uniform_snake_cursor_cache_valid,
                                     snake_plan.active_cursor);
        db_set_uniform1ui_if_changed(
            g_state.u_snake_batch_size, &g_state.uniform_snake_batch_size_cache,
            &g_state.uniform_snake_batch_size_cache_valid,
            g_state.runtime.snake.batch_size);
        g_state.runtime.snake.cursor = snake_plan.next_cursor;
    } else if ((g_state.runtime.pattern == DB_PATTERN_GRADIENT_SWEEP) ||
               (g_state.runtime.pattern == DB_PATTERN_GRADIENT_FILL)) {
        const db_gradient_damage_plan_t plan = db_gradient_step_from_runtime(
            g_state.runtime.pattern, g_state.runtime.gradient.head_row,
            g_state.runtime.mode_phase_flag,
            g_state.runtime.gradient.cycle_index,
            g_state.runtime.bench_speed_step);
        db_gradient_apply_step_to_runtime(&g_state.runtime, &plan);
        db_set_uniform1ui_if_changed(
            g_state.u_gradient_head_row,
            &g_state.uniform_gradient_head_row_cache,
            &g_state.uniform_gradient_head_row_cache_valid,
            plan.render_state.head_row);
        db_set_uniform1i_if_changed(g_state.u_mode_phase_flag,
                                    &g_state.uniform_mode_phase_flag_cache,
                                    plan.render_state.direction_down);
        db_set_uniform1ui_if_changed(g_state.u_palette_cycle,
                                     &g_state.uniform_palette_cycle_cache,
                                     &g_state.uniform_palette_cycle_cache_valid,
                                     plan.render_state.cycle_index);
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
        glDrawArrays(GL_TRIANGLES, 0,
                     (GLsizei)db_gl_draw_vertex_count_i32(
                         BACKEND_NAME, g_state.vertex.draw_vertex_count));
        g_state.full_draw_frames++;
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
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_read_fbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_draw_fbo);

    if (viewport_changed) {
        db_gl_set_viewport_px(g_state.last_viewport_w, g_state.last_viewport_h);
    }
    int used_dirty_history_draw = 0;
    if ((g_state.runtime.backbuffer_draw_full == 0) &&
        (snake_plan_valid != 0)) {
        db_dirty_row_range_t dirty_rows[4] = {
            {0U, 0U}, {0U, 0U}, {0U, 0U}, {0U, 0U}};
        const size_t dirty_count = db_gl3_collect_snake_dirty_rows(
            &snake_plan, &snake_target.region, dirty_rows);
        if (dirty_count > 0U) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER,
                              g_state.history_fbo[read_index]);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER,
                              g_state.history_fbo[write_index]);
            glBlitFramebuffer(0, 0, g_state.history_width,
                              g_state.history_height, 0, 0,
                              g_state.history_width, g_state.history_height,
                              GL_COLOR_BUFFER_BIT, GL_NEAREST);

            glBindFramebuffer(GL_FRAMEBUFFER, g_state.history_fbo[write_index]);
            glEnable(GL_SCISSOR_TEST);
            for (size_t i = 0U; i < dirty_count; i++) {
                GLint sx = 0;
                GLint sy = 0;
                GLsizei sw = 0;
                GLsizei sh = 0;
                if (db_gl3_scissor_from_row_range(
                        &dirty_rows[i], g_state.last_viewport_w,
                        g_state.last_viewport_h, &sx, &sy, &sw, &sh) == 0) {
                    continue;
                }
                glScissor(sx, sy, sw, sh);
                glDrawArrays(
                    GL_TRIANGLES, 0,
                    (GLsizei)db_gl_draw_vertex_count_i32(
                        BACKEND_NAME, g_state.vertex.draw_vertex_count));
            }
            glDisable(GL_SCISSOR_TEST);
            used_dirty_history_draw = 1;
        }
    }
    if (used_dirty_history_draw != 0) {
        g_state.dirty_draw_frames++;
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, g_state.history_fbo[write_index]);
        glDrawArrays(GL_TRIANGLES, 0,
                     (GLsizei)db_gl_draw_vertex_count_i32(
                         BACKEND_NAME, g_state.vertex.draw_vertex_count));
        g_state.full_draw_frames++;
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, g_state.history_fbo[write_index]);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, g_state.history_width, g_state.history_height, 0, 0,
                      g_state.history_width, g_state.history_height,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prev_read_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prev_draw_fbo);
    g_state.history_read_index = write_index;
    g_state.state_hash = db_benchmark_runtime_state_hash(
        &g_state.runtime, g_state.frame_index, db_grid_cols_effective(),
        db_grid_rows_effective());
    g_state.frame_index++;
}

void db_renderer_opengl_gl3_3_shutdown(void) {
    if (g_state.vertex.upload.persistent_mapped_ptr != NULL) {
        (void)db_gl_bind_array_buffer_cached((unsigned int)g_state.vbo,
                                             &g_state.bound_array_buffer);
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
    if (g_state.capability_mode[0] == '\0') {
        db_gl3_refresh_capability_mode();
    }
    return g_state.capability_mode;
}

uint32_t db_renderer_opengl_gl3_3_work_unit_count(void) {
    return db_runtime_work_unit_count(&g_state.runtime, 1);
}

uint64_t db_renderer_opengl_gl3_3_state_hash(void) {
    return g_state.state_hash;
}

void db_renderer_opengl_gl3_3_draw_stats(uint64_t *full_draw_frames,
                                         uint64_t *dirty_draw_frames) {
    if (full_draw_frames != NULL) {
        *full_draw_frames = g_state.full_draw_frames;
    }
    if (dirty_draw_frames != NULL) {
        *dirty_draw_frames = g_state.dirty_draw_frames;
    }
}
