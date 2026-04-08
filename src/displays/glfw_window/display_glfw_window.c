#include "../../renderers/renderer_benchmark_runtime.h"
#include "../../renderers/renderer_benchmark_types.h"
#ifdef DB_HAS_VULKAN_API
#define GLFW_INCLUDE_VULKAN
#endif
#include <GLFW/glfw3.h>

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "../../core/db_alloc_policy.h"
#include "../../core/db_buffer_convert.h"
#include "../../core/db_core.h"
#include "../../core/db_hash.h"
#include "../../core/db_numeric.h"
#include "../../driverbench_config.h"
#include "../../renderers/cpu_renderer/renderer_cpu_renderer.h"
#include "../../renderers/renderer_gl_common.h"
#include "../../renderers/renderer_identity.h"
#ifdef DB_HAS_VULKAN_API
#include "../../renderers/vulkan_1_2_multi_gpu/renderer_vulkan_1_2_multi_gpu.h"
#endif
#include "../../config/benchmark_config.h"
#include "../../config/runtime_options.h"
#include "../display_dispatch.h"
#include "../display_frame_loop_common.h"
#include "../display_gl_hash_readback_common.h"
#include "../display_gl_renderer_select_common.h"
#include "../display_gl_runtime_common.h"
#include "../display_hash_common.h"
#include "../display_runtime_config_common.h"
#include "../display_types.h"
#include "display_glfw_window_common.h"

#define DB_CAP_MODE_CPU_GLFW_PBO "cpu_glfw_window_pbo"
#define DB_CAP_MODE_CPU_GLFW_PBO_HDR "cpu_glfw_window_pbo_hdr_rgba16f"
#define DB_CAP_MODE_CPU_GLFW_TEX_SUB_IMAGE "cpu_glfw_window_tex_sub_image"
#define DB_CAP_MODE_CPU_GLFW_TEX_SUB_IMAGE_HDR                                 \
    "cpu_glfw_window_tex_sub_image_hdr_rgba16f"
#define DB_CPU_DEBUG_CLEAR_CHUNK_ROWS 64U
#ifdef DB_HAS_VULKAN_API
typedef struct {
    const char *backend_name;
    db_display_hash_tracker_t *state_hash_tracker;
    db_display_hash_tracker_t *output_hash_tracker;
    int state_hash_enabled;
    int output_hash_enabled;
} db_glfw_vulkan_loop_ctx_t;
#endif

typedef struct {
    db_gl_shadow_present_state_t shared;
    int last_viewport_w;
    int last_viewport_h;
    // Debug-only scratch buffer used to clear the CPU present texture without
    // allocating in the hot path. Sized for chunked row uploads.
    uint8_t *debug_clear_buf;
    size_t debug_clear_buf_bytes;
    uint32_t debug_clear_row_bytes;
    uint32_t debug_clear_chunk_rows;
    uint32_t debug_clear_pixel_width;
    uint8_t debug_clear_rgba[4];
    int debug_clear_ready;
} db_cpu_present_gl_state_t;

typedef enum {
    DB_GLFW_CPU_PRESENT_FORMAT_RGBA8 = 0,
    DB_GLFW_CPU_PRESENT_FORMAT_RGBA16F = 1,
} db_glfw_cpu_present_format_t;

typedef struct {
    const char *api_name;
    const char *capability_mode;
    double next_progress_log_due_ms;
    db_display_frame_step_t frame_step;
    db_display_hash_tracker_t *state_hash_tracker;
    db_display_hash_tracker_t *framebuffer_hash_tracker;
    int framebuffer_hash_uses_rgba16f;
    db_gl_framebuffer_hash_scratch_t *hash_rgba8_scratch;
    db_gl_framebuffer_hash_f16_scratch_t *hash_rgba16f_scratch;
    int state_hash_enabled;
    int output_hash_enabled;
    int debug_clear_default_framebuffer;
    db_glfw_cpu_present_format_t selected_format;
    db_cpu_present_gl_state_t *present;
    uint32_t work_unit_count;
    GLFWwindow *window;
} db_glfw_cpu_loop_ctx_t;

typedef struct {
    db_cpu_present_gl_state_t *state;
    int debug_clear_default_framebuffer;
    int selected_present_uses_rgba16f;
} db_cpu_present_prepare_upload_target_ctx_t;

typedef struct {
    int hdr_explicit_requested;
    db_glfw_cpu_present_format_t selected_format;
    const char *capability_mode;
} db_glfw_cpu_present_mode_t;

typedef struct {
    const char *backend_name;
    const char *capability_mode;
    const char *renderer_name;
    db_display_gl_renderer_ops_t renderer_ops;
    db_display_frame_step_t frame_step;
    db_display_hash_tracker_t *state_hash_tracker;
    db_display_hash_tracker_t *framebuffer_hash_tracker;
    int framebuffer_hash_uses_rgba16f;
    db_gl_framebuffer_hash_scratch_t *hash_rgba8_scratch;
    db_gl_framebuffer_hash_f16_scratch_t *hash_rgba16f_scratch;
    double next_progress_log_due_ms;
    int state_hash_enabled;
    int output_hash_enabled;
    int debug_clear_default_framebuffer;
    uint32_t renderer_preserved_framebuffer_count;
    uint32_t work_unit_count;
    GLFWwindow *window;
} db_glfw_opengl_loop_ctx_t;

