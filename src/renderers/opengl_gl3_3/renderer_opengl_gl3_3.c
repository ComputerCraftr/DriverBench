#include "renderer_opengl_gl3_3.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "../../config/benchmark_config.h"
#include "../../core/db_core.h"
#include "../renderer_benchmark_common.h"
#include "../renderer_gl_api.h"
#include "../renderer_gl_common.h"
#include "../renderer_history_common.h"
#include "../renderer_snake_common.h"
#include "../renderer_viewport_common.h"

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
    unsigned int fallback_tex;
    db_renderer_frame_stats_t frame;
    db_benchmark_runtime_init_t runtime;
    db_gl_vertex_init_t vertex;
    int u_gradient_head_row;
    int u_gradient_window_rows;
    int u_band_count;
    int u_grid_base_color;
    int u_grid_cols;
    int u_grid_rows;
    int u_grid_target_color;
    int u_history_tex;
    int u_direction_flag;
    int u_palette_cycle;
    int u_pattern_seed;
    int u_render_mode;
    int u_snake_batch_size;
    int u_snake_cursor;
    int u_snake_phase_completed;
    int u_snake_shape_index;
    int u_frame_index;
    int history_height;
    unsigned int history_fbo[2];
    db_history_pair_state_t history_pair;
    unsigned int history_tex[2];
    int history_width;
    db_gl_viewport_cache_t viewport;
    unsigned int program;
    int uniform_direction_flag_cache;
    int uniform_snake_phase_completed_cache;
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
    unsigned int vao;
    db_gl_buffer_cache_t buffers;
} renderer_state_t;

typedef struct {
    uint32_t draw_vertex_count;
} db_gl3_scissor_draw_ctx_t;

typedef struct {
    const unsigned int *history_fbo;
} db_gl3_seed_history_targets_ctx_t;

static renderer_state_t g_state = {0};

static void db_gl3_apply_scissor_draw_rect(const db_gl_scissor_rect_t *rect,
                                           void *user_data) {
    const db_gl3_scissor_draw_ctx_t *ctx =
        (const db_gl3_scissor_draw_ctx_t *)user_data;
    if ((rect == NULL) || (ctx == NULL) || (ctx->draw_vertex_count == 0U)) {
        return;
    }
    db_gl_set_scissor_rect(rect->x_px, rect->y_px, rect->width_px,
                           rect->height_px);
    db_gl_draw_arrays_triangles(
        0, db_gl_draw_vertex_count_i32(BACKEND_NAME, ctx->draw_vertex_count));
}

static void db_gl3_seed_history_targets_clear_cb(const float rgba[4],
                                                 void *user_data) {
    const db_gl3_seed_history_targets_ctx_t *ctx =
        (const db_gl3_seed_history_targets_ctx_t *)user_data;
    if ((rgba == NULL) || (ctx == NULL) || (ctx->history_fbo == NULL)) {
        return;
    }
    for (size_t target_index = 0U; target_index < 2U; target_index++) {
        db_gl_bind_framebuffer(GL_FRAMEBUFFER, ctx->history_fbo[target_index]);
        db_gl_clear_color_rgba(rgba[0], rgba[1], rgba[2], rgba[3]);
        db_gl_clear_color_buffer();
    }
}

static void db_gl3_refresh_capability_mode(void) {
    const db_history_runtime_mode_flags_t mode_flags =
        db_history_runtime_mode_flags(&g_state.runtime);
    const char *draw_mode = db_gl_capability_mode_draw_select(
        0, mode_flags.uses_history_pipeline, 1);
    db_gl_capability_mode_compose(
        g_state.capability_mode, sizeof(g_state.capability_mode), draw_mode,
        DB_GL_CAP_UPLOAD_VBO,
        db_runtime_backbuffer_replay_enabled(&g_state.runtime));
}

static void db_set_uniform1i_if_changed(int location, int *cache, int value) {
    if ((location >= 0) && (*cache != value)) {
        db_gl_uniform1i(location, value);
        *cache = value;
    }
}

static void db_set_uniform1ui_if_changed(int location, uint32_t *cache,
                                         int *cache_valid, uint32_t value) {
    if ((location >= 0) && ((*cache_valid == 0) || (*cache != value))) {
        db_gl_uniform1ui(location, value);
        *cache = value;
        *cache_valid = 1;
    }
}

