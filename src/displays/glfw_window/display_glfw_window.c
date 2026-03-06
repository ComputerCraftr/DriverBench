#ifdef DB_HAS_VULKAN_API
#define GLFW_INCLUDE_VULKAN
#endif
#include <GLFW/glfw3.h>

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "../../core/db_buffer_convert.h"
#include "../../core/db_core.h"
#include "../../core/db_numeric.h"
#include "../../driverbench_config.h"
#include "../../renderers/cpu_renderer/renderer_cpu_renderer.h"
#include "../../renderers/renderer_benchmark_common.h"
#include "../../renderers/renderer_gl_common.h"
#include "../../renderers/renderer_identity.h"
#ifdef DB_HAS_VULKAN_API
#include "../../renderers/vulkan_1_2_multi_gpu/renderer_vulkan_1_2_multi_gpu.h"
#endif
#include "../../config/benchmark_config.h"
#include "../display_cpu_hash_common.h"
#include "../display_cpu_upload_policy_common.h"
#include "../display_dispatch.h"
#include "../display_frame_loop_common.h"
#include "../display_gl_hash_readback_common.h"
#include "../display_gl_renderer_select_common.h"
#include "../display_gl_runtime_common.h"
#include "../display_hash_common.h"
#include "../display_runtime_config_common.h"
#include "../display_types.h"
#include "display_glfw_window_common.h"

#define BACKEND_NAME_CPU "display_glfw_window_cpu_renderer"
#define DB_CAP_MODE_CPU_GLFW_PBO "cpu_glfw_window_pbo"
#define DB_CAP_MODE_CPU_GLFW_PBO_HDR "cpu_glfw_window_pbo_hdr_rgba16f"
#define DB_CAP_MODE_CPU_GLFW_TEX_SUB_IMAGE "cpu_glfw_window_tex_sub_image"
#define DB_CAP_MODE_CPU_GLFW_TEX_SUB_IMAGE_HDR                                 \
    "cpu_glfw_window_tex_sub_image_hdr_rgba16f"
#define DB_CPU_DEBUG_CLEAR_CHUNK_ROWS 64U
#define DB_CPU_RGBA16F_CHANNELS_PER_PIXEL 4U
#define DB_CPU_RGBA16F_BYTES_PER_PIXEL                                         \
    (sizeof(uint16_t) * DB_CPU_RGBA16F_CHANNELS_PER_PIXEL)
#define DB_CPU_RGBA8_BYTES_PER_PIXEL 4U
#define BACKEND_NAME_GL "display_glfw_window_opengl"
#ifdef DB_HAS_VULKAN_API
#define BACKEND_NAME_VK "display_glfw_window_vulkan"
#endif

#ifdef DB_HAS_VULKAN_API
typedef struct {
    const char *backend_name;
    db_display_hash_tracker_t *hash_tracker;
    int state_hash_enabled;
} db_glfw_vulkan_loop_ctx_t;
#endif