static GLFWwindow *db_glfw_create_renderer_window(
    const char *backend_name, const db_display_gl_context_policy_t *policy,
    int swap_interval, db_glfw_window_visibility_t visibility,
    int *out_context_is_gles) {
    if (policy == NULL) {
        return NULL;
    }
    if (policy->allow_gles1_1_fallback != 0) {
        return db_glfw_create_gl1_5_or_gles1_1_window(
            backend_name, "OpenGL 1.5/GLES1.1 GLFW DriverBench",
            BENCH_WINDOW_WIDTH_PX, BENCH_WINDOW_HEIGHT_PX,
            policy->requested_gl_major, policy->requested_gl_minor,
            swap_interval, out_context_is_gles, visibility);
    }
    if (out_context_is_gles != NULL) {
        *out_context_is_gles = 0;
    }
    return db_glfw_create_opengl_window(
        backend_name, "OpenGL 3.3 Shader GLFW DriverBench",
        BENCH_WINDOW_WIDTH_PX, BENCH_WINDOW_HEIGHT_PX,
        policy->requested_gl_major, policy->requested_gl_minor, 1,
        swap_interval, visibility);
}

#ifdef __linux__
static db_glfw_default_fb_probe_result_t
db_glfw_probe_and_log_default_framebuffer_behavior(
    const char *backend_name, db_gl_renderer_t renderer,
    const db_display_gl_context_policy_t *policy, int swap_interval) {
    db_glfw_default_fb_probe_result_t result = {0};
    const int probe_window_hidden = 1;
    int context_is_gles = 0;
    GLFWwindow *window =
        db_glfw_create_renderer_window(backend_name, policy, swap_interval,
                                       probe_window_hidden, &context_is_gles);
    (void)db_display_require_gl_runtime_for_renderer(
        (db_gl_proc_resolver_fn_t)glfwGetProcAddress, renderer, backend_name,
        (policy->allow_gles1_1_fallback != 0) ? context_is_gles : -1);
    result =
        db_glfw_probe_and_log_default_framebuffer_reuse(backend_name, window);
    db_glfw_destroy_window(window);
    return result;
}
#endif

static void db_present_cpu_debug_clear_prepare(db_cpu_present_gl_state_t *state,
                                               uint32_t pixel_width) {
    if ((state == NULL) || (pixel_width == 0U)) {
        return;
    }

    const uint32_t row_bytes = db_checked_mul_u32(
        DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU, "cpu_debug_clear_row_bytes",
        pixel_width, DB_RGBA8_BYTES_PER_PIXEL);

    // Fixed chunk size to keep uploads bounded; this buffer is reused.
    const uint32_t chunk_bytes_u32 = db_checked_mul_u32(
        DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU, "cpu_debug_clear_chunk_bytes",
        row_bytes, DB_CPU_DEBUG_CLEAR_CHUNK_ROWS);
    const size_t chunk_bytes = (size_t)chunk_bytes_u32;
    if (chunk_bytes == 0U) {
        state->debug_clear_ready = 0;
        state->shared.texture_valid = 0;
        state->shared.texture_needs_full_upload = 1;
        return;
    }

    // If already prepared for this pixel width, nothing to do.
    if ((state->debug_clear_ready != 0) &&
        (state->debug_clear_pixel_width == pixel_width) &&
        (state->debug_clear_row_bytes == row_bytes) &&
        (state->debug_clear_chunk_rows == DB_CPU_DEBUG_CLEAR_CHUNK_ROWS) &&
        (state->debug_clear_buf != NULL) &&
        (state->debug_clear_buf_bytes == chunk_bytes)) {
        return;
    }

    // Allocate or grow the reusable scratch buffer.
    db_reserve_array_capacity_or_fail(
        (void **)&state->debug_clear_buf, &state->debug_clear_buf_bytes,
        chunk_bytes, chunk_bytes, sizeof(uint8_t), 0U,
        DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU, "debug_clear_buf");

    state->debug_clear_pixel_width = pixel_width;
    state->debug_clear_row_bytes = row_bytes;
    state->debug_clear_chunk_rows = DB_CPU_DEBUG_CLEAR_CHUNK_ROWS;
    // Cache clear color bytes.
    const double debug_clear_rgba01[4] = {
        BENCH_CLEAR_COLOR_R,
        BENCH_CLEAR_COLOR_G,
        BENCH_CLEAR_COLOR_B,
        BENCH_CLEAR_COLOR_A,
    };
    db_rgba01_to_u8_rgba4(debug_clear_rgba01, state->debug_clear_rgba);

    // Fill the buffer once via shared conversion helper.
    db_fill_rgba8_byte_pattern(state->debug_clear_buf,
                               chunk_bytes_u32 / DB_RGBA8_BYTES_PER_PIXEL,
                               state->debug_clear_rgba);

    state->debug_clear_ready = 1;
}

// Helper: clear the CPU present texture to the debug clear color (for debug
// mode).
static void db_present_cpu_clear_texture_debug(db_cpu_present_gl_state_t *state,
                                               uint32_t pixel_width,
                                               uint32_t pixel_height) {
    if ((state == NULL) || (state->shared.texture == 0U) ||
        (pixel_width == 0U) || (pixel_height == 0U)) {
        return;
    }

    // Must be prepared by init/resize; never allocate in the hot path.
    if ((state->debug_clear_ready == 0) || (state->debug_clear_buf == NULL) ||
        (state->debug_clear_pixel_width != pixel_width) ||
        (state->debug_clear_row_bytes == 0U) ||
        (state->debug_clear_chunk_rows == 0U)) {
        state->shared.texture_valid = 0;
        state->shared.texture_needs_full_upload = 1;
        return;
    }

    db_gl_texture_bind_2d(state->shared.texture);

    const uint32_t chunk_rows = state->debug_clear_chunk_rows;
    uint32_t row = 0U;
    while (row < pixel_height) {
        const uint32_t rows_left = pixel_height - row;
        const uint32_t upload_rows =
            (rows_left < chunk_rows) ? rows_left : chunk_rows;
        db_gl_texture_sub_image_2d_rgba(
            0,
            db_checked_u32_to_i32(DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU,
                                  "cpu_debug_clear_y", row),
            db_checked_u32_to_i32(DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU,
                                  "cpu_debug_clear_w", pixel_width),
            db_checked_u32_to_i32(DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU,
                                  "cpu_debug_clear_h", upload_rows),
            state->debug_clear_buf);
        row += upload_rows;
    }
}