static void db_gl3_destroy_history_targets(void) {
    for (size_t i = 0U; i < 2U; i++) {
        if (g_state.history_fbo[i] != 0U) {
            db_gl_delete_framebuffers(1, &g_state.history_fbo[i]);
            g_state.history_fbo[i] = 0U;
        }
        db_gl_texture_delete_if_valid(&g_state.history_tex[i]);
    }
    g_state.history_width = 0;
    g_state.history_height = 0;
    db_history_pair_state_reset(&g_state.history_pair);
}

static void db_gl3_create_fallback_texture(void) {
    if (g_state.fallback_tex != 0U) {
        return;
    }
    uint8_t fallback_rgba[4] = {0U, 0U, 0U, 255U};
    db_history_seed_background_rgba8(&g_state.runtime, fallback_rgba);
    if (db_gl_texture_create_rgba(&g_state.fallback_tex, 1, 1, GL_RGBA8,
                                  fallback_rgba) == 0) {
        failf("failed to create fallback history texture");
    }
}

static void db_gl3_ensure_history_targets(int viewport_width_px,
                                          int viewport_height_px) {
    const db_history_pattern_mode_flags_t pattern_flags =
        db_history_pattern_mode_flags(g_state.runtime.pattern);
    if (pattern_flags.uses_history_pipeline == 0) {
        return;
    }

    const int viewport_width = viewport_width_px;
    const int viewport_height = viewport_height_px;
    if ((viewport_width <= 0) || (viewport_height <= 0)) {
        return;
    }
    int prev_read_fbo = 0;
    int prev_draw_fbo = 0;
    db_gl_get_integerv(GL_READ_FRAMEBUFFER_BINDING, &prev_read_fbo);
    db_gl_get_integerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_draw_fbo);
    if ((g_state.history_width == viewport_width) &&
        (g_state.history_height == viewport_height) &&
        (g_state.history_tex[0] != 0U) && (g_state.history_tex[1] != 0U) &&
        (g_state.history_fbo[0] != 0U) && (g_state.history_fbo[1] != 0U) &&
        (g_state.history_pair.read_index >= 0)) {
        return;
    }

    const int old_width = g_state.history_width;
    const int old_height = g_state.history_height;
    const int old_read_index = g_state.history_pair.read_index;
    const int old_initialized = g_state.history_pair.is_valid;
    const unsigned int old_tex0 = g_state.history_tex[0];
    const unsigned int old_tex1 = g_state.history_tex[1];
    const unsigned int old_fbo0 = g_state.history_fbo[0];
    const unsigned int old_fbo1 = g_state.history_fbo[1];

    unsigned int new_tex[2] = {0U, 0U};
    unsigned int new_fbo[2] = {0U, 0U};
    for (size_t i = 0U; i < 2U; i++) {
        if (db_gl_texture_create_rgba(&new_tex[i], viewport_width,
                                      viewport_height, GL_RGBA8, NULL) == 0) {
            failf("failed to create history texture");
        }

        db_gl_gen_framebuffers(1, &new_fbo[i]);
        db_gl_bind_framebuffer(GL_FRAMEBUFFER, new_fbo[i]);
        db_gl_framebuffer_texture_2d(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                     GL_TEXTURE_2D, new_tex[i], 0);
        if (db_gl_check_framebuffer_status(GL_FRAMEBUFFER) !=
            GL_FRAMEBUFFER_COMPLETE) {
            failf("history framebuffer incomplete");
        }
    }

    int copied_history = 0;
    if ((old_initialized != 0) && (old_width > 0) && (old_height > 0) &&
        ((old_read_index == 0) || (old_read_index == 1))) {
        const unsigned int old_read_tex =
            (old_read_index == 0) ? old_tex0 : old_tex1;
        if (old_read_tex != 0U) {
            unsigned int read_fbo = 0U;
            db_gl_gen_framebuffers(1, &read_fbo);
            db_gl_bind_framebuffer(GL_READ_FRAMEBUFFER, read_fbo);
            db_gl_framebuffer_texture_2d(GL_READ_FRAMEBUFFER,
                                         GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                         old_read_tex, 0);
            if (db_gl_check_framebuffer_status(GL_READ_FRAMEBUFFER) ==
                GL_FRAMEBUFFER_COMPLETE) {
                copied_history = 1;
                for (size_t i = 0U; i < 2U; i++) {
                    db_gl_bind_framebuffer(GL_DRAW_FRAMEBUFFER, new_fbo[i]);
                    db_gl_blit_framebuffer(0, 0, old_width, old_height, 0, 0,
                                           viewport_width, viewport_height,
                                           GL_COLOR_BUFFER_BIT, GL_NEAREST);
                    if (db_gl_get_error_code() != GL_NO_ERROR) {
                        copied_history = 0;
                        break;
                    }
                }
            }
            db_gl_bind_framebuffer(GL_READ_FRAMEBUFFER, prev_read_fbo);
            db_gl_bind_framebuffer(GL_DRAW_FRAMEBUFFER, prev_draw_fbo);
            if (read_fbo != 0U) {
                db_gl_delete_framebuffers(1, &read_fbo);
            }
        }
    }

    const db_history_resize_preserve_policy_t resize_policy =
        db_history_resize_preserve_policy_for_pattern(g_state.runtime.pattern,
                                                      1, copied_history, 1);
    const db_gl3_seed_history_targets_ctx_t seed_ctx = {.history_fbo = new_fbo};
    (void)db_history_run_seed_clear_if_needed(
        resize_policy.should_seed_targets, &g_state.runtime,
        db_gl3_seed_history_targets_clear_cb, (void *)&seed_ctx);

    if (old_fbo0 != 0U) {
        db_gl_delete_framebuffers(1, &old_fbo0);
    }
    if (old_fbo1 != 0U) {
        db_gl_delete_framebuffers(1, &old_fbo1);
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
    db_history_apply_resize_preserve_policy(&resize_policy, &g_state.runtime,
                                            &g_state.history_pair, NULL);
    g_state.history_pair.read_index = 0;
    db_gl_bind_framebuffer(GL_READ_FRAMEBUFFER, prev_read_fbo);
    db_gl_bind_framebuffer(GL_DRAW_FRAMEBUFFER, prev_draw_fbo);
}