typedef struct {
    unsigned int pbo;
    int has_pbo;
    int initialized;
    int force_full_upload;
    int use_hdr_float_bo;
    unsigned int texture;
    uint32_t texture_height;
    int use_npot;
    uint32_t texture_width;
    float texcoords[8];
    float vertices[8];
    int last_viewport_w;
    int last_viewport_h;
    // Scratch buffer for upload ranges (damage row -> byte range conversion).
    // Allocated once (or on rare resize), never allocated in the hot path.
    db_gl_upload_range_t *upload_ranges_buf;
    size_t upload_ranges_cap;
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

typedef struct {
    db_cpu_present_gl_state_t *state;
    const uint8_t *pixels_rgba8;
    const uint16_t *pixels_rgba16f;
    uint32_t pixel_width;
    int use_pbo;
} db_cpu_upload_apply_ctx_t;

typedef struct {
    const char *api_name;
    const char *capability_mode;
    double next_progress_log_due_ms;
    db_display_frame_step_t frame_step;
    db_display_hash_tracker_t *state_hash_tracker;
    db_display_hash_tracker_t *bo_hash_tracker;
    int state_hash_enabled;
    int output_hash_enabled;
    int debug_clear_default_framebuffer;
    db_cpu_present_gl_state_t *present;
    uint32_t work_unit_count;
    GLFWwindow *window;
} db_glfw_cpu_loop_ctx_t;

typedef struct {
    const char *backend_name;
    const char *capability_mode;
    const char *renderer_name;
    db_display_gl_renderer_ops_t renderer_ops;
    db_display_frame_step_t frame_step;
    db_display_hash_tracker_t *state_hash_tracker;
    db_display_hash_tracker_t *framebuffer_hash_tracker;
    db_gl_framebuffer_hash_scratch_t *hash_scratch;
    double next_progress_log_due_ms;
    int state_hash_enabled;
    int output_hash_enabled;
    int debug_clear_default_framebuffer;
    uint32_t work_unit_count;
    GLFWwindow *window;
} db_glfw_opengl_loop_ctx_t;

static void db_present_cpu_texture_resize(db_cpu_present_gl_state_t *state,
                                          uint32_t pixel_width,
                                          uint32_t pixel_height) {
    if ((state == NULL) || (pixel_width == 0U) || (pixel_height == 0U)) {
        return;
    }

    const uint32_t target_width =
        (state->use_npot != 0) ? pixel_width : db_u32_next_pow2(pixel_width);
    const uint32_t target_height =
        (state->use_npot != 0) ? pixel_height : db_u32_next_pow2(pixel_height);

    state->texture_width = target_width;
    state->texture_height = target_height;
    if (state->texture == 0U) {
        const int created =
            (state->use_hdr_float_bo != 0)
                ? db_gl_texture_create_rgba16f(
                      &state->texture,
                      db_checked_u32_to_i32(BACKEND_NAME_CPU, "cpu_tex_width",
                                            state->texture_width),
                      db_checked_u32_to_i32(BACKEND_NAME_CPU, "cpu_tex_height",
                                            state->texture_height),
                      NULL)
                : db_gl_texture_create_rgba8(
                      &state->texture,
                      db_checked_u32_to_i32(BACKEND_NAME_CPU, "cpu_tex_width",
                                            state->texture_width),
                      db_checked_u32_to_i32(BACKEND_NAME_CPU, "cpu_tex_height",
                                            state->texture_height),
                      NULL);
        if (created == 0) {
            db_failf(BACKEND_NAME_CPU, "failed to create CPU present texture");
        }
    } else {
        const int allocated =
            (state->use_hdr_float_bo != 0)
                ? db_gl_texture_allocate_rgba16f(
                      state->texture,
                      db_checked_u32_to_i32(BACKEND_NAME_CPU, "cpu_tex_width",
                                            state->texture_width),
                      db_checked_u32_to_i32(BACKEND_NAME_CPU, "cpu_tex_height",
                                            state->texture_height),
                      NULL)
                : db_gl_texture_allocate_rgba8(
                      state->texture,
                      db_checked_u32_to_i32(BACKEND_NAME_CPU, "cpu_tex_width",
                                            state->texture_width),
                      db_checked_u32_to_i32(BACKEND_NAME_CPU, "cpu_tex_height",
                                            state->texture_height),
                      NULL);
        if (allocated == 0) {
            db_failf(BACKEND_NAME_CPU, "failed to resize CPU present texture");
        }
    }
    db_display_cpu_upload_mark_force_full(&state->force_full_upload);
}

static void db_present_cpu_init_state(db_cpu_present_gl_state_t *state,
                                      int enable_pbo_probe) {
    if ((state == NULL) || (state->initialized != 0)) {
        return;
    }
    state->texture = 0U;
    db_display_cpu_upload_mark_force_full(&state->force_full_upload);
    if ((enable_pbo_probe != 0) &&
        (db_gl_context_has_pbo_upload_procs() != 0)) {
        state->pbo = db_gl_pbo_create_or_zero();
        if (state->pbo != 0U) {
            state->has_pbo = 1;
        }
    }

    db_gl_quad_init(state->vertices);

    db_gl_set_depth_test_enabled(0);
    db_gl_set_cull_face_enabled(0);
    db_gl_set_blend_enabled(0);
    db_gl_set_texture_2d_enabled(1);
    db_gl_set_client_state_vertex_array_enabled(1);
    db_gl_set_client_state_texcoord_array_enabled(1);
    db_gl_set_vertex_pointer_2f(0, state->vertices);
    db_gl_set_texcoord_pointer_2f(0, state->texcoords);
    state->initialized = 1;
}

static void db_present_cpu_upload_rows(db_cpu_present_gl_state_t *state,
                                       uint32_t pixel_width, uint32_t row_start,
                                       uint32_t row_count,
                                       const void *pixel_data,
                                       int allow_null_data) {
    if ((state == NULL) || (pixel_width == 0U) || (row_count == 0U) ||
        ((allow_null_data == 0) && (pixel_data == NULL))) {
        return;
    }
    if (state->use_hdr_float_bo != 0) {
        db_gl_texture_sub_image_2d_rgba16f(
            0,
            db_checked_u32_to_i32(BACKEND_NAME_CPU, "cpu_upload_y", row_start),
            db_checked_u32_to_i32(BACKEND_NAME_CPU, "cpu_upload_w",
                                  pixel_width),
            db_checked_u32_to_i32(BACKEND_NAME_CPU, "cpu_upload_h", row_count),
            pixel_data);
    } else {
        db_gl_texture_sub_image_2d_rgba(
            0,
            db_checked_u32_to_i32(BACKEND_NAME_CPU, "cpu_upload_y", row_start),
            db_checked_u32_to_i32(BACKEND_NAME_CPU, "cpu_upload_w",
                                  pixel_width),
            db_checked_u32_to_i32(BACKEND_NAME_CPU, "cpu_upload_h", row_count),
            pixel_data);
    }
}

static void db_present_cpu_shutdown_state(db_cpu_present_gl_state_t *state) {
    if (state == NULL) {
        return;
    }
    if (state->pbo != 0U) {
        db_gl_pbo_delete_if_valid(state->pbo);
        state->pbo = 0U;
    }
    if (state->texture != 0U) {
        db_gl_texture_delete_if_valid(&state->texture);
    }
    if (state->upload_ranges_buf != NULL) {
        db_display_cpu_upload_ranges_release((void **)&state->upload_ranges_buf,
                                             &state->upload_ranges_cap);
    }
    if (state->debug_clear_buf != NULL) {
        free(state->debug_clear_buf);
        state->debug_clear_buf = NULL;
        state->debug_clear_buf_bytes = 0U;
        state->debug_clear_ready = 0;
    }
}

static void db_apply_cpu_upload_span(const db_gl_upload_row_span_t *span,
                                     void *user_data) {
    db_cpu_upload_apply_ctx_t *ctx = (db_cpu_upload_apply_ctx_t *)user_data;
    if ((ctx == NULL) || (span == NULL)) {
        return;
    }
    if (ctx->use_pbo != 0) {
        const void *pbo_offset =
            db_gl_vbo_offset_ptr(span->range.dst_offset_bytes);
        db_present_cpu_upload_rows(ctx->state, ctx->pixel_width,
                                   span->rows.row_start, span->rows.row_count,
                                   pbo_offset, 1);
    } else {
        const void *pixels_ptr =
            (ctx->state->use_hdr_float_bo != 0)
                ? (const void *)(ctx->pixels_rgba16f +
                                 (span->range.src_offset_bytes /
                                  DB_CPU_RGBA16F_BYTES_PER_PIXEL))
                : (const void *)(ctx->pixels_rgba8 +
                                 span->range.src_offset_bytes);
        db_present_cpu_upload_rows(ctx->state, ctx->pixel_width,
                                   span->rows.row_start, span->rows.row_count,
                                   pixels_ptr, 0);
    }
}

static void db_present_cpu_upload_spans(
    db_cpu_present_gl_state_t *state, const uint8_t *pixels_rgba8,
    const uint16_t *pixels_rgba16f, uint32_t pixel_width, uint32_t pixel_height,
    const db_gl_upload_range_t *ranges, size_t span_count) {
    if ((state == NULL) || (pixel_width == 0U) || (pixel_height == 0U) ||
        (ranges == NULL) || (span_count == 0U)) {
        return;
    }
    if ((state->use_hdr_float_bo != 0) && (pixels_rgba16f == NULL)) {
        return;
    }
    if ((state->use_hdr_float_bo == 0) && (pixels_rgba8 == NULL)) {
        return;
    }

    const uint32_t pixel_bytes = (state->use_hdr_float_bo != 0)
                                     ? (uint32_t)DB_CPU_RGBA16F_BYTES_PER_PIXEL
                                     : DB_CPU_RGBA8_BYTES_PER_PIXEL;
    const size_t total_bytes = (size_t)db_checked_mul_u32(
        BACKEND_NAME_CPU, "cpu_upload_total_bytes",
        db_checked_mul_u32(BACKEND_NAME_CPU, "cpu_upload_row_bytes",
                           pixel_width, pixel_bytes),
        pixel_height);
    if (total_bytes > (size_t)PTRDIFF_MAX) {
        db_failf(BACKEND_NAME_CPU, "cpu_upload_total_bytes too large: %zu",
                 total_bytes);
    }
    const int use_pbo = (state->has_pbo != 0) && (state->pbo != 0U) &&
                        (db_gl_context_has_pbo_upload_procs() != 0);
    if (use_pbo != 0) {
        const void *source_base = (state->use_hdr_float_bo != 0)
                                      ? (const void *)pixels_rgba16f
                                      : (const void *)pixels_rgba8;
        db_gl_upload_ranges_target(source_base, total_bytes, ranges, span_count,
                                   DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER,
                                   state->pbo, 0, NULL, 0, 0);
    }
    db_cpu_upload_apply_ctx_t apply_ctx = {
        .state = state,
        .pixels_rgba8 = pixels_rgba8,
        .pixels_rgba16f = pixels_rgba16f,
        .pixel_width = pixel_width,
        .use_pbo = use_pbo,
    };
    (void)db_gl_for_each_upload_row_span(
        BACKEND_NAME_CPU,
        db_checked_mul_u32(BACKEND_NAME_CPU, "cpu_row_unit_width", pixel_width,
                           pixel_bytes / DB_CPU_RGBA8_BYTES_PER_PIXEL),
        ranges, span_count, db_apply_cpu_upload_span, &apply_ctx);
    if (use_pbo != 0) {
        db_gl_pbo_unbind_unpack();
    }
}

static void db_present_cpu_debug_clear_prepare(db_cpu_present_gl_state_t *state,
                                               uint32_t pixel_width) {
    if ((state == NULL) || (pixel_width == 0U)) {
        return;
    }

    const uint32_t row_bytes =
        db_checked_mul_u32(BACKEND_NAME_CPU, "cpu_debug_clear_row_bytes",
                           pixel_width, DB_CPU_RGBA8_BYTES_PER_PIXEL);

    // Fixed chunk size to keep uploads bounded; this buffer is reused.
    const uint32_t chunk_bytes_u32 =
        db_checked_mul_u32(BACKEND_NAME_CPU, "cpu_debug_clear_chunk_bytes",
                           row_bytes, DB_CPU_DEBUG_CLEAR_CHUNK_ROWS);
    const size_t chunk_bytes = (size_t)chunk_bytes_u32;

    // If already prepared for this pixel width, nothing to do.
    if ((state->debug_clear_ready != 0) &&
        (state->debug_clear_pixel_width == pixel_width) &&
        (state->debug_clear_row_bytes == row_bytes) &&
        (state->debug_clear_chunk_rows == DB_CPU_DEBUG_CLEAR_CHUNK_ROWS) &&
        (state->debug_clear_buf != NULL) &&
        (state->debug_clear_buf_bytes == chunk_bytes)) {
        return;
    }

    // Allocate or resize the reusable scratch buffer.
    if (state->debug_clear_buf == NULL) {
        state->debug_clear_buf = (uint8_t *)db_alloc_array_or_fail(
            BACKEND_NAME_CPU, "debug_clear_buf", 1, chunk_bytes);
    } else if (state->debug_clear_buf_bytes != chunk_bytes) {
        uint8_t *new_buf =
            (uint8_t *)realloc(state->debug_clear_buf, chunk_bytes);
        if (new_buf != NULL) {
            state->debug_clear_buf = new_buf;
        } else {
            // Allocation failure: disable debug clear buffer and force full
            // upload as a safe fallback.
            state->debug_clear_ready = 0;
            db_display_cpu_upload_mark_force_full(&state->force_full_upload);
            return;
        }
    }

    state->debug_clear_pixel_width = pixel_width;
    state->debug_clear_row_bytes = row_bytes;
    state->debug_clear_chunk_rows = DB_CPU_DEBUG_CLEAR_CHUNK_ROWS;
    state->debug_clear_buf_bytes = chunk_bytes;

    // Cache clear color bytes.
    state->debug_clear_rgba[0] = db_double01_to_u8_clamped(BENCH_CLEAR_COLOR_R);
    state->debug_clear_rgba[1] = db_double01_to_u8_clamped(BENCH_CLEAR_COLOR_G);
    state->debug_clear_rgba[2] = db_double01_to_u8_clamped(BENCH_CLEAR_COLOR_B);
    state->debug_clear_rgba[3] = db_double01_to_u8_clamped(BENCH_CLEAR_COLOR_A);

    // Fill the buffer once via shared conversion helper.
    db_fill_rgba8_byte_pattern(
        state->debug_clear_buf, chunk_bytes_u32 / DB_CPU_RGBA8_BYTES_PER_PIXEL,
        state->debug_clear_rgba[0], state->debug_clear_rgba[1],
        state->debug_clear_rgba[2], state->debug_clear_rgba[3]);

    state->debug_clear_ready = 1;
}

// Helper: clear the CPU present texture to the debug clear color (for debug
// mode).
static void db_present_cpu_clear_texture_debug(db_cpu_present_gl_state_t *state,
                                               uint32_t pixel_width,
                                               uint32_t pixel_height) {
    if ((state == NULL) || (state->texture == 0U) || (pixel_width == 0U) ||
        (pixel_height == 0U)) {
        return;
    }

    // Must be prepared by init/resize; never allocate in the hot path.
    if ((state->debug_clear_ready == 0) || (state->debug_clear_buf == NULL) ||
        (state->debug_clear_pixel_width != pixel_width) ||
        (state->debug_clear_row_bytes == 0U) ||
        (state->debug_clear_chunk_rows == 0U)) {
        db_display_cpu_upload_mark_force_full(&state->force_full_upload);
        return;
    }

    db_gl_texture_bind_2d(state->texture);

    const uint32_t chunk_rows = state->debug_clear_chunk_rows;
    uint32_t row = 0U;
    while (row < pixel_height) {
        const uint32_t rows_left = pixel_height - row;
        const uint32_t upload_rows =
            (rows_left < chunk_rows) ? rows_left : chunk_rows;
        db_gl_texture_sub_image_2d_rgba(
            0,
            db_checked_u32_to_i32(BACKEND_NAME_CPU, "cpu_debug_clear_y", row),
            db_checked_u32_to_i32(BACKEND_NAME_CPU, "cpu_debug_clear_w",
                                  pixel_width),
            db_checked_u32_to_i32(BACKEND_NAME_CPU, "cpu_debug_clear_h",
                                  upload_rows),
            state->debug_clear_buf);
        row += upload_rows;
    }
}

static void
db_present_cpu_prepare_resources(db_cpu_present_gl_state_t *state,
                                 uint32_t pixel_width, uint32_t pixel_height,
                                 int debug_clear_default_framebuffer) {
    if ((state == NULL) || (pixel_width == 0U) || (pixel_height == 0U)) {
        return;
    }

    const uint32_t target_width =
        (state->use_npot != 0) ? pixel_width : db_u32_next_pow2(pixel_width);
    const uint32_t target_height =
        (state->use_npot != 0) ? pixel_height : db_u32_next_pow2(pixel_height);
    if ((target_width != state->texture_width) ||
        (target_height != state->texture_height)) {
        db_present_cpu_texture_resize(state, pixel_width, pixel_height);
    }
    if (state->upload_ranges_cap < (size_t)pixel_height) {
        db_display_cpu_upload_ranges_ensure_capacity(
            BACKEND_NAME_CPU, "upload_ranges_buf",
            (void **)&state->upload_ranges_buf, &state->upload_ranges_cap,
            pixel_height, sizeof(db_gl_upload_range_t));
    }

    if ((debug_clear_default_framebuffer != 0) &&
        (state->use_hdr_float_bo == 0)) {
        db_present_cpu_debug_clear_prepare(state, pixel_width);
    }
}

static void db_present_cpu_framebuffer(GLFWwindow *window,
                                       db_cpu_present_gl_state_t *state,
                                       const db_dirty_row_range_t *ranges,
                                       size_t range_count,
                                       int debug_clear_default_framebuffer) {
    uint32_t pixel_width = 0U;
    uint32_t pixel_height = 0U;
    const int use_hdr_float_bo = db_renderer_cpu_renderer_is_hdr_float_bo();
    const uint32_t *pixels_rgba8 = NULL;
    const uint16_t *pixels_rgba16f = NULL;
    if (use_hdr_float_bo != 0) {
        pixels_rgba16f = db_renderer_cpu_renderer_pixels_rgba16f(&pixel_width,
                                                                 &pixel_height);
    } else {
        pixels_rgba8 =
            db_renderer_cpu_renderer_pixels_rgba8(&pixel_width, &pixel_height);
    }
    if (((use_hdr_float_bo != 0) && (pixels_rgba16f == NULL)) ||
        ((use_hdr_float_bo == 0) && (pixels_rgba8 == NULL)) ||
        (pixel_width == 0U) || (pixel_height == 0U)) {
        db_failf(BACKEND_NAME_CPU, "cpu renderer returned invalid framebuffer");
    }
    state->use_hdr_float_bo = use_hdr_float_bo;

    int framebuffer_width_px = 0;
    int framebuffer_height_px = 0;
    glfwGetFramebufferSize(window, &framebuffer_width_px,
                           &framebuffer_height_px);
    if (framebuffer_width_px <= 0 || framebuffer_height_px <= 0) {
        framebuffer_width_px = db_checked_u32_to_i32(
            BACKEND_NAME_CPU, "framebuffer_width_px", db_grid_cols_effective());
        framebuffer_height_px =
            db_checked_u32_to_i32(BACKEND_NAME_CPU, "framebuffer_height_px",
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

    db_present_cpu_prepare_resources(state, pixel_width, pixel_height,
                                     debug_clear);
    if (state->texture == 0U) {
        db_failf(BACKEND_NAME_CPU, "CPU present texture is not initialized");
    }

    if (debug_clear != 0) {
        db_present_cpu_clear_texture_debug(state, pixel_width, pixel_height);
    }

    // Direct row-based uploading, no pattern planner.
    const uint32_t pixel_bytes = (state->use_hdr_float_bo != 0)
                                     ? (uint32_t)DB_CPU_RGBA16F_BYTES_PER_PIXEL
                                     : DB_CPU_RGBA8_BYTES_PER_PIXEL;
    const uint32_t row_bytes = db_checked_mul_u32(
        BACKEND_NAME_CPU, "cpu_upload_row_bytes", pixel_width, pixel_bytes);
    if (db_display_cpu_upload_should_force_full(
            state->force_full_upload, state->upload_ranges_buf,
            state->upload_ranges_cap, pixel_height) != 0) {
        const size_t full_bytes = (size_t)db_checked_mul_u32(
            BACKEND_NAME_CPU, "cpu_upload_full_bytes", row_bytes, pixel_height);
        const db_gl_upload_range_t full = db_gl_upload_full_range(full_bytes);
        db_present_cpu_upload_spans(state, (const uint8_t *)pixels_rgba8,
                                    pixels_rgba16f, pixel_width, pixel_height,
                                    &full, 1U);
        state->force_full_upload = 0;
    } else {
        db_gl_upload_range_t *upload_ranges = state->upload_ranges_buf;
        const size_t upload_span_count = db_gl_collect_row_upload_ranges(
            pixel_width, pixel_height, pixel_bytes, ranges, range_count, NULL,
            upload_ranges, state->upload_ranges_cap);
        if ((upload_span_count > 0U) && (upload_ranges != NULL)) {
            db_present_cpu_upload_spans(
                state, (const uint8_t *)pixels_rgba8, pixels_rgba16f,
                pixel_width, pixel_height, upload_ranges, upload_span_count);
        }
    }

    const float tex_u = (state->texture_width == 0U)
                            ? 1.0F
                            : db_double_to_f32((double)pixel_width /
                                               (double)state->texture_width);
    const float tex_v = (state->texture_height == 0U)
                            ? 1.0F
                            : db_double_to_f32((double)pixel_height /
                                               (double)state->texture_height);
    state->texcoords[DB_GL_QUAD_V0_X] = 0.0F;
    state->texcoords[DB_GL_QUAD_V0_Y] = tex_v;
    state->texcoords[DB_GL_QUAD_V1_X] = tex_u;
    state->texcoords[DB_GL_QUAD_V1_Y] = tex_v;
    state->texcoords[DB_GL_QUAD_V2_X] = 0.0F;
    state->texcoords[DB_GL_QUAD_V2_Y] = 0.0F;
    state->texcoords[DB_GL_QUAD_V3_X] = tex_u;
    state->texcoords[DB_GL_QUAD_V3_Y] = 0.0F;
    db_gl_draw_arrays_triangle_strip(0, 4);
}

static uint64_t db_glfw_cpu_renderer_bo_hash_or_fail(void) {
    return db_display_cpu_renderer_bo_hash_or_fail(BACKEND_NAME_CPU);
}

static void
db_glfw_cpu_present_damage_cb(const db_dirty_row_range_t *damage_ranges,
                              size_t damage_count, void *user_data) {
    db_glfw_cpu_loop_ctx_t *ctx = (db_glfw_cpu_loop_ctx_t *)user_data;
    if (ctx == NULL) {
        return;
    }
    db_present_cpu_framebuffer(ctx->window, ctx->present, damage_ranges,
                               damage_count,
                               ctx->debug_clear_default_framebuffer);
}

static db_display_frame_loop_result_t
db_glfw_cpu_frame(void *user_data, uint32_t frame_index, double elapsed_ms) {
    db_glfw_cpu_loop_ctx_t *ctx = (db_glfw_cpu_loop_ctx_t *)user_data;
    db_display_cpu_render_present_and_hash(
        &ctx->frame_step, frame_index, elapsed_ms,
        db_glfw_cpu_present_damage_cb, ctx,
        db_glfw_cpu_renderer_bo_hash_or_fail);

    glfwSwapBuffers(ctx->window);
    return DB_DISPLAY_FRAME_LOOP_CONTINUE;
}

static int db_run_glfw_window_cpu(const db_cli_config_t *cfg) {
    db_validate_runtime_environment(BACKEND_NAME_CPU,
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
        BACKEND_NAME_CPU, "CPU Renderer GLFW DriverBench",
        BENCH_WINDOW_WIDTH_PX, BENCH_WINDOW_HEIGHT_PX, gl_legacy_context_major,
        gl_legacy_context_minor, swap_interval, &is_gles,
        (cfg != NULL) ? cfg->offscreen_enabled : 0);
    const char *runtime_version = NULL;
    const int runtime_is_gles = db_display_prepare_and_validate_gl_runtime(
        (db_gl_proc_resolver_fn_t)glfwGetProcAddress,
        DB_GL_RENDERER_GL1_5_GLES1_1, BACKEND_NAME_CPU,
        DB_DISPLAY_GL_RUNTIME_LOG_ENABLED, is_gles, &runtime_version, NULL);
    const char *runtime_exts = db_gl_get_extensions_string();
    const int has_npot =
        (runtime_is_gles == 0) ||
        db_gl_version_text_at_least(runtime_version, 2, 0) ||
        db_has_gl_extension_token(runtime_exts, "GL_OES_texture_npot");
    const int has_pbo =
        db_gl_extensions_advertise_pbo(runtime_version, runtime_exts);
    const int has_texture_float_advertised =
        db_gl_extensions_advertise_texture_float(runtime_version, runtime_exts);
    const int has_texture_float =
        (has_texture_float_advertised != 0) &&
                (db_gl_context_probe_texture_float_support() != 0)
            ? 1
            : 0;
    db_renderer_cpu_renderer_init();
    db_cpu_present_gl_state_t present = {
        .has_pbo = 0,
        .initialized = 0,
        .use_hdr_float_bo = 0,
        .pbo = 0U,
        .texture = 0U,
        .texture_height = 0U,
        .texture_width = 0U,
        .use_npot = has_npot,
    };
    const int cpu_hdr_requested = db_renderer_cpu_renderer_is_hdr_float_bo();
    if ((cpu_hdr_requested != 0) && (has_texture_float == 0)) {
        db_failf(BACKEND_NAME_CPU,
                 "cpu_hdr requested but runtime has no float texture support");
    }
    present.use_hdr_float_bo = cpu_hdr_requested;
    db_present_cpu_init_state(&present, has_pbo);

    // Prepare upload range scratch buffer up-front to avoid per-frame
    // allocations.
    uint32_t init_pixel_width = 0U;
    uint32_t init_pixel_height = 0U;
    if (present.use_hdr_float_bo != 0) {
        (void)db_renderer_cpu_renderer_pixels_rgba16f(&init_pixel_width,
                                                      &init_pixel_height);
    } else {
        (void)db_renderer_cpu_renderer_pixels_rgba8(&init_pixel_width,
                                                    &init_pixel_height);
    }
    db_present_cpu_prepare_resources(
        &present, init_pixel_width, init_pixel_height,
        runtime_cfg.debug_clear_default_framebuffer);
    const int use_pbo_mode = ((present.has_pbo != 0) && (present.pbo != 0U) &&
                              (db_gl_context_has_pbo_upload_procs() != 0))
                                 ? 1
                                 : 0;
    const char *capability_mode = NULL;
    if (use_pbo_mode != 0) {
        capability_mode = (present.use_hdr_float_bo != 0)
                              ? DB_CAP_MODE_CPU_GLFW_PBO_HDR
                              : DB_CAP_MODE_CPU_GLFW_PBO;
    } else {
        capability_mode = (present.use_hdr_float_bo != 0)
                              ? DB_CAP_MODE_CPU_GLFW_TEX_SUB_IMAGE_HDR
                              : DB_CAP_MODE_CPU_GLFW_TEX_SUB_IMAGE;
    }

    const uint32_t work_unit_count = db_renderer_cpu_renderer_work_unit_count();
    const uint64_t bench_start_ns = db_now_ns_monotonic();
    db_display_dual_hash_trackers_t hash_trackers =
        db_display_dual_hash_trackers_create_from_runtime(
            BACKEND_NAME_CPU, &runtime_hash_cfg, "state_hash", "bo_hash");
    db_glfw_cpu_loop_ctx_t loop_ctx = {
        .api_name = db_dispatch_api_name(DB_API_CPU),
        .capability_mode = capability_mode,
        .next_progress_log_due_ms = 0.0,
        .frame_step = {0},
        .state_hash_tracker = &hash_trackers.state,
        .bo_hash_tracker = &hash_trackers.output,
        .state_hash_enabled = hash_settings.state_hash_enabled,
        .output_hash_enabled = hash_settings.output_hash_enabled,
        .debug_clear_default_framebuffer =
            runtime_cfg.debug_clear_default_framebuffer,
        .present = &present,
        .work_unit_count = work_unit_count,
        .window = window,
    };
    loop_ctx.frame_step = db_display_frame_step_make(
        loop_ctx.api_name, BACKEND_NAME_CPU, loop_ctx.capability_mode,
        db_renderer_name_cpu(), loop_ctx.bo_hash_tracker,
        loop_ctx.state_hash_tracker, &loop_ctx.next_progress_log_due_ms,
        loop_ctx.work_unit_count, loop_ctx.output_hash_enabled,
        loop_ctx.state_hash_enabled);
    const db_glfw_loop_t loop = {
        .backend = BACKEND_NAME_CPU,
        .frame_fn = db_glfw_cpu_frame,
        .fps_cap = runtime_cfg.fps_cap,
        .frame_limit = runtime_cfg.frame_limit,
        .user_data = &loop_ctx,
        .window = window,
    };
    const uint64_t frames = db_glfw_run_loop(&loop);

    const double bench_ms =
        (double)(db_now_ns_monotonic() - bench_start_ns) / DB_NS_PER_MS;
    db_benchmark_log_final(db_dispatch_api_name(DB_API_CPU),
                           db_renderer_name_cpu(), BACKEND_NAME_CPU, frames,
                           work_unit_count, bench_ms, capability_mode);
    db_display_dual_hash_trackers_log_final(BACKEND_NAME_CPU, &hash_trackers);

    db_renderer_cpu_renderer_shutdown();
    db_present_cpu_shutdown_state(&present);
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
    renderer_ops->render_frame_glfw(frame_index, framebuffer_width_px,
                                    framebuffer_height_px);

    const db_display_gl_hash_rgba8_cb_ctx_t framebuffer_hash_ctx = {
        .backend_name = ctx->backend_name,
        .framebuffer_width_px = framebuffer_width_px,
        .framebuffer_height_px = framebuffer_height_px,
        .scratch = ctx->hash_scratch,
    };
    db_display_gl_frame_step_with_hash_fns(
        &ctx->frame_step, frame_index, elapsed_ms,
        db_display_gl_renderer_ops_state_hash_cb, (void *)renderer_ops,
        db_display_gl_hash_rgba8_framebuffer_cb, (void *)&framebuffer_hash_ctx);
    glfwSwapBuffers(ctx->window);
    return DB_DISPLAY_FRAME_LOOP_CONTINUE;
}

static int db_run_glfw_window_opengl(db_gl_renderer_t renderer,
                                     const db_cli_config_t *cfg) {
    const char *backend_name = BACKEND_NAME_GL;
    db_validate_runtime_environment(backend_name,
                                    DB_RUNTIME_OPT_ALLOW_REMOTE_DISPLAY);
    db_install_signal_handlers();

    const db_display_runtime_hash_config_t runtime_hash_cfg =
        db_display_runtime_hash_config_from_cli(cfg, 0, 0);
    const db_display_runtime_config_t runtime_cfg = runtime_hash_cfg.runtime;
    const int swap_interval =
        ((cfg != NULL) && (cfg->vsync_enabled != 0)) ? 1 : 0;
    const db_display_hash_settings_t hash_settings =
        runtime_hash_cfg.hash_settings;
    int context_is_gles = 0;
    const char *runtime_version = NULL;
    const db_display_gl_renderer_ops_t renderer_ops =
        db_display_gl_select_renderer_ops(renderer);
    const db_display_gl_context_policy_t context_policy =
        db_display_gl_context_policy_for_renderer(renderer);

    GLFWwindow *window = NULL;
    if (context_policy.allow_gles1_1_fallback != 0) {
        window = db_glfw_create_gl1_5_or_gles1_1_window(
            backend_name, "OpenGL 1.5/GLES1.1 GLFW DriverBench",
            BENCH_WINDOW_WIDTH_PX, BENCH_WINDOW_HEIGHT_PX,
            context_policy.requested_gl_major,
            context_policy.requested_gl_minor, swap_interval, &context_is_gles,
            (cfg != NULL) ? cfg->offscreen_enabled : 0);
    } else {
        window = db_glfw_create_opengl_window(
            backend_name, "OpenGL 3.3 Shader GLFW DriverBench",
            BENCH_WINDOW_WIDTH_PX, BENCH_WINDOW_HEIGHT_PX,
            context_policy.requested_gl_major,
            context_policy.requested_gl_minor, 1, swap_interval,
            (cfg != NULL) ? cfg->offscreen_enabled : 0);
    }

    (void)db_display_prepare_and_validate_gl_runtime(
        (db_gl_proc_resolver_fn_t)glfwGetProcAddress, renderer, backend_name,
        DB_DISPLAY_GL_RUNTIME_LOG_ENABLED,
        (context_policy.allow_gles1_1_fallback != 0) ? context_is_gles : -1,
        &runtime_version, NULL);

    renderer_ops.init();
    const char *capability_mode = renderer_ops.runtime_capability_mode();
    const uint32_t work_unit_count = renderer_ops.work_unit_count();
    const char *renderer_name = renderer_ops.renderer_name;
    const uint64_t bench_start_ns = db_now_ns_monotonic();
    db_display_dual_hash_trackers_t hash_trackers =
        db_display_dual_hash_trackers_create_from_runtime(
            backend_name, &runtime_hash_cfg, "state_hash", "framebuffer_hash");
    db_gl_framebuffer_hash_scratch_t hash_scratch = {0};
    db_glfw_opengl_loop_ctx_t loop_ctx = {
        .backend_name = backend_name,
        .capability_mode = capability_mode,
        .renderer_name = renderer_name,
        .frame_step = {0},
        .state_hash_tracker = &hash_trackers.state,
        .framebuffer_hash_tracker = &hash_trackers.output,
        .hash_scratch = &hash_scratch,
        .renderer_ops = renderer_ops,
        .next_progress_log_due_ms = 0.0,
        .state_hash_enabled = hash_settings.state_hash_enabled,
        .output_hash_enabled = hash_settings.output_hash_enabled,
        .debug_clear_default_framebuffer =
            runtime_cfg.debug_clear_default_framebuffer,
        .work_unit_count = work_unit_count,
        .window = window,
    };
    loop_ctx.frame_step = db_display_frame_step_make(
        db_dispatch_api_name(DB_API_OPENGL), loop_ctx.backend_name,
        loop_ctx.capability_mode, loop_ctx.renderer_name,
        loop_ctx.framebuffer_hash_tracker, loop_ctx.state_hash_tracker,
        &loop_ctx.next_progress_log_due_ms, loop_ctx.work_unit_count,
        loop_ctx.output_hash_enabled, loop_ctx.state_hash_enabled);
    const db_glfw_loop_t loop = {
        .backend = backend_name,
        .frame_fn = db_glfw_opengl_frame,
        .fps_cap = runtime_cfg.fps_cap,
        .frame_limit = runtime_cfg.frame_limit,
        .user_data = &loop_ctx,
        .window = window,
    };
    const uint64_t frames = db_glfw_run_loop(&loop);

    const double bench_ms =
        (double)(db_now_ns_monotonic() - bench_start_ns) / DB_NS_PER_MS;
    db_display_log_draw_stats_with_fn(backend_name, renderer_ops.draw_stats);
    db_benchmark_log_final(db_dispatch_api_name(DB_API_OPENGL), renderer_name,
                           backend_name, frames, work_unit_count, bench_ms,
                           capability_mode);
    db_display_dual_hash_trackers_log_final(backend_name, &hash_trackers);

    renderer_ops.shutdown();
    db_glfw_destroy_window(window);
    db_gl_hash_scratch_release(&hash_scratch);
    return 0;
}

#ifdef DB_HAS_VULKAN_API
// NOLINTBEGIN(misc-include-cleaner)
static const char *const *
db_glfw_vk_required_instance_extensions(uint32_t *count, void *user_data) {
    (void)user_data;
    return glfwGetRequiredInstanceExtensions(count);
}

static VkResult db_glfw_vk_create_surface(VkInstance instance,
                                          void *window_handle,
                                          VkSurfaceKHR *surface,
                                          void *user_data) {
    (void)user_data;
    return glfwCreateWindowSurface(instance, (GLFWwindow *)window_handle, NULL,
                                   surface);
}

static void db_glfw_vk_get_framebuffer_size(void *window_handle, int *width,
                                            int *height, void *user_data) {
    (void)user_data;
    glfwGetFramebufferSize((GLFWwindow *)window_handle, width, height);
}

static db_display_frame_loop_result_t
db_glfw_vulkan_frame(void *user_data, uint32_t frame_index, double elapsed_ms) {
    (void)elapsed_ms;
    const db_glfw_vulkan_loop_ctx_t *ctx =
        (const db_glfw_vulkan_loop_ctx_t *)user_data;
    const db_vk_frame_result_t frame_result =
        db_renderer_vulkan_1_2_multi_gpu_render_frame();
    if ((ctx->state_hash_enabled != 0) && (frame_result == DB_VK_FRAME_OK)) {
        const uint64_t state_hash =
            db_renderer_vulkan_1_2_multi_gpu_state_hash();
        db_display_hash_tracker_record(ctx->hash_tracker, state_hash);
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
    db_validate_runtime_environment(BACKEND_NAME_VK,
                                    DB_RUNTIME_OPT_ALLOW_REMOTE_DISPLAY);
    db_install_signal_handlers();
    const db_display_runtime_hash_config_t runtime_hash_cfg =
        db_display_runtime_hash_config_from_cli(cfg, 0, 0);
    const db_display_runtime_config_t runtime_cfg = runtime_hash_cfg.runtime;
    const db_display_hash_settings_t hash_settings =
        runtime_hash_cfg.hash_settings;
    (void)hash_settings.output_hash_enabled;

    GLFWwindow *window = db_glfw_create_no_api_window(
        BACKEND_NAME_VK, "Vulkan 1.2 opportunistic multi-GPU (device groups)",
        BENCH_WINDOW_WIDTH_PX, BENCH_WINDOW_HEIGHT_PX,
        (cfg != NULL) ? cfg->offscreen_enabled : 0);
    uint32_t runtime_api_version = VK_API_VERSION_1_0;
    const VkResult version_result =
        vkEnumerateInstanceVersion(&runtime_api_version);
    if (version_result != VK_SUCCESS) {
        runtime_api_version = VK_API_VERSION_1_0;
    }
    db_display_log_vulkan_runtime_api(BACKEND_NAME_VK, runtime_api_version,
                                      "(selected by renderer)");

    const db_vk_wsi_config_t wsi_config = {
        .window_handle = window,
        .user_data = (void *)BACKEND_NAME_VK,
        .get_required_instance_extensions =
            db_glfw_vk_required_instance_extensions,
        .create_window_surface = db_glfw_vk_create_surface,
        .get_framebuffer_size = db_glfw_vk_get_framebuffer_size,
    };
    db_renderer_vulkan_1_2_multi_gpu_init(
        &wsi_config,
        (cfg != NULL) ? cfg->vsync_enabled : BENCH_DEFAULT_VSYNC_ENABLED);
    db_display_hash_tracker_t hash_tracker = db_display_hash_tracker_create(
        BACKEND_NAME_VK, hash_settings.state_hash_enabled, "state_hash",
        runtime_cfg.hash_report);
    const db_glfw_vulkan_loop_ctx_t loop_ctx = {
        .backend_name = BACKEND_NAME_VK,
        .hash_tracker = &hash_tracker,
        .state_hash_enabled = hash_settings.state_hash_enabled,
    };
    const db_glfw_loop_t loop = {
        .backend = BACKEND_NAME_VK,
        .frame_fn = db_glfw_vulkan_frame,
        .fps_cap = runtime_cfg.fps_cap,
        .frame_limit = runtime_cfg.frame_limit,
        .user_data = (void *)&loop_ctx,
        .window = window,
    };
    (void)db_glfw_run_loop(&loop);
    db_display_log_draw_stats_with_fn(
        BACKEND_NAME_VK, db_renderer_vulkan_1_2_multi_gpu_draw_stats);
    db_renderer_vulkan_1_2_multi_gpu_shutdown();
    db_display_hash_tracker_log_final(BACKEND_NAME_VK, &hash_tracker);
    db_glfw_destroy_window(window);
    return 0;
}
// NOLINTEND(misc-include-cleaner)
#endif

int db_run_glfw_window(db_api_t api, db_gl_renderer_t renderer,
                       const db_cli_config_t *cfg) {
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