static void
db_glfw_cpu_prepare_upload_target(db_gl_shadow_present_state_t *shared,
                                  uint32_t pixel_width, uint32_t pixel_height,
                                  void *user_data) {
    db_cpu_present_prepare_upload_target_ctx_t *ctx =
        (db_cpu_present_prepare_upload_target_ctx_t *)user_data;
    if ((ctx == NULL) || (ctx->state == NULL) || (shared == NULL)) {
        return;
    }
    if ((ctx->debug_clear_default_framebuffer != 0) &&
        (ctx->selected_present_uses_rgba16f == 0)) {
        db_present_cpu_debug_clear_prepare(ctx->state, pixel_width);
        db_present_cpu_clear_texture_debug(ctx->state, pixel_width,
                                           pixel_height);
    }
}

static void db_glfw_cpu_present_shadow_framebuffer(
    GLFWwindow *window, db_cpu_present_gl_state_t *state,
    const db_damage_block_t *blocks, size_t block_count,
    int debug_clear_default_framebuffer,
    db_glfw_cpu_present_format_t selected_format) {
    uint32_t pixel_width = 0U;
    uint32_t pixel_height = 0U;
    const void *selected_pixels = NULL;

    int framebuffer_width_px = 0;
    int framebuffer_height_px = 0;
    glfwGetFramebufferSize(window, &framebuffer_width_px,
                           &framebuffer_height_px);
    if (framebuffer_width_px <= 0 || framebuffer_height_px <= 0) {
        framebuffer_width_px = db_checked_u32_to_i32(
            DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU, "framebuffer_width_px",
            db_grid_cols_effective());
        framebuffer_height_px = db_checked_u32_to_i32(
            DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU, "framebuffer_height_px",
            db_grid_rows_effective());
    }

    const int viewport_changed =
        (state->last_viewport_w != framebuffer_width_px) ||
        (state->last_viewport_h != framebuffer_height_px);

    if (viewport_changed != 0) {
        state->last_viewport_w = framebuffer_width_px;
        state->last_viewport_h = framebuffer_height_px;
        db_gl_set_viewport_px(state->last_viewport_w, state->last_viewport_h);
    }

    // Do not clear here by default. Some renderers rely on keeping the prior
    // frame in the default framebuffer (damage-only updates) and will
    // clear/draw explicitly as needed.
    const int debug_clear = (debug_clear_default_framebuffer != 0) ? 1 : 0;
    // CPU present also clears the texture itself in debug mode.
    db_display_gl_debug_clear_default_framebuffer_if_enabled(debug_clear);

    if (selected_format == DB_GLFW_CPU_PRESENT_FORMAT_RGBA16F) {
        selected_pixels = (const void *)db_renderer_cpu_renderer_pixels_rgba16f(
            &pixel_width, &pixel_height);
        if ((selected_pixels == NULL) || (pixel_width == 0U) ||
            (pixel_height == 0U)) {
            db_failf(DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU,
                     "cpu renderer returned invalid HDR framebuffer");
        }
    } else {
        selected_pixels = (const void *)db_renderer_cpu_renderer_pixels_rgba8(
            &pixel_width, &pixel_height);
        if ((selected_pixels == NULL) || (pixel_width == 0U) ||
            (pixel_height == 0U)) {
            db_failf(DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU,
                     "cpu renderer returned invalid framebuffer");
        }
    }

    db_cpu_present_prepare_upload_target_ctx_t prepare_upload_target_ctx = {
        .state = state,
        .debug_clear_default_framebuffer = debug_clear,
        .selected_present_uses_rgba16f =
            (selected_format == DB_GLFW_CPU_PRESENT_FORMAT_RGBA16F) ? 1 : 0,
    };
    const db_gl_shadow_present_frame_t present_frame = {
        .state = &state->shared,
        .backend = DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU,
        .pixel_width = pixel_width,
        .pixel_height = pixel_height,
        .selected_pixels = selected_pixels,
        .damage_blocks = blocks,
        .damage_block_count = block_count,
        .prepare_upload_target_fn = db_glfw_cpu_prepare_upload_target,
        .prepare_upload_target_user_data = &prepare_upload_target_ctx,
    };
    db_gl_shadow_present_frame(&present_frame);
}

static void
db_glfw_cpu_present_damage_cb(const db_damage_block_t *damage_blocks,
                              size_t damage_count, void *user_data) {
    db_glfw_cpu_loop_ctx_t *ctx = (db_glfw_cpu_loop_ctx_t *)user_data;
    if (ctx == NULL) {
        return;
    }
    db_glfw_cpu_present_shadow_framebuffer(
        ctx->window, ctx->present, damage_blocks, damage_count,
        ctx->debug_clear_default_framebuffer, ctx->selected_format);
}