static unsigned int compile_shader(unsigned int shader_type,
                                   const char *source) {
    unsigned int shader = db_gl_create_shader(shader_type);
    db_gl_shader_source_single(shader, source);
    db_gl_compile_shader(shader);

    int ok = 0;
    db_gl_get_shader_iv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_FALSE) {
        char log_msg[SHADER_LOG_MSG_CAPACITY];
        int msg_len = 0;
        db_gl_get_shader_info_log(shader, sizeof(log_msg), &msg_len, log_msg);
        const int msg_len_i32 =
            db_checked_int_to_i32(BACKEND_NAME, "shader_log_msg_len", msg_len);
        failf("Shader compile failed (%u): %.*s", (unsigned)shader_type,
              msg_len_i32, log_msg);
    }
    return shader;
}

static unsigned int build_program_from_files(const char *vert_shader_path,
                                             const char *frag_shader_path) {
    char *vert_src = db_read_text_file_or_fail(BACKEND_NAME, vert_shader_path);
    char *frag_src = db_read_text_file_or_fail(BACKEND_NAME, frag_shader_path);

    unsigned int vert = compile_shader(GL_VERTEX_SHADER, vert_src);
    unsigned int frag = compile_shader(GL_FRAGMENT_SHADER, frag_src);
    free(vert_src);
    free(frag_src);

    unsigned int program = db_gl_create_program();
    db_gl_attach_shader(program, vert);
    db_gl_attach_shader(program, frag);
    db_gl_link_program(program);
    db_gl_delete_shader(vert);
    db_gl_delete_shader(frag);

    int ok = 0;
    db_gl_get_program_iv(program, GL_LINK_STATUS, &ok);
    if (ok == GL_FALSE) {
        char log_msg[SHADER_LOG_MSG_CAPACITY];
        int msg_len = 0;
        db_gl_get_program_info_log(program, sizeof(log_msg), &msg_len, log_msg);
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

    db_gl_gen_vertex_arrays(1, &g_state.vao);
    unsigned int vbo_u32 = 0U;
    if (db_gl_vbo_create_or_zero(&vbo_u32) != 0) {
        g_state.buffers.vbo = vbo_u32;
    }
    if (g_state.buffers.vbo == 0U) {
        failf("failed to create GL array buffer");
    }
    db_gl_bind_vertex_array(g_state.vao);
    if (db_gl_bind_array_buffer_cached(
            g_state.buffers.vbo, &g_state.buffers.bound_array_buffer) == 0) {
        failf("failed to bind GL array buffer");
    }
    g_state.buffers.vbo_bytes = (size_t)g_state.vertex.draw_vertex_count *
                                DB_VERTEX_FLOAT_STRIDE * sizeof(float);
    if (db_gl_vbo_init_data(g_state.buffers.vbo_bytes, g_state.vertex.vertices,
                            GL_DYNAMIC_DRAW) == 0) {
        failf("failed to initialize GL array buffer");
    }

    db_gl_enable_vertex_attrib_array(ATTR_POSITION_LOC);
    db_gl_vertex_attrib_pointer_2f(
        ATTR_POSITION_LOC, (int)(DB_VERTEX_FLOAT_STRIDE * sizeof(float)), 0U);
    db_gl_enable_vertex_attrib_array(ATTR_COLOR_LOC);
    db_gl_vertex_attrib_pointer_3f(
        ATTR_COLOR_LOC, (int)(DB_VERTEX_FLOAT_STRIDE * sizeof(float)),
        DB_VERTEX_POSITION_FLOAT_COUNT * sizeof(float));

    g_state.vertex.upload = (db_gl_upload_probe_result_t){0};
    db_gl_upload_probe_result_t probe_result = {0};
    db_gl_context_probe_upload_capabilities(
        g_state.buffers.vbo_bytes, g_state.vertex.vertices, &probe_result);
    g_state.vertex.upload = probe_result;
    db_gl3_refresh_capability_mode();
    infof("using capability mode: %s",
          db_renderer_opengl_gl3_3_capability_mode());

    g_state.program = build_program_from_files(OPENGL_GL3_3_VERT_SHADER_PATH,
                                               OPENGL_GL3_3_FRAG_SHADER_PATH);
    db_gl_use_program(g_state.program);
    g_state.u_render_mode =
        db_gl_get_uniform_location(g_state.program, "u_render_mode");
    g_state.u_band_count =
        db_gl_get_uniform_location(g_state.program, "u_band_count");
    g_state.u_direction_flag =
        db_gl_get_uniform_location(g_state.program, "u_direction_flag");
    g_state.u_snake_cursor =
        db_gl_get_uniform_location(g_state.program, "u_snake_cursor");
    g_state.u_snake_phase_completed =
        db_gl_get_uniform_location(g_state.program, "u_snake_phase_completed");
    g_state.u_snake_batch_size =
        db_gl_get_uniform_location(g_state.program, "u_snake_batch_size");
    g_state.u_grid_cols =
        db_gl_get_uniform_location(g_state.program, "u_grid_cols");
    g_state.u_grid_rows =
        db_gl_get_uniform_location(g_state.program, "u_grid_rows");
    g_state.u_gradient_head_row =
        db_gl_get_uniform_location(g_state.program, "u_gradient_head_row");
    g_state.u_snake_shape_index =
        db_gl_get_uniform_location(g_state.program, "u_snake_shape_index");
    g_state.u_frame_index =
        db_gl_get_uniform_location(g_state.program, "u_frame_index");
    g_state.u_gradient_window_rows =
        db_gl_get_uniform_location(g_state.program, "u_gradient_window_rows");
    g_state.u_palette_cycle =
        db_gl_get_uniform_location(g_state.program, "u_palette_cycle");
    g_state.u_pattern_seed =
        db_gl_get_uniform_location(g_state.program, "u_pattern_seed");
    g_state.u_grid_base_color =
        db_gl_get_uniform_location(g_state.program, "u_grid_base_color");
    g_state.u_grid_target_color =
        db_gl_get_uniform_location(g_state.program, "u_grid_target_color");
    g_state.u_history_tex =
        db_gl_get_uniform_location(g_state.program, "u_history_tex");
    g_state.uniform_direction_flag_cache = -1;
    g_state.uniform_snake_phase_completed_cache = -1;
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
        db_gl_uniform1ui(g_state.u_render_mode,
                         db_checked_int_to_u32(BACKEND_NAME, "u_render_mode",
                                               g_state.runtime.pattern));
    }
    db_gl_uniform3f(g_state.u_grid_base_color, BENCH_GRID_PHASE0_R,
                    BENCH_GRID_PHASE0_G, BENCH_GRID_PHASE0_B);
    db_gl_uniform3f(g_state.u_grid_target_color, BENCH_GRID_PHASE1_R,
                    BENCH_GRID_PHASE1_G, BENCH_GRID_PHASE1_B);
    if (g_state.u_band_count >= 0) {
        db_gl_uniform1ui(g_state.u_band_count, BENCH_BANDS);
    }
    db_gl_uniform1ui(g_state.u_grid_cols, db_grid_cols_effective());
    db_gl_uniform1ui(g_state.u_grid_rows, db_grid_rows_effective());
    db_gl_uniform1ui(g_state.u_gradient_window_rows,
                     db_gradient_window_rows_effective());
    if (g_state.u_pattern_seed >= 0) {
        db_gl_uniform1ui(g_state.u_pattern_seed, g_state.runtime.pattern_seed);
    }
    if (g_state.u_history_tex >= 0) {
        db_gl_uniform1i(g_state.u_history_tex, 0);
    }
    db_gl3_create_fallback_texture();
    if (g_state.fallback_tex != 0U) {
        db_gl_active_texture(GL_TEXTURE0);
        db_gl_texture_bind_2d(g_state.fallback_tex);
    }

    const db_history_pattern_mode_flags_t pattern_flags =
        db_history_pattern_mode_flags(g_state.runtime.pattern);
    if ((pattern_flags.is_bands != 0) && (g_state.u_frame_index >= 0)) {
        db_gl_uniform1ui(g_state.u_frame_index, 0);
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
    db_set_uniform1i_if_changed(g_state.u_direction_flag,
                                &g_state.uniform_direction_flag_cache,
                                g_state.runtime.gradient.direction_down);
    db_set_uniform1i_if_changed(g_state.u_snake_phase_completed,
                                &g_state.uniform_snake_phase_completed_cache,
                                g_state.runtime.snake.phase_completed);
}

void db_renderer_opengl_gl3_3_render_frame(uint32_t frame_index,
                                           int viewport_width_px,
                                           int viewport_height_px) {
    const db_renderer_viewport_state_t viewport_state =
        db_renderer_resolve_viewport_state(BACKEND_NAME, &viewport_width_px,
                                           &viewport_height_px,
                                           &g_state.viewport.last_viewport_w,
                                           &g_state.viewport.last_viewport_h);

    db_gl3_ensure_history_targets(g_state.viewport.last_viewport_w,
                                  g_state.viewport.last_viewport_h);
    const db_history_runtime_mode_flags_t mode_flags =
        db_history_runtime_mode_flags(&g_state.runtime);
    db_snake_plan_t snake_plan = {0};
    db_snake_step_target_t snake_target = {0};
    int snake_plan_valid = 0;
    if (mode_flags.is_snake_history_texture != 0) {
        const db_history_snake_step_eval_t eval =
            db_history_eval_snake_step_from_runtime(&g_state.runtime);
        snake_plan = eval.plan;
        snake_target = eval.target;
        snake_plan_valid = 1;
        if (snake_target.has_next_direction_flag != 0) {
            db_set_uniform1i_if_changed(g_state.u_direction_flag,
                                        &g_state.uniform_direction_flag_cache,
                                        snake_plan.clearing_phase);
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
            snake_plan.batch_size);
        db_set_uniform1i_if_changed(
            g_state.u_snake_phase_completed,
            &g_state.uniform_snake_phase_completed_cache,
            snake_plan.phase_completed);
        db_history_apply_snake_step_to_runtime(&g_state.runtime, &eval);
    } else if (mode_flags.is_gradient != 0) {
        const db_gradient_damage_plan_t plan = db_gradient_step_from_runtime(
            g_state.runtime.pattern, g_state.runtime.gradient.head_row,
            g_state.runtime.gradient.direction_down,
            g_state.runtime.gradient.cycle_index,
            g_state.runtime.bench_speed_step);
        db_gradient_apply_step_to_runtime(&g_state.runtime, &plan);
        db_set_uniform1ui_if_changed(
            g_state.u_gradient_head_row,
            &g_state.uniform_gradient_head_row_cache,
            &g_state.uniform_gradient_head_row_cache_valid,
            plan.render_state.head_row);
        db_set_uniform1i_if_changed(g_state.u_direction_flag,
                                    &g_state.uniform_direction_flag_cache,
                                    plan.render_state.direction_down);
        db_set_uniform1ui_if_changed(g_state.u_palette_cycle,
                                     &g_state.uniform_palette_cycle_cache,
                                     &g_state.uniform_palette_cycle_cache_valid,
                                     plan.render_state.cycle_index);
    } else if (mode_flags.is_bands != 0) {
        if (g_state.u_frame_index >= 0) {
            db_gl_uniform1ui(g_state.u_frame_index, frame_index);
        }
    }

    if (mode_flags.uses_history_pipeline == 0) {
        if (g_state.fallback_tex != 0U) {
            db_gl_active_texture(GL_TEXTURE0);
            db_gl_texture_bind_2d(g_state.fallback_tex);
        }
        db_gl_draw_arrays_triangles(
            0, db_gl_draw_vertex_count_i32(BACKEND_NAME,
                                           g_state.vertex.draw_vertex_count));
        const db_history_draw_stats_counted_t counted_draw =
            db_history_classify_counted_draw(
                1, 0, (g_state.vertex.draw_vertex_count > 0U) ? 1U : 0U);
        db_history_record_draw_stats(
            &g_state.frame.full_draw_frames, &g_state.frame.dirty_draw_frames,
            counted_draw.counted_full_draw, counted_draw.counted_dirty_draw);
        g_state.frame.state_hash =
            db_benchmark_runtime_state_hash_cross_renderer(
                &g_state.runtime, g_state.frame.frame_index,
                db_grid_cols_effective(), db_grid_rows_effective());
        g_state.frame.frame_index++;
        return;
    }
    if ((g_state.history_pair.read_index < 0) || (g_state.history_width <= 0) ||
        (g_state.history_height <= 0)) {
        g_state.frame.state_hash =
            db_benchmark_runtime_state_hash_cross_renderer(
                &g_state.runtime, g_state.frame.frame_index,
                db_grid_cols_effective(), db_grid_rows_effective());
        g_state.frame.frame_index++;
        return;
    }

    const int read_index = db_history_pair_read_index(&g_state.history_pair);
    const int write_index = db_history_pair_write_index(&g_state.history_pair);
    db_gl_active_texture(GL_TEXTURE0);
    db_gl_texture_bind_2d(g_state.history_tex[read_index]);

    int prev_read_fbo = 0;
    int prev_draw_fbo = 0;
    db_gl_get_integerv(GL_READ_FRAMEBUFFER_BINDING, &prev_read_fbo);
    db_gl_get_integerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_draw_fbo);

    if (viewport_state.viewport_changed != 0) {
        db_gl_set_viewport_px(g_state.viewport.last_viewport_w,
                              g_state.viewport.last_viewport_h);
    }
    int drew_dirty_history = 0;
    if (db_history_should_use_snake_history_scissor_pass(
            mode_flags.uses_dirty_backbuffer_mode, snake_plan_valid) != 0) {
        db_snake_col_span_t spans[BENCH_SNAKE_PHASE_WINDOW_TILES * 2U] = {
            {0U, 0U, 0U}};
        const size_t max_spans = sizeof(spans) / sizeof(spans[0]);
        const size_t span_count = db_snake_collect_damage_spans_for_plan(
            spans, max_spans, &snake_target.region, &snake_plan, NULL);
        if (span_count > 0U) {
            const uint32_t total_cols = db_grid_cols_effective();
            const uint32_t total_rows = db_grid_rows_effective();
            db_gl_bind_framebuffer(GL_READ_FRAMEBUFFER,
                                   g_state.history_fbo[read_index]);
            db_gl_bind_framebuffer(GL_DRAW_FRAMEBUFFER,
                                   g_state.history_fbo[write_index]);
            db_gl_blit_framebuffer(
                0, 0, g_state.history_width, g_state.history_height, 0, 0,
                g_state.history_width, g_state.history_height,
                GL_COLOR_BUFFER_BIT, GL_NEAREST);

            db_gl_bind_framebuffer(GL_FRAMEBUFFER,
                                   g_state.history_fbo[write_index]);
            db_gl_set_scissor_enabled(1);
            const db_gl3_scissor_draw_ctx_t scissor_ctx = {
                .draw_vertex_count = g_state.vertex.draw_vertex_count};
            const size_t applied_rect_count =
                db_gl_for_each_span_scissor_rect_merged(
                    spans, span_count, total_cols, total_rows,
                    g_state.viewport.last_viewport_w,
                    g_state.viewport.last_viewport_h,
                    db_gl3_apply_scissor_draw_rect, (void *)&scissor_ctx);
            db_gl_set_scissor_enabled(0);
            if (applied_rect_count > 0U) {
                drew_dirty_history = 1;
            }
        }
    }
    if (drew_dirty_history != 0) {
        const db_history_draw_stats_counted_t counted_draw =
            db_history_classify_counted_draw(0, 1, 1U);
        db_history_record_draw_stats(
            &g_state.frame.full_draw_frames, &g_state.frame.dirty_draw_frames,
            counted_draw.counted_full_draw, counted_draw.counted_dirty_draw);
    } else {
        db_gl_bind_framebuffer(GL_FRAMEBUFFER,
                               g_state.history_fbo[write_index]);
        db_gl_draw_arrays_triangles(
            0, db_gl_draw_vertex_count_i32(BACKEND_NAME,
                                           g_state.vertex.draw_vertex_count));
        const db_history_draw_stats_counted_t counted_draw =
            db_history_classify_counted_draw(
                1, 0, (g_state.vertex.draw_vertex_count > 0U) ? 1U : 0U);
        db_history_record_draw_stats(
            &g_state.frame.full_draw_frames, &g_state.frame.dirty_draw_frames,
            counted_draw.counted_full_draw, counted_draw.counted_dirty_draw);
    }

    db_gl_bind_framebuffer(GL_READ_FRAMEBUFFER,
                           g_state.history_fbo[write_index]);
    db_gl_bind_framebuffer(GL_DRAW_FRAMEBUFFER, 0);
    db_gl_blit_framebuffer(0, 0, g_state.history_width, g_state.history_height,
                           0, 0, g_state.history_width, g_state.history_height,
                           GL_COLOR_BUFFER_BIT, GL_NEAREST);

    db_gl_bind_framebuffer(GL_READ_FRAMEBUFFER, prev_read_fbo);
    db_gl_bind_framebuffer(GL_DRAW_FRAMEBUFFER, prev_draw_fbo);
    db_history_pair_flip_to_write(&g_state.history_pair);
    g_state.frame.state_hash = db_benchmark_runtime_state_hash_cross_renderer(
        &g_state.runtime, g_state.frame.frame_index, db_grid_cols_effective(),
        db_grid_rows_effective());
    g_state.frame.frame_index++;
}

void db_renderer_opengl_gl3_3_shutdown(void) {
    if (g_state.vertex.upload.persistent_mapped_ptr != NULL) {
        (void)db_gl_bind_array_buffer_cached(
            g_state.buffers.vbo, &g_state.buffers.bound_array_buffer);
        db_gl_unmap_current_array_buffer();
    }
    db_gl3_destroy_history_targets();
    db_gl_texture_delete_if_valid(&g_state.fallback_tex);
    db_gl_delete_program(g_state.program);
    db_gl_vbo_delete_if_valid(g_state.buffers.vbo);
    db_gl_delete_vertex_arrays(1, &g_state.vao);
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
    return g_state.frame.state_hash;
}

void db_renderer_opengl_gl3_3_draw_stats(uint64_t *full_draw_frames,
                                         uint64_t *dirty_draw_frames) {
    if (full_draw_frames != NULL) {
        *full_draw_frames = g_state.frame.full_draw_frames;
    }
    if (dirty_draw_frames != NULL) {
        *dirty_draw_frames = g_state.frame.dirty_draw_frames;
    }
}