static uint64_t db_glfw_hash_presented_default_framebuffer_or_fail(
    const char *backend_name, GLFWwindow *window, int uses_rgba16f,
    db_gl_framebuffer_hash_scratch_t *rgba8_scratch,
    db_gl_framebuffer_hash_f16_scratch_t *rgba16f_scratch) {
    if ((backend_name == NULL) || (window == NULL)) {
        db_failf("display_glfw_window",
                 "invalid presented default-framebuffer hash inputs");
    }

    int framebuffer_width_px = 0;
    int framebuffer_height_px = 0;
    glfwGetFramebufferSize(window, &framebuffer_width_px,
                           &framebuffer_height_px);
    if (uses_rgba16f != 0) {
        if (rgba16f_scratch == NULL) {
            db_failf("display_glfw_window",
                     "missing RGBA16F hash scratch for presented framebuffer");
        }
        return db_gl_hash_framebuffer_rgba16f_or_fail(
            backend_name, framebuffer_width_px, framebuffer_height_px,
            rgba16f_scratch, 1);
    }
    if (rgba8_scratch == NULL) {
        db_failf("display_glfw_window",
                 "missing RGBA8 hash scratch for presented framebuffer");
    }
    const uint8_t *pixels = db_gl_read_framebuffer_rgba8_or_fail(
        backend_name, framebuffer_width_px, framebuffer_height_px,
        rgba8_scratch);
    return db_hash_rgba8_pixels_canonical(
        pixels,
        db_checked_int_to_u32(backend_name, "fb_w", framebuffer_width_px),
        db_checked_int_to_u32(backend_name, "fb_h", framebuffer_height_px),
        db_checked_int_to_size(backend_name, "fb_row_pixels",
                               framebuffer_width_px) *
            4U,
        1);
}

static db_glfw_cpu_present_mode_t
db_glfw_cpu_present_mode_or_fail(const db_cpu_present_gl_state_t *present) {
    if (present == NULL) {
        db_failf(DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU,
                 "missing cpu GLFW present state");
    }
    const db_display_cpu_hdr_option_state_t hdr_option =
        db_display_cpu_hdr_option_state();
    const int hdr_present_supported =
        (present->shared.runtime_supports_hdr_present != 0) ? 1 : 0;
    if ((hdr_option.option_explicitly_requests_hdr != 0) &&
        (hdr_present_supported == 0)) {
        db_failf(DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU,
                 "cpu_hdr requested but runtime has no float texture present "
                 "support");
    }

    const db_glfw_cpu_present_format_t selected_format =
        ((hdr_option.option_enables_hdr != 0) && (hdr_present_supported != 0))
            ? DB_GLFW_CPU_PRESENT_FORMAT_RGBA16F
            : DB_GLFW_CPU_PRESENT_FORMAT_RGBA8;
    const int uses_unpack_pbo = (present->shared.unpack_pbo != 0U) ? 1 : 0;
    const char *capability_mode = NULL;
    if (uses_unpack_pbo != 0) {
        capability_mode =
            (selected_format == DB_GLFW_CPU_PRESENT_FORMAT_RGBA16F)
                ? DB_CAP_MODE_CPU_GLFW_PBO_HDR
                : DB_CAP_MODE_CPU_GLFW_PBO;
    } else {
        capability_mode =
            (selected_format == DB_GLFW_CPU_PRESENT_FORMAT_RGBA16F)
                ? DB_CAP_MODE_CPU_GLFW_TEX_SUB_IMAGE_HDR
                : DB_CAP_MODE_CPU_GLFW_TEX_SUB_IMAGE;
    }

    return (db_glfw_cpu_present_mode_t){
        .hdr_explicit_requested = hdr_option.option_explicitly_requests_hdr,
        .selected_format = selected_format,
        .capability_mode = capability_mode,
    };
}

static db_display_frame_loop_result_t
db_glfw_cpu_frame(void *user_data, uint32_t frame_index, double elapsed_ms) {
    db_glfw_cpu_loop_ctx_t *ctx = (db_glfw_cpu_loop_ctx_t *)user_data;
    db_renderer_cpu_renderer_render_frame(frame_index);
    size_t damage_count = 0U;
    const db_damage_block_t *damage_blocks =
        db_renderer_cpu_renderer_damage_blocks(&damage_count);
    db_glfw_cpu_present_damage_cb(damage_blocks, damage_count, ctx);
    db_display_gl_frame_step(
        &ctx->frame_step, frame_index, elapsed_ms, 1,
        db_renderer_cpu_renderer_state_hash(), 1,
        db_glfw_hash_presented_default_framebuffer_or_fail(
            DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU, ctx->window,
            ctx->framebuffer_hash_uses_rgba16f, ctx->hash_rgba8_scratch,
            ctx->hash_rgba16f_scratch));

    glfwSwapBuffers(ctx->window);
    return DB_DISPLAY_FRAME_LOOP_CONTINUE;
}

static int db_run_glfw_window_cpu(const db_cli_config_t *cfg) {
    db_validate_runtime_environment(DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU,
                                    DB_RUNTIME_OPT_ALLOW_REMOTE_DISPLAY);
    db_install_signal_handlers();

    const db_display_runtime_hash_config_t runtime_hash_cfg =
        db_display_runtime_hash_config_from_cli(cfg, 0, 0);
    const db_display_runtime_config_t runtime_cfg = runtime_hash_cfg.runtime;
    const int swap_interval =
        ((cfg != NULL) && (cfg->vsync_enabled != 0)) ? 1 : 0;
    const db_display_hash_settings_t hash_settings =
        runtime_hash_cfg.hash_settings;

    const int gl_legacy_context_major = 2;
    const int gl_legacy_context_minor = 1;
    int is_gles = 0;
    GLFWwindow *window = db_glfw_create_gl1_5_or_gles1_1_window(
        DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU,
        "CPU Renderer GLFW DriverBench", BENCH_WINDOW_WIDTH_PX,
        BENCH_WINDOW_HEIGHT_PX, gl_legacy_context_major,
        gl_legacy_context_minor, swap_interval, &is_gles,
        ((cfg != NULL) && (cfg->glfw_window_hidden != 0))
            ? DB_GLFW_WINDOW_HIDDEN
            : DB_GLFW_WINDOW_VISIBLE);
    (void)db_display_require_gl_runtime_for_renderer(
        (db_gl_proc_resolver_fn_t)glfwGetProcAddress,
        DB_GL_RENDERER_GL1_5_GLES1_1, DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU,
        is_gles);
    db_cpu_present_gl_state_t present = {0};
    const db_display_cpu_hdr_option_state_t hdr_option =
        db_display_cpu_hdr_option_state();
    db_gl_shadow_present_init_runtime(&present.shared, 1,
                                      hdr_option.option_enables_hdr);
    const db_glfw_cpu_present_mode_t present_mode =
        db_glfw_cpu_present_mode_or_fail(&present);
    db_renderer_cpu_renderer_init_with_hdr_float_bo(
        (present_mode.selected_format == DB_GLFW_CPU_PRESENT_FORMAT_RGBA16F)
            ? 1
            : 0);

    // Prepare upload range scratch buffer up-front to avoid per-frame
    // allocations.
    uint32_t init_pixel_width = 0U;
    uint32_t init_pixel_height = 0U;
    if (present_mode.selected_format == DB_GLFW_CPU_PRESENT_FORMAT_RGBA16F) {
        (void)db_renderer_cpu_renderer_pixels_rgba16f(&init_pixel_width,
                                                      &init_pixel_height);
    } else {
        (void)db_renderer_cpu_renderer_pixels_rgba8(&init_pixel_width,
                                                    &init_pixel_height);
    }
    db_gl_shadow_present_prepare_texture(
        &present.shared, DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU,
        init_pixel_width, init_pixel_height);
    if ((runtime_cfg.debug_clear_default_framebuffer != 0) &&
        (present_mode.selected_format == DB_GLFW_CPU_PRESENT_FORMAT_RGBA8)) {
        db_present_cpu_debug_clear_prepare(&present, init_pixel_width);
    }
    db_gl_shadow_present_log_decision(
        DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU, "cpu present",
        (present_mode.selected_format == DB_GLFW_CPU_PRESENT_FORMAT_RGBA16F)
            ? 1
            : 0,
        present_mode.hdr_explicit_requested, &present.shared);

    const uint32_t work_unit_count = db_renderer_cpu_renderer_work_unit_count();
    const uint64_t bench_start_ns = db_now_ns_monotonic();
    db_display_dual_hash_trackers_t hash_trackers =
        db_display_dual_hash_trackers_create_from_runtime(
            DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU, &runtime_hash_cfg,
            DB_DISPLAY_HASH_KEY_STATE, DB_DISPLAY_HASH_KEY_FRAMEBUFFER);
    db_gl_framebuffer_hash_scratch_t hash_rgba8_scratch = {0};
    db_gl_framebuffer_hash_f16_scratch_t hash_rgba16f_scratch = {0};
    db_glfw_cpu_loop_ctx_t loop_ctx = {
        .api_name = db_dispatch_api_name(DB_API_CPU),
        .capability_mode = present_mode.capability_mode,
        .next_progress_log_due_ms = 0.0,
        .frame_step = {0},
        .state_hash_tracker = &hash_trackers.state,
        .framebuffer_hash_tracker = &hash_trackers.output,
        .framebuffer_hash_uses_rgba16f =
            (present_mode.selected_format == DB_GLFW_CPU_PRESENT_FORMAT_RGBA16F)
                ? 1
                : 0,
        .hash_rgba8_scratch = &hash_rgba8_scratch,
        .hash_rgba16f_scratch = &hash_rgba16f_scratch,
        .state_hash_enabled = hash_settings.state_hash_enabled,
        .output_hash_enabled = hash_settings.output_hash_enabled,
        .debug_clear_default_framebuffer =
            runtime_cfg.debug_clear_default_framebuffer,
        .selected_format = present_mode.selected_format,
        .present = &present,
        .work_unit_count = work_unit_count,
        .window = window,
    };
    loop_ctx.frame_step = db_display_frame_step_make(
        loop_ctx.api_name, DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU,
        present_mode.capability_mode, db_renderer_name_cpu(),
        loop_ctx.framebuffer_hash_tracker, loop_ctx.state_hash_tracker,
        &loop_ctx.next_progress_log_due_ms, loop_ctx.work_unit_count,
        loop_ctx.output_hash_enabled, loop_ctx.state_hash_enabled);
    db_glfw_loop_t loop = {
        .backend = DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU,
        .frame_fn = db_glfw_cpu_frame,
        .fps_cap = runtime_cfg.fps_cap,
        .frame_limit = runtime_cfg.frame_limit,
        .user_data = &loop_ctx,
        .window = window,
    };
    const db_display_frame_loop_run_result_t loop_result =
        db_glfw_run_loop(&loop);
    const uint64_t frames = loop_result.frames;

    const double bench_ms =
        (double)(db_now_ns_monotonic() - bench_start_ns) / DB_NS_PER_MS;
    db_benchmark_log_final(
        db_dispatch_api_name(DB_API_CPU), db_renderer_name_cpu(),
        DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU, frames, work_unit_count,
        bench_ms, present_mode.capability_mode);
    db_display_dual_hash_trackers_log_final(
        DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU, &hash_trackers);

    db_gl_hash_scratch_release(&hash_rgba8_scratch);
    db_gl_hash_f16_scratch_release(&hash_rgba16f_scratch);
    db_renderer_cpu_renderer_shutdown();
    db_gl_shadow_present_shutdown(&present.shared);
    if (present.debug_clear_buf != NULL) {
        free(present.debug_clear_buf);
        present.debug_clear_buf = NULL;
        present.debug_clear_buf_bytes = 0U;
        present.debug_clear_ready = 0;
    }
    db_glfw_destroy_window(window);
    return 0;
}
static db_display_frame_loop_result_t
db_glfw_opengl_frame(void *user_data, uint32_t frame_index, double elapsed_ms) {
    db_glfw_opengl_loop_ctx_t *ctx = (db_glfw_opengl_loop_ctx_t *)user_data;
    int framebuffer_width_px = 0;
    int framebuffer_height_px = 0;
    glfwGetFramebufferSize(ctx->window, &framebuffer_width_px,
                           &framebuffer_height_px);
    // Do not clear here. Some renderers rely on keeping the prior frame in the
    // default framebuffer (damage-only updates) and will clear/draw explicitly
    // as needed.
    db_display_gl_debug_clear_default_framebuffer_if_enabled(
        ctx->debug_clear_default_framebuffer);

    const db_display_gl_renderer_ops_t *renderer_ops = &ctx->renderer_ops;
    db_display_gl_render_frame(renderer_ops->renderer, frame_index,
                               framebuffer_width_px, framebuffer_height_px,
                               ctx->renderer_preserved_framebuffer_count, 0);

    db_display_gl_frame_step(
        &ctx->frame_step, frame_index, elapsed_ms, 1,
        renderer_ops->state_hash(), 1,
        db_glfw_hash_presented_default_framebuffer_or_fail(
            ctx->backend_name, ctx->window, ctx->framebuffer_hash_uses_rgba16f,
            ctx->hash_rgba8_scratch, ctx->hash_rgba16f_scratch));
    glfwSwapBuffers(ctx->window);
    return DB_DISPLAY_FRAME_LOOP_CONTINUE;
}

static int db_run_glfw_window_opengl(db_gl_renderer_t renderer,
                                     const db_cli_config_t *cfg) {
    db_cli_config_t effective_cfg = (cfg != NULL) ? *cfg : (db_cli_config_t){0};
    const db_glfw_window_visibility_t visibility =
        (effective_cfg.glfw_window_hidden != 0) ? DB_GLFW_WINDOW_HIDDEN
                                                : DB_GLFW_WINDOW_VISIBLE;
    const int true_offscreen_backend =
        (effective_cfg.display == DB_DISPLAY_OFFSCREEN) ? 1 : 0;
    const char *backend_name = (true_offscreen_backend != 0)
                                   ? DB_BACKEND_NAME_DISPLAY_OFFSCREEN
                                   : DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_GL;
    db_validate_runtime_environment(backend_name,
                                    DB_RUNTIME_OPT_ALLOW_REMOTE_DISPLAY);
    db_install_signal_handlers();
    const int swap_interval = (effective_cfg.vsync_enabled != 0) ? 1 : 0;
    int context_is_gles = 0;
    const db_display_gl_renderer_ops_t renderer_ops =
        db_display_gl_select_renderer_ops(renderer);
    const db_display_gl_context_policy_t context_policy =
        db_display_gl_context_policy_for_renderer(renderer);
    uint32_t renderer_preserved_framebuffer_count =
        db_display_gl_default_preserved_framebuffer_count(renderer);

#ifdef __linux__
    if ((true_offscreen_backend == 0) &&
        (renderer == DB_GL_RENDERER_GL1_5_GLES1_1)) {
        const db_glfw_default_fb_probe_result_t probe_result =
            db_glfw_probe_and_log_default_framebuffer_behavior(
                backend_name, renderer, &context_policy, swap_interval);
        const int probe_is_stable =
            db_glfw_default_framebuffer_probe_is_stable(&probe_result);
        renderer_preserved_framebuffer_count = (probe_is_stable != 0) ? 2U : 0U;
        if ((effective_cfg.backbuffer_draw_full == 0) &&
            (effective_cfg.backbuffer_draw_mode_explicit == 0) &&
            (probe_is_stable == 0)) {
            effective_cfg.backbuffer_draw_full = 1;
            renderer_preserved_framebuffer_count = 0U;
            db_runtime_option_set_backbuffer_draw_full(1);
            db_infof(backend_name,
                     "forcing full backbuffer draw: unstable default-fb reuse "
                     "observed on Linux GLFW");
        }
    }
#endif

    const db_display_runtime_hash_config_t runtime_hash_cfg =
        db_display_runtime_hash_config_from_cli(&effective_cfg, 0, 0);
    const db_display_runtime_config_t runtime_cfg = runtime_hash_cfg.runtime;
    const db_display_hash_settings_t hash_settings =
        runtime_hash_cfg.hash_settings;

    GLFWwindow *window = db_glfw_create_renderer_window(
        backend_name, &context_policy, swap_interval, visibility,
        &context_is_gles);

    (void)db_display_require_gl_runtime_for_renderer(
        (db_gl_proc_resolver_fn_t)glfwGetProcAddress, renderer, backend_name,
        (context_policy.allow_gles1_1_fallback != 0) ? context_is_gles : -1);

    renderer_ops.init();
    const char *capability_mode = renderer_ops.runtime_capability_mode();
    const uint32_t work_unit_count = renderer_ops.work_unit_count();
    const char *renderer_name = renderer_ops.renderer_name;
    const uint64_t bench_start_ns = db_now_ns_monotonic();
    db_display_dual_hash_trackers_t hash_trackers =
        db_display_dual_hash_trackers_create_from_runtime(
            backend_name, &runtime_hash_cfg, DB_DISPLAY_HASH_KEY_STATE,
            DB_DISPLAY_HASH_KEY_FRAMEBUFFER);
    db_gl_framebuffer_hash_scratch_t hash_rgba8_scratch = {0};
    db_gl_framebuffer_hash_f16_scratch_t hash_rgba16f_scratch = {0};
    db_glfw_opengl_loop_ctx_t loop_ctx = {
        .backend_name = backend_name,
        .capability_mode = capability_mode,
        .renderer_name = renderer_name,
        .frame_step = {0},
        .state_hash_tracker = &hash_trackers.state,
        .framebuffer_hash_tracker = &hash_trackers.output,
        .framebuffer_hash_uses_rgba16f = 0,
        .hash_rgba8_scratch = &hash_rgba8_scratch,
        .hash_rgba16f_scratch = &hash_rgba16f_scratch,
        .renderer_ops = renderer_ops,
        .next_progress_log_due_ms = 0.0,
        .state_hash_enabled = hash_settings.state_hash_enabled,
        .output_hash_enabled = hash_settings.output_hash_enabled,
        .debug_clear_default_framebuffer =
            runtime_cfg.debug_clear_default_framebuffer,
        .renderer_preserved_framebuffer_count =
            renderer_preserved_framebuffer_count,
        .work_unit_count = work_unit_count,
        .window = window,
    };
    loop_ctx.frame_step = db_display_frame_step_make(
        db_dispatch_api_name(DB_API_OPENGL), loop_ctx.backend_name,
        loop_ctx.capability_mode, loop_ctx.renderer_name,
        loop_ctx.framebuffer_hash_tracker, loop_ctx.state_hash_tracker,
        &loop_ctx.next_progress_log_due_ms, loop_ctx.work_unit_count,
        loop_ctx.output_hash_enabled, loop_ctx.state_hash_enabled);
    db_glfw_loop_t loop = {
        .backend = backend_name,
        .frame_fn = db_glfw_opengl_frame,
        .fps_cap = runtime_cfg.fps_cap,
        .frame_limit = runtime_cfg.frame_limit,
        .user_data = &loop_ctx,
        .window = window,
    };
    const db_display_frame_loop_run_result_t loop_result =
        db_glfw_run_loop(&loop);
    const uint64_t frames = loop_result.frames;

    const double bench_ms =
        (double)(db_now_ns_monotonic() - bench_start_ns) / DB_NS_PER_MS;
    db_display_log_draw_stats_with_fn(backend_name, renderer_ops.draw_stats);
    db_benchmark_log_final(db_dispatch_api_name(DB_API_OPENGL), renderer_name,
                           backend_name, frames, work_unit_count, bench_ms,
                           capability_mode);
    db_display_dual_hash_trackers_log_final(backend_name, &hash_trackers);

    renderer_ops.shutdown();
    db_glfw_destroy_window(window);
    db_gl_hash_scratch_release(&hash_rgba8_scratch);
    db_gl_hash_f16_scratch_release(&hash_rgba16f_scratch);
    return 0;
}

#ifdef DB_HAS_VULKAN_API
// NOLINTBEGIN(misc-include-cleaner)
static const char *const *
db_glfw_vk_required_instance_extensions(uint32_t *count,
                                        const void *user_data) {
    (void)user_data;
    return glfwGetRequiredInstanceExtensions(count);
}

static VkResult db_glfw_vk_create_surface(VkInstance instance,
                                          void *window_handle,
                                          VkSurfaceKHR *surface,
                                          const void *user_data) {
    (void)user_data;
    return glfwCreateWindowSurface(instance, (GLFWwindow *)window_handle, NULL,
                                   surface);
}

static void db_glfw_vk_get_framebuffer_size(void *window_handle, int *width,
                                            int *height,
                                            const void *user_data) {
    (void)user_data;
    glfwGetFramebufferSize((GLFWwindow *)window_handle, width, height);
}

static db_display_frame_loop_result_t
db_glfw_vulkan_frame(void *user_data, uint32_t frame_index, double elapsed_ms) {
    (void)frame_index;
    (void)elapsed_ms;
    const db_glfw_vulkan_loop_ctx_t *ctx =
        (const db_glfw_vulkan_loop_ctx_t *)user_data;
    const db_vk_frame_result_t frame_result =
        db_renderer_vulkan_1_2_multi_gpu_render_frame();
    if ((ctx->state_hash_enabled != 0) && (frame_result == DB_VK_FRAME_OK)) {
        const uint64_t state_hash =
            db_renderer_vulkan_1_2_multi_gpu_state_hash();
        db_display_hash_tracker_record(ctx->state_hash_tracker, state_hash);
    }
    if ((ctx->output_hash_enabled != 0) && (frame_result == DB_VK_FRAME_OK)) {
        const uint64_t output_hash =
            db_renderer_vulkan_1_2_multi_gpu_output_hash();
        db_display_hash_tracker_record(ctx->output_hash_tracker, output_hash);
    }
    if (frame_result == DB_VK_FRAME_STOP) {
        db_infof(ctx->backend_name, "renderer requested stop");
        return DB_DISPLAY_FRAME_LOOP_STOP;
    }
    if (frame_result == DB_VK_FRAME_RETRY) {
        return DB_DISPLAY_FRAME_LOOP_RETRY;
    }
    return DB_DISPLAY_FRAME_LOOP_CONTINUE;
}

static int db_run_glfw_window_vulkan(const db_cli_config_t *cfg) {
    const db_glfw_window_visibility_t visibility =
        ((cfg != NULL) && (cfg->glfw_window_hidden != 0))
            ? DB_GLFW_WINDOW_HIDDEN
            : DB_GLFW_WINDOW_VISIBLE;
    const int true_offscreen_backend =
        ((cfg != NULL) && (cfg->display == DB_DISPLAY_OFFSCREEN)) ? 1 : 0;
    const char *backend_name = (true_offscreen_backend != 0)
                                   ? DB_BACKEND_NAME_DISPLAY_OFFSCREEN
                                   : DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_VK;
    db_validate_runtime_environment(backend_name,
                                    DB_RUNTIME_OPT_ALLOW_REMOTE_DISPLAY);
    db_install_signal_handlers();
    const db_display_runtime_hash_config_t runtime_hash_cfg =
        db_display_runtime_hash_config_from_cli(cfg, 0, 0);
    const db_display_runtime_config_t runtime_cfg = runtime_hash_cfg.runtime;
    const db_display_hash_settings_t hash_settings =
        runtime_hash_cfg.hash_settings;

    GLFWwindow *window = db_glfw_create_no_api_window(
        backend_name, "Vulkan 1.2 opportunistic multi-GPU (device groups)",
        BENCH_WINDOW_WIDTH_PX, BENCH_WINDOW_HEIGHT_PX, visibility);
    uint32_t runtime_api_version = VK_API_VERSION_1_0;
    const VkResult version_result =
        vkEnumerateInstanceVersion(&runtime_api_version);
    if (version_result != VK_SUCCESS) {
        runtime_api_version = VK_API_VERSION_1_0;
    }
    db_display_log_vulkan_runtime_api(backend_name, runtime_api_version,
                                      "(selected by renderer)");

    const db_vk_wsi_config_t wsi_config = {
        .window_handle = window,
        .user_data = backend_name,
        .get_required_instance_extensions =
            db_glfw_vk_required_instance_extensions,
        .create_window_surface = db_glfw_vk_create_surface,
        .get_framebuffer_size = db_glfw_vk_get_framebuffer_size,
    };
    db_renderer_vulkan_1_2_multi_gpu_init(
        &wsi_config,
        (cfg != NULL) ? cfg->vsync_enabled : BENCH_DEFAULT_VSYNC_ENABLED);
    db_renderer_vulkan_1_2_multi_gpu_set_output_hash_enabled(
        hash_settings.output_hash_enabled);
    db_display_dual_hash_trackers_t hash_trackers =
        db_display_dual_hash_trackers_create_from_runtime(
            backend_name, &runtime_hash_cfg, DB_DISPLAY_HASH_KEY_STATE,
            DB_DISPLAY_HASH_KEY_FBO);
    db_glfw_vulkan_loop_ctx_t loop_ctx = {
        .backend_name = backend_name,
        .state_hash_tracker = &hash_trackers.state,
        .output_hash_tracker = &hash_trackers.output,
        .state_hash_enabled = hash_settings.state_hash_enabled,
        .output_hash_enabled = hash_settings.output_hash_enabled,
    };
    db_glfw_loop_t loop = {
        .backend = backend_name,
        .frame_fn = db_glfw_vulkan_frame,
        .fps_cap = runtime_cfg.fps_cap,
        .frame_limit = runtime_cfg.frame_limit,
        .user_data = &loop_ctx,
        .window = window,
    };
    const db_display_frame_loop_run_result_t loop_result =
        db_glfw_run_loop(&loop);
    db_renderer_vulkan_1_2_multi_gpu_set_present_metrics(
        loop_result.frame_ema_ms, loop_result.jitter_ema_ms,
        loop_result.frame_p50_ms, loop_result.frame_p95_ms,
        loop_result.frame_p99_ms, loop_result.retries);
    db_display_log_draw_stats_with_fn(
        backend_name, db_renderer_vulkan_1_2_multi_gpu_draw_stats);
    db_renderer_vulkan_1_2_multi_gpu_shutdown();
    db_display_dual_hash_trackers_log_final(backend_name, &hash_trackers);
    db_glfw_destroy_window(window);
    return 0;
}
// NOLINTEND(misc-include-cleaner)
#endif

int db_run_glfw_window(db_api_t api, db_gl_renderer_t renderer,
                       const db_cli_config_t *cfg) {
    db_dispatch_validate_backend_or_fail(DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_GL,
                                         DB_DISPLAY_GLFW_WINDOW, api, renderer);
    if (api == DB_API_CPU) {
        return db_run_glfw_window_cpu(cfg);
    }
#ifdef DB_HAS_VULKAN_API
    if (api == DB_API_VULKAN) {
        return db_run_glfw_window_vulkan(cfg);
    }
#endif
    return db_run_glfw_window_opengl(renderer, cfg);
}
