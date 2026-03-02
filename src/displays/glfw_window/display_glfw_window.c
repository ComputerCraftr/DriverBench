#ifdef DB_HAS_VULKAN_API
#define GLFW_INCLUDE_VULKAN
#endif
#include <GLFW/glfw3.h>

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "../../core/db_core.h"
#include "../../core/db_hash.h"
#include "../../driverbench_config.h"
#include "../../renderers/cpu_renderer/renderer_cpu_renderer.h"
#include "../../renderers/opengl_gl1_5_gles1_1/renderer_opengl_gl1_5_gles1_1.h"
#include "../../renderers/opengl_gl3_3/renderer_opengl_gl3_3.h"
#include "../../renderers/renderer_benchmark_common.h"
#include "../../renderers/renderer_gl_common.h"
#include "../../renderers/renderer_identity.h"
#ifdef DB_HAS_VULKAN_API
#include "../../renderers/vulkan_1_2_multi_gpu/renderer_vulkan_1_2_multi_gpu.h"
#endif
#include "../../config/benchmark_config.h"
#include "../display_dispatch.h"
#include "../display_gl_hash_readback_common.h"
#include "../display_gl_runtime_common.h"
#include "../display_hash_common.h"
#include "../display_types.h"
#include "display_glfw_window_common.h"

#define BACKEND_NAME_CPU "display_glfw_window_cpu_renderer"
#define DB_CAP_MODE_CPU_GLFW_PBO "cpu_glfw_window_pbo"
#define DB_CAP_MODE_CPU_GLFW_PBO_HDR "cpu_glfw_window_pbo_hdr_rgba32f"
#define DB_CAP_MODE_CPU_GLFW_TEX_SUB_IMAGE "cpu_glfw_window_tex_sub_image"
#define DB_CAP_MODE_CPU_GLFW_TEX_SUB_IMAGE_HDR                                 \
    "cpu_glfw_window_tex_sub_image_hdr_rgba32f"
#define DB_CPU_DEBUG_CLEAR_CHUNK_ROWS 64U
#define DB_CPU_RGBA32F_CHANNELS_PER_PIXEL 4U
#define DB_CPU_RGBA32F_BYTES_PER_PIXEL                                         \
    (sizeof(float) * DB_CPU_RGBA32F_CHANNELS_PER_PIXEL)
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

typedef enum {
    DB_GLFW_LOOP_CONTINUE = 0,
    DB_GLFW_LOOP_STOP = 1,
    DB_GLFW_LOOP_RETRY = 2,
} db_glfw_loop_result_t;

typedef db_glfw_loop_result_t (*db_glfw_frame_fn_t)(void *user_data,
                                                    uint32_t frame_index);

typedef struct {
    const char *backend;
    db_glfw_frame_fn_t frame_fn;
    double fps_cap;
    uint32_t frame_limit;
    void *user_data;
    GLFWwindow *window;
} db_glfw_loop_t;

static uint64_t db_glfw_run_loop(const db_glfw_loop_t *loop) {
    uint64_t frames = 0U;
    while (!glfwWindowShouldClose(loop->window) && !db_should_stop()) {
        if ((loop->frame_limit > 0U) && (frames >= loop->frame_limit)) {
            break;
        }
        const double frame_start_s = db_glfw_time_seconds();
        db_glfw_poll_events();
        const db_glfw_loop_result_t frame_result =
            loop->frame_fn(loop->user_data, frames);
        if (frame_result == DB_GLFW_LOOP_STOP) {
            break;
        }
        db_glfw_sleep_to_fps_cap(loop->backend, frame_start_s, loop->fps_cap);
        if (frame_result != DB_GLFW_LOOP_RETRY) {
            frames++;
        }
    }
    return frames;
}

static db_display_hash_settings_t
db_glfw_hash_settings_for_backend(const db_cli_config_t *cfg) {
    return db_display_resolve_hash_settings(
        0, 0, (cfg != NULL) ? cfg->hash_mode : "none");
}

typedef struct {
    unsigned int pbo;
    int has_pbo;
    int initialized;
    int needs_full_frame_upload;
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

static void
db_present_cpu_upload_ranges_prepare(db_cpu_present_gl_state_t *state,
                                     uint32_t pixel_height) {
    if ((state == NULL) || (pixel_height == 0U)) {
        return;
    }

    // Worst-case: every row is a separate dirty range.
    const size_t needed = (size_t)pixel_height;
    if ((state->upload_ranges_buf != NULL) &&
        (state->upload_ranges_cap >= needed)) {
        return;
    }

    const size_t bytes = needed * sizeof(db_gl_upload_range_t);
    if (state->upload_ranges_buf == NULL) {
        state->upload_ranges_buf =
            (db_gl_upload_range_t *)db_alloc_array_or_fail(
                BACKEND_NAME_CPU, "upload_ranges_buf", needed,
                sizeof(db_gl_upload_range_t));
    } else {
        db_gl_upload_range_t *new_buf =
            (db_gl_upload_range_t *)realloc(state->upload_ranges_buf, bytes);
        if (new_buf == NULL) {
            db_failf(BACKEND_NAME_CPU,
                     "cpu upload ranges realloc failed (bytes=%zu)", bytes);
        }
        state->upload_ranges_buf = new_buf;
    }

    state->upload_ranges_cap = needed;
}

typedef struct {
    db_cpu_present_gl_state_t *state;
    const uint8_t *pixels_rgba8;
    const float *pixels_rgba32f;
    uint32_t pixel_width;
    int use_pbo;
} db_cpu_upload_apply_ctx_t;

typedef struct {
    double bench_start;
    const char *capability_mode;
    double next_progress_log_due_ms;
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
    db_display_hash_tracker_t *state_hash_tracker;
    db_display_hash_tracker_t *framebuffer_hash_tracker;
    db_gl_framebuffer_hash_scratch_t *hash_scratch;
    double bench_start;
    double next_progress_log_due_ms;
    db_gl_renderer_t renderer;
    int state_hash_enabled;
    int output_hash_enabled;
    int debug_clear_default_framebuffer;
    uint32_t work_unit_count;
    GLFWwindow *window;
} db_glfw_opengl_loop_ctx_t;

static db_gl_generic_proc_t db_glfw_resolve_proc(const char *name) {
    return (db_gl_generic_proc_t)glfwGetProcAddress(name);
}

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
                ? db_gl_texture_create_rgba32f(
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
                ? db_gl_texture_allocate_rgba32f(
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
    state->needs_full_frame_upload = 1;
}

static void db_present_cpu_try_init_pbo(db_cpu_present_gl_state_t *state,
                                        int enable_pbo_probe) {
    if ((state == NULL) || (enable_pbo_probe == 0)) {
        return;
    }
    if (db_gl_context_supports_pbo_upload() == 0) {
        return;
    }
    state->pbo = db_gl_pbo_create_or_zero();
    if (state->pbo != 0U) {
        state->has_pbo = 1;
    }
}

static void db_present_cpu_init_state(db_cpu_present_gl_state_t *state,
                                      int enable_pbo_probe) {
    if ((state == NULL) || (state->initialized != 0)) {
        return;
    }
    state->texture = 0U;
    state->needs_full_frame_upload = 1;
    db_present_cpu_try_init_pbo(state, enable_pbo_probe);

    db_gl_quad_init(state->vertices);

    db_gl_set_depth_test_enabled(0);
    db_gl_set_cull_face_enabled(0);
    db_gl_set_blend_enabled(0);
    db_gl_set_scissor_enabled(0);
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
        db_gl_texture_sub_image_2d_rgba32f(
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
                ? (const void *)(ctx->pixels_rgba32f +
                                 (span->range.src_offset_bytes /
                                  DB_CPU_RGBA32F_BYTES_PER_PIXEL))
                : (const void *)(ctx->pixels_rgba8 +
                                 span->range.src_offset_bytes);
        db_present_cpu_upload_rows(ctx->state, ctx->pixel_width,
                                   span->rows.row_start, span->rows.row_count,
                                   pixels_ptr, 0);
    }
}

static void db_present_cpu_upload_spans(
    db_cpu_present_gl_state_t *state, const uint8_t *pixels_rgba8,
    const float *pixels_rgba32f, uint32_t pixel_width, uint32_t pixel_height,
    const db_gl_upload_range_t *ranges, size_t span_count) {
    if ((state == NULL) || (pixel_width == 0U) || (pixel_height == 0U) ||
        (ranges == NULL) || (span_count == 0U)) {
        return;
    }
    if ((state->use_hdr_float_bo != 0) && (pixels_rgba32f == NULL)) {
        return;
    }
    if ((state->use_hdr_float_bo == 0) && (pixels_rgba8 == NULL)) {
        return;
    }

    const uint32_t pixel_bytes = (state->use_hdr_float_bo != 0)
                                     ? (uint32_t)DB_CPU_RGBA32F_BYTES_PER_PIXEL
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
                        (db_gl_context_supports_pbo_upload() != 0);
    if (use_pbo != 0) {
        const void *source_base = (state->use_hdr_float_bo != 0)
                                      ? (const void *)pixels_rgba32f
                                      : (const void *)pixels_rgba8;
        db_gl_upload_ranges_target(source_base, total_bytes, ranges, span_count,
                                   DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER,
                                   state->pbo, 0, NULL, 0, 0);
    }
    db_cpu_upload_apply_ctx_t apply_ctx = {
        .state = state,
        .pixels_rgba8 = pixels_rgba8,
        .pixels_rgba32f = pixels_rgba32f,
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
    const uint32_t chunk_rows = DB_CPU_DEBUG_CLEAR_CHUNK_ROWS;
    const uint32_t chunk_bytes_u32 = db_checked_mul_u32(
        BACKEND_NAME_CPU, "cpu_debug_clear_chunk_bytes", row_bytes, chunk_rows);
    const size_t chunk_bytes = (size_t)chunk_bytes_u32;

    // If already prepared for this pixel width, nothing to do.
    if ((state->debug_clear_ready != 0) &&
        (state->debug_clear_pixel_width == pixel_width) &&
        (state->debug_clear_row_bytes == row_bytes) &&
        (state->debug_clear_chunk_rows == chunk_rows) &&
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
            state->needs_full_frame_upload = 1;
            return;
        }
    }

    state->debug_clear_pixel_width = pixel_width;
    state->debug_clear_row_bytes = row_bytes;
    state->debug_clear_chunk_rows = chunk_rows;
    state->debug_clear_buf_bytes = chunk_bytes;

    // Cache clear color bytes.
    state->debug_clear_rgba[0] =
        db_float01_to_u8_clamped(BENCH_CLEAR_COLOR_R_F);
    state->debug_clear_rgba[1] =
        db_float01_to_u8_clamped(BENCH_CLEAR_COLOR_G_F);
    state->debug_clear_rgba[2] =
        db_float01_to_u8_clamped(BENCH_CLEAR_COLOR_B_F);
    state->debug_clear_rgba[3] =
        db_float01_to_u8_clamped(BENCH_CLEAR_COLOR_A_F);

    // Fill the buffer once.
    for (size_t i = 0U; i + 3U < chunk_bytes; i += 4U) {
        state->debug_clear_buf[i + 0U] = state->debug_clear_rgba[0];
        state->debug_clear_buf[i + 1U] = state->debug_clear_rgba[1];
        state->debug_clear_buf[i + 2U] = state->debug_clear_rgba[2];
        state->debug_clear_buf[i + 3U] = state->debug_clear_rgba[3];
    }

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
        state->needs_full_frame_upload = 1;
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
        db_present_cpu_upload_ranges_prepare(state, pixel_height);
    }

    if ((debug_clear_default_framebuffer != 0) &&
        (state->use_hdr_float_bo == 0)) {
        db_present_cpu_debug_clear_prepare(state, pixel_width);
    }
}

// New: build upload ranges directly from dirty row ranges, using persistent
// scratch buffer.
static size_t db_cpu_build_upload_ranges_from_dirty_rows(
    const char *backend_name, db_cpu_present_gl_state_t *state,
    uint32_t pixel_width, uint32_t pixel_height, uint32_t pixel_bytes,
    const db_dirty_row_range_t *ranges, size_t range_count,
    db_gl_upload_range_t **out_ranges) {
    if ((out_ranges == NULL) || (state == NULL) || (pixel_width == 0U) ||
        (pixel_height == 0U)) {
        return 0U;
    }
    *out_ranges = NULL;

    if ((ranges == NULL) || (range_count == 0U)) {
        return 0U;
    }

    // Use persistent scratch buffer; never allocate in the hot path.
    if ((state->upload_ranges_buf == NULL) ||
        (state->upload_ranges_cap < (size_t)pixel_height)) {
        // Should have been prepared at init/resize; as a safe fallback, force
        // full upload.
        state->needs_full_frame_upload = 1;
        return 0U;
    }

    db_gl_upload_range_t *tmp = state->upload_ranges_buf;

    const uint32_t row_bytes = db_checked_mul_u32(backend_name, "cpu_row_bytes",
                                                  pixel_width, pixel_bytes);

    size_t out_count = 0U;
    for (size_t i = 0U; i < range_count; i++) {
        const uint32_t row_start = ranges[i].row_start;
        const uint32_t row_count = ranges[i].row_count;
        if ((row_count == 0U) || (row_start >= pixel_height)) {
            continue;
        }
        const uint32_t clamped_count = (row_start + row_count > pixel_height)
                                           ? (pixel_height - row_start)
                                           : row_count;
        const uint64_t offset_u64 = (uint64_t)row_start * (uint64_t)row_bytes;
        const uint64_t size_u64 = (uint64_t)clamped_count * (uint64_t)row_bytes;
        if ((offset_u64 > UINT32_MAX) || (size_u64 > UINT32_MAX)) {
            // pixel dimensions in DriverBench should never hit this, but keep
            // it safe.
            continue;
        }
        tmp[out_count++] = (db_gl_upload_range_t){
            .src_offset_bytes = (uint32_t)offset_u64,
            .dst_offset_bytes = (uint32_t)offset_u64,
            .size_bytes = (uint32_t)size_u64,
        };
    }

    if (out_count == 0U) {
        return 0U;
    }

    // Sort by dst_offset_bytes (simple insertion sort; out_count is small).
    for (size_t i = 1U; i < out_count; i++) {
        const db_gl_upload_range_t key = tmp[i];
        size_t insert_index = i;
        while ((insert_index > 0U) && (tmp[insert_index - 1U].dst_offset_bytes >
                                       key.dst_offset_bytes)) {
            tmp[insert_index] = tmp[insert_index - 1U];
            insert_index--;
        }
        tmp[insert_index] = key;
    }

    // Merge adjacent/overlapping ranges.
    size_t merged_count = 0U;
    for (size_t i = 0U; i < out_count; i++) {
        const db_gl_upload_range_t cur = tmp[i];
        if (merged_count == 0U) {
            tmp[merged_count++] = cur;
            continue;
        }
        db_gl_upload_range_t *prev = &tmp[merged_count - 1U];
        const uint32_t prev_end = prev->dst_offset_bytes + prev->size_bytes;
        if (cur.dst_offset_bytes <= prev_end) {
            const uint32_t cur_end = cur.dst_offset_bytes + cur.size_bytes;
            if (cur_end > prev_end) {
                prev->size_bytes = cur_end - prev->dst_offset_bytes;
            }
            // Keep src aligned with dst for CPU buffer.
            prev->src_offset_bytes = prev->dst_offset_bytes;
        } else {
            tmp[merged_count++] = cur;
        }
    }

    *out_ranges = tmp;
    return merged_count;
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
    const float *pixels_rgba32f = NULL;
    if (use_hdr_float_bo != 0) {
        pixels_rgba32f = db_renderer_cpu_renderer_pixels_rgba32f(&pixel_width,
                                                                 &pixel_height);
    } else {
        pixels_rgba8 =
            db_renderer_cpu_renderer_pixels_rgba8(&pixel_width, &pixel_height);
    }
    if (((use_hdr_float_bo != 0) && (pixels_rgba32f == NULL)) ||
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
    if (debug_clear != 0) {
        // Debug mode: clear the default framebuffer and also clear the CPU
        // present texture so missed uploads show up as the clear color.
        db_gl_clear_color_rgb(BENCH_CLEAR_COLOR_R_F, BENCH_CLEAR_COLOR_G_F,
                              BENCH_CLEAR_COLOR_B_F);
        db_gl_clear_color_buffer();
    }

    db_present_cpu_prepare_resources(state, pixel_width, pixel_height,
                                     debug_clear);
    if (state->texture == 0U) {
        db_failf(BACKEND_NAME_CPU, "CPU present texture is not initialized");
    }

    if (debug_clear != 0) {
        db_present_cpu_clear_texture_debug(state, pixel_width, pixel_height);
    }

    // Direct row-based uploading, no pattern planner.
    const int force_full_upload = (state->needs_full_frame_upload != 0) ? 1 : 0;
    const uint32_t pixel_bytes = (state->use_hdr_float_bo != 0)
                                     ? (uint32_t)DB_CPU_RGBA32F_BYTES_PER_PIXEL
                                     : DB_CPU_RGBA8_BYTES_PER_PIXEL;
    const uint32_t row_bytes = db_checked_mul_u32(
        BACKEND_NAME_CPU, "cpu_upload_row_bytes", pixel_width, pixel_bytes);

    if (force_full_upload != 0) {
        const db_gl_upload_range_t full = {
            .src_offset_bytes = 0U,
            .dst_offset_bytes = 0U,
            .size_bytes =
                db_checked_mul_u32(BACKEND_NAME_CPU, "cpu_upload_full_bytes",
                                   row_bytes, pixel_height),
        };
        db_present_cpu_upload_spans(state, (const uint8_t *)pixels_rgba8,
                                    pixels_rgba32f, pixel_width, pixel_height,
                                    &full, 1U);
        state->needs_full_frame_upload = 0;
    } else {
        db_gl_upload_range_t *upload_ranges = NULL;
        const size_t upload_span_count =
            db_cpu_build_upload_ranges_from_dirty_rows(
                BACKEND_NAME_CPU, state, pixel_width, pixel_height, pixel_bytes,
                ranges, range_count, &upload_ranges);
        if ((upload_span_count > 0U) && (upload_ranges != NULL)) {
            db_present_cpu_upload_spans(
                state, (const uint8_t *)pixels_rgba8, pixels_rgba32f,
                pixel_width, pixel_height, upload_ranges, upload_span_count);
        }
        // upload_ranges points into state scratch; do NOT free.
    }

    const float tex_u = (state->texture_width == 0U)
                            ? 1.0F
                            : (float)pixel_width / (float)state->texture_width;
    const float tex_v =
        (state->texture_height == 0U)
            ? 1.0F
            : (float)pixel_height / (float)state->texture_height;
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

static db_glfw_loop_result_t db_glfw_cpu_frame(void *user_data,
                                               uint32_t frame_index) {
    db_glfw_cpu_loop_ctx_t *ctx = (db_glfw_cpu_loop_ctx_t *)user_data;
    db_renderer_cpu_renderer_render_frame(frame_index);
    size_t damage_count = 0U;
    const db_dirty_row_range_t *damage_ranges =
        db_renderer_cpu_renderer_damage_rows(&damage_count);
    db_present_cpu_framebuffer(ctx->window, ctx->present, damage_ranges,
                               damage_count,
                               ctx->debug_clear_default_framebuffer);

    if (ctx->state_hash_enabled != 0) {
        const uint64_t state_hash = db_renderer_cpu_renderer_state_hash();
        db_display_hash_tracker_record(ctx->state_hash_tracker, state_hash);
    }
    if (ctx->output_hash_enabled != 0) {
        uint32_t pixel_width = 0U;
        uint32_t pixel_height = 0U;
        uint64_t bo_hash = 0U;
        if (db_renderer_cpu_renderer_is_hdr_float_bo() != 0) {
            const float *pixels = db_renderer_cpu_renderer_pixels_rgba32f(
                &pixel_width, &pixel_height);
            if (pixels == NULL) {
                db_failf(BACKEND_NAME_CPU,
                         "cpu renderer returned invalid HDR framebuffer");
            }
            bo_hash = db_hash_rgba32f_pixels_canonical(
                pixels, pixel_width, pixel_height,
                (size_t)pixel_width * 4U * sizeof(float), 0);
        } else {
            const uint32_t *pixels = db_renderer_cpu_renderer_pixels_rgba8(
                &pixel_width, &pixel_height);
            if (pixels == NULL) {
                db_failf(BACKEND_NAME_CPU,
                         "cpu renderer returned invalid framebuffer");
            }
            bo_hash = db_hash_rgba8_pixels_canonical(
                (const uint8_t *)pixels, pixel_width, pixel_height,
                (size_t)pixel_width * 4U, 0);
        }
        db_display_hash_tracker_record(ctx->bo_hash_tracker, bo_hash);
    }

    glfwSwapBuffers(ctx->window);
    const uint64_t logged_frames = frame_index + 1U;
    const double bench_ms =
        (db_glfw_time_seconds() - ctx->bench_start) * DB_MS_PER_SECOND_D;
    db_benchmark_log_periodic(
        db_dispatch_api_name(DB_API_CPU), db_renderer_name_cpu(),
        BACKEND_NAME_CPU, logged_frames, ctx->work_unit_count, bench_ms,
        ctx->capability_mode, &ctx->next_progress_log_due_ms,
        BENCH_LOG_INTERVAL_MS_D);
    return DB_GLFW_LOOP_CONTINUE;
}

static int db_run_glfw_window_cpu(const db_cli_config_t *cfg) {
    db_validate_runtime_environment(BACKEND_NAME_CPU,
                                    DB_RUNTIME_OPT_ALLOW_REMOTE_DISPLAY);
    db_install_signal_handlers();

    const int swap_interval =
        ((cfg != NULL) && (cfg->vsync_enabled != 0)) ? 1 : 0;
    const double fps_cap = (cfg != NULL) ? cfg->fps_cap : BENCH_FPS_CAP_D;
    const uint32_t frame_limit = (cfg != NULL) ? cfg->frame_limit : 0U;
    const db_display_hash_settings_t hash_settings =
        db_glfw_hash_settings_for_backend(cfg);

    const int gl_legacy_context_major = 2;
    const int gl_legacy_context_minor = 1;
    int is_gles = 0;
    GLFWwindow *window = db_glfw_create_gl1_5_or_gles1_1_window(
        BACKEND_NAME_CPU, "CPU Renderer GLFW DriverBench",
        BENCH_WINDOW_WIDTH_PX, BENCH_WINDOW_HEIGHT_PX, gl_legacy_context_major,
        gl_legacy_context_minor, swap_interval, &is_gles,
        (cfg != NULL) ? cfg->offscreen_enabled : 0);
    db_gl_set_proc_resolver(db_glfw_resolve_proc);
    db_gl_preload_upload_proc_table();

    const char *runtime_version = db_gl_get_version_string();
    const char *runtime_renderer = db_gl_get_renderer_string();
    const int runtime_is_gles = db_display_log_gl_runtime_api(
        BACKEND_NAME_CPU, runtime_version, runtime_renderer);
    const char *runtime_exts = db_gl_get_extensions_string();
    const int has_npot =
        (runtime_is_gles == 0) ||
        db_gl_version_text_at_least(runtime_version, 2, 0) ||
        db_has_gl_extension_token(runtime_exts, "GL_OES_texture_npot");
    const int has_pbo =
        db_gl_runtime_supports_pbo(runtime_version, runtime_exts);
    const int has_texture_float =
        db_gl_runtime_supports_texture_float(runtime_version, runtime_exts);
    if ((is_gles != 0) && (runtime_is_gles == 0)) {
        db_infof(BACKEND_NAME_CPU, "context creation reported GLES fallback, "
                                   "but runtime API is OpenGL");
    }

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
        (void)db_renderer_cpu_renderer_pixels_rgba32f(&init_pixel_width,
                                                      &init_pixel_height);
    } else {
        (void)db_renderer_cpu_renderer_pixels_rgba8(&init_pixel_width,
                                                    &init_pixel_height);
    }
    db_present_cpu_prepare_resources(
        &present, init_pixel_width, init_pixel_height,
        (cfg != NULL) ? cfg->debug_clear_default_framebuffer : 0);
    const int use_pbo_mode = ((present.has_pbo != 0) && (present.pbo != 0U) &&
                              (db_gl_context_supports_pbo_upload() != 0))
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
    const double bench_start = db_glfw_time_seconds();
    db_display_hash_tracker_t state_hash_tracker =
        db_display_hash_tracker_create(
            BACKEND_NAME_CPU, hash_settings.state_hash_enabled, "state_hash",
            (cfg != NULL) ? cfg->hash_report : "both");
    db_display_hash_tracker_t bo_hash_tracker = db_display_hash_tracker_create(
        BACKEND_NAME_CPU, hash_settings.output_hash_enabled, "bo_hash",
        (cfg != NULL) ? cfg->hash_report : "both");
    db_glfw_cpu_loop_ctx_t loop_ctx = {
        .bench_start = bench_start,
        .capability_mode = capability_mode,
        .next_progress_log_due_ms = 0.0,
        .state_hash_tracker = &state_hash_tracker,
        .bo_hash_tracker = &bo_hash_tracker,
        .state_hash_enabled = hash_settings.state_hash_enabled,
        .output_hash_enabled = hash_settings.output_hash_enabled,
        .debug_clear_default_framebuffer =
            (cfg != NULL) ? cfg->debug_clear_default_framebuffer : 0,
        .present = &present,
        .work_unit_count = work_unit_count,
        .window = window,
    };
    const db_glfw_loop_t loop = {
        .backend = BACKEND_NAME_CPU,
        .frame_fn = db_glfw_cpu_frame,
        .fps_cap = fps_cap,
        .frame_limit = frame_limit,
        .user_data = &loop_ctx,
        .window = window,
    };
    const uint64_t frames = db_glfw_run_loop(&loop);

    const double bench_ms =
        (db_glfw_time_seconds() - bench_start) * DB_MS_PER_SECOND_D;
    db_benchmark_log_final(db_dispatch_api_name(DB_API_CPU),
                           db_renderer_name_cpu(), BACKEND_NAME_CPU, frames,
                           work_unit_count, bench_ms, capability_mode);
    db_display_hash_tracker_log_final(BACKEND_NAME_CPU, &state_hash_tracker);
    db_display_hash_tracker_log_final(BACKEND_NAME_CPU, &bo_hash_tracker);

    db_renderer_cpu_renderer_shutdown();
    if (present.pbo != 0U) {
        db_gl_pbo_delete_if_valid(present.pbo);
    }
    if (present.texture != 0U) {
        db_gl_texture_delete_if_valid(&present.texture);
    }
    if (present.upload_ranges_buf != NULL) {
        free(present.upload_ranges_buf);
        present.upload_ranges_buf = NULL;
        present.upload_ranges_cap = 0U;
    }
    if (present.debug_clear_buf != NULL) {
        free(present.debug_clear_buf);
        present.debug_clear_buf = NULL;
        present.debug_clear_buf_bytes = 0U;
        present.debug_clear_ready = 0;
    }
    db_glfw_destroy_window(window);
    return 0;
}
static const char *db_gl_backend_name(db_gl_renderer_t renderer) {
    (void)renderer;
    return BACKEND_NAME_GL;
}

static const char *db_gl_renderer_name(db_gl_renderer_t renderer) {
    return (renderer == DB_GL_RENDERER_GL1_5_GLES1_1)
               ? db_renderer_name_opengl_gl1_5_gles1_1()
               : db_renderer_name_opengl_gl3_3();
}

static void db_gl_renderer_init(db_gl_renderer_t renderer) {
    if (renderer == DB_GL_RENDERER_GL1_5_GLES1_1) {
        db_renderer_opengl_gl1_5_gles1_1_init();
        return;
    }
    db_renderer_opengl_gl3_3_init();
}

static void db_gl_renderer_render_frame(db_gl_renderer_t renderer,
                                        uint32_t frame_index,
                                        int framebuffer_width_px,
                                        int framebuffer_height_px) {
    if (renderer == DB_GL_RENDERER_GL1_5_GLES1_1) {
        db_renderer_opengl_gl1_5_gles1_1_render_frame(
            frame_index, framebuffer_width_px, framebuffer_height_px, 1);
        return;
    }
    db_renderer_opengl_gl3_3_render_frame(frame_index, framebuffer_width_px,
                                          framebuffer_height_px);
}

static void db_gl_renderer_shutdown(db_gl_renderer_t renderer) {
    if (renderer == DB_GL_RENDERER_GL1_5_GLES1_1) {
        db_renderer_opengl_gl1_5_gles1_1_shutdown();
        return;
    }
    db_renderer_opengl_gl3_3_shutdown();
}

static const char *db_gl_renderer_capability_mode(db_gl_renderer_t renderer) {
    if (renderer == DB_GL_RENDERER_GL1_5_GLES1_1) {
        return db_renderer_opengl_gl1_5_gles1_1_capability_mode();
    }
    return db_renderer_opengl_gl3_3_capability_mode();
}

static uint32_t db_gl_renderer_work_unit_count(db_gl_renderer_t renderer) {
    if (renderer == DB_GL_RENDERER_GL1_5_GLES1_1) {
        return db_renderer_opengl_gl1_5_gles1_1_work_unit_count();
    }
    return db_renderer_opengl_gl3_3_work_unit_count();
}

static uint64_t db_gl_renderer_state_hash(db_gl_renderer_t renderer) {
    if (renderer == DB_GL_RENDERER_GL1_5_GLES1_1) {
        return db_renderer_opengl_gl1_5_gles1_1_state_hash();
    }
    return db_renderer_opengl_gl3_3_state_hash();
}

static void db_gl_renderer_draw_stats(db_gl_renderer_t renderer,
                                      uint64_t *full_draw_frames,
                                      uint64_t *dirty_draw_frames) {
    if (renderer == DB_GL_RENDERER_GL1_5_GLES1_1) {
        db_renderer_opengl_gl1_5_gles1_1_draw_stats(full_draw_frames,
                                                    dirty_draw_frames);
        return;
    }
    db_renderer_opengl_gl3_3_draw_stats(full_draw_frames, dirty_draw_frames);
}

static db_glfw_loop_result_t db_glfw_opengl_frame(void *user_data,
                                                  uint32_t frame_index) {
    db_glfw_opengl_loop_ctx_t *ctx = (db_glfw_opengl_loop_ctx_t *)user_data;
    int framebuffer_width_px = 0;
    int framebuffer_height_px = 0;
    glfwGetFramebufferSize(ctx->window, &framebuffer_width_px,
                           &framebuffer_height_px);
    // Do not clear here. Some renderers rely on keeping the prior frame in the
    // default framebuffer (damage-only updates) and will clear/draw explicitly
    // as needed.
    if (ctx->debug_clear_default_framebuffer != 0) {
        // Debug mode: clear the default framebuffer so damage-only renderers
        // can be visually validated (you should see missing regions as the
        // clear color).
        db_gl_set_scissor_enabled(0);
        db_gl_clear_color_rgb(BENCH_CLEAR_COLOR_R_F, BENCH_CLEAR_COLOR_G_F,
                              BENCH_CLEAR_COLOR_B_F);
        db_gl_clear_color_buffer();
    }

    db_gl_renderer_render_frame(ctx->renderer, frame_index,
                                framebuffer_width_px, framebuffer_height_px);

    if (ctx->state_hash_enabled != 0) {
        const uint64_t state_hash = db_gl_renderer_state_hash(ctx->renderer);
        db_display_hash_tracker_record(ctx->state_hash_tracker, state_hash);
    }
    if (ctx->output_hash_enabled != 0) {
        const uint8_t *framebuffer_pixels =
            db_gl_read_framebuffer_rgba8_or_fail(
                ctx->backend_name, framebuffer_width_px, framebuffer_height_px,
                ctx->hash_scratch);
        const uint64_t framebuffer_hash = db_hash_rgba8_pixels_canonical(
            framebuffer_pixels,
            db_checked_int_to_u32(ctx->backend_name, "fb_w",
                                  framebuffer_width_px),
            db_checked_int_to_u32(ctx->backend_name, "fb_h",
                                  framebuffer_height_px),
            (size_t)db_checked_int_to_u32(ctx->backend_name, "fb_row_bytes",
                                          framebuffer_width_px) *
                4U,
            1);
        db_display_hash_tracker_record(ctx->framebuffer_hash_tracker,
                                       framebuffer_hash);
    }

    glfwSwapBuffers(ctx->window);
    const uint64_t logged_frames = frame_index + 1U;
    const double bench_ms =
        (db_glfw_time_seconds() - ctx->bench_start) * DB_MS_PER_SECOND_D;
    db_benchmark_log_periodic(
        db_dispatch_api_name(DB_API_OPENGL), ctx->renderer_name,
        ctx->backend_name, logged_frames, ctx->work_unit_count, bench_ms,
        ctx->capability_mode, &ctx->next_progress_log_due_ms,
        BENCH_LOG_INTERVAL_MS_D);
    return DB_GLFW_LOOP_CONTINUE;
}

static int db_run_glfw_window_opengl(db_gl_renderer_t renderer,
                                     const db_cli_config_t *cfg) {
    const char *backend_name = db_gl_backend_name(renderer);
    db_validate_runtime_environment(backend_name,
                                    DB_RUNTIME_OPT_ALLOW_REMOTE_DISPLAY);
    db_install_signal_handlers();

    const int swap_interval =
        ((cfg != NULL) && (cfg->vsync_enabled != 0)) ? 1 : 0;
    const double fps_cap = (cfg != NULL) ? cfg->fps_cap : BENCH_FPS_CAP_D;
    const uint32_t frame_limit = (cfg != NULL) ? cfg->frame_limit : 0U;
    const db_display_hash_settings_t hash_settings =
        db_glfw_hash_settings_for_backend(cfg);

    GLFWwindow *window = NULL;
    if (renderer == DB_GL_RENDERER_GL1_5_GLES1_1) {
        const int gl_legacy_context_major = 2;
        const int gl_legacy_context_minor = 1;
        int is_gles = 0;
        window = db_glfw_create_gl1_5_or_gles1_1_window(
            backend_name, "OpenGL 1.5/GLES1.1 GLFW DriverBench",
            BENCH_WINDOW_WIDTH_PX, BENCH_WINDOW_HEIGHT_PX,
            gl_legacy_context_major, gl_legacy_context_minor, swap_interval,
            &is_gles, (cfg != NULL) ? cfg->offscreen_enabled : 0);
        db_gl_set_proc_resolver(db_glfw_resolve_proc);
        db_gl_preload_upload_proc_table();
        const char *runtime_version = db_gl_get_version_string();
        const char *runtime_renderer = db_gl_get_renderer_string();
        const int runtime_is_gles = db_display_log_gl_runtime_api(
            backend_name, runtime_version, runtime_renderer);
        if (runtime_is_gles != 0) {
            db_display_validate_gles_1x_runtime_or_fail(backend_name,
                                                        runtime_version);
        }
        if ((is_gles != 0) && (runtime_is_gles == 0)) {
            db_infof(backend_name, "context creation reported GLES fallback, "
                                   "but runtime API is OpenGL");
        }
    } else {
        const int gl3_context_major = 3;
        const int gl3_context_minor = 3;
        window = db_glfw_create_opengl_window(
            backend_name, "OpenGL 3.3 Shader GLFW DriverBench",
            BENCH_WINDOW_WIDTH_PX, BENCH_WINDOW_HEIGHT_PX, gl3_context_major,
            gl3_context_minor, 1, swap_interval,
            (cfg != NULL) ? cfg->offscreen_enabled : 0);
        db_gl_set_proc_resolver(db_glfw_resolve_proc);
        db_gl_preload_upload_proc_table();
        const char *runtime_version = db_gl_get_version_string();
        const char *runtime_renderer = db_gl_get_renderer_string();
        const int runtime_is_gles = db_display_log_gl_runtime_api(
            backend_name, runtime_version, runtime_renderer);
        if (runtime_is_gles != 0) {
            db_failf(backend_name,
                     "OpenGL ES runtime is unsupported for renderer gl3_3; "
                     "requires desktop OpenGL 3.3+");
        }
        if (!db_gl_version_text_at_least(runtime_version, 3, 3)) {
            db_failf(backend_name,
                     "desktop OpenGL runtime %s is unsupported for renderer "
                     "gl3_3; requires OpenGL 3.3+",
                     (runtime_version != NULL) ? runtime_version : "(null)");
        }
    }

    db_gl_renderer_init(renderer);
    const char *capability_mode = db_gl_renderer_capability_mode(renderer);
    const uint32_t work_unit_count = db_gl_renderer_work_unit_count(renderer);
    const double bench_start = db_glfw_time_seconds();
    db_display_hash_tracker_t state_hash_tracker =
        db_display_hash_tracker_create(
            backend_name, hash_settings.state_hash_enabled, "state_hash",
            (cfg != NULL) ? cfg->hash_report : "both");
    db_display_hash_tracker_t framebuffer_hash_tracker =
        db_display_hash_tracker_create(
            backend_name, hash_settings.output_hash_enabled, "framebuffer_hash",
            (cfg != NULL) ? cfg->hash_report : "both");
    db_gl_framebuffer_hash_scratch_t hash_scratch = {0};
    db_glfw_opengl_loop_ctx_t loop_ctx = {
        .backend_name = backend_name,
        .capability_mode = capability_mode,
        .renderer_name = db_gl_renderer_name(renderer),
        .state_hash_tracker = &state_hash_tracker,
        .framebuffer_hash_tracker = &framebuffer_hash_tracker,
        .hash_scratch = &hash_scratch,
        .renderer = renderer,
        .bench_start = bench_start,
        .next_progress_log_due_ms = 0.0,
        .state_hash_enabled = hash_settings.state_hash_enabled,
        .output_hash_enabled = hash_settings.output_hash_enabled,
        .debug_clear_default_framebuffer =
            (cfg != NULL) ? cfg->debug_clear_default_framebuffer : 0,
        .work_unit_count = work_unit_count,
        .window = window,
    };
    const db_glfw_loop_t loop = {
        .backend = backend_name,
        .frame_fn = db_glfw_opengl_frame,
        .fps_cap = fps_cap,
        .frame_limit = frame_limit,
        .user_data = &loop_ctx,
        .window = window,
    };
    const uint64_t frames = db_glfw_run_loop(&loop);

    const double bench_ms =
        (db_glfw_time_seconds() - bench_start) * DB_MS_PER_SECOND_D;
    uint64_t full_draw_frames = 0U;
    uint64_t dirty_draw_frames = 0U;
    db_gl_renderer_draw_stats(renderer, &full_draw_frames, &dirty_draw_frames);
    db_infof(backend_name,
             "draw stats: full_draw_frames=%llu dirty_draw_frames=%llu",
             (unsigned long long)full_draw_frames,
             (unsigned long long)dirty_draw_frames);
    db_benchmark_log_final(db_dispatch_api_name(DB_API_OPENGL),
                           db_gl_renderer_name(renderer), backend_name, frames,
                           work_unit_count, bench_ms, capability_mode);
    db_display_hash_tracker_log_final(backend_name, &state_hash_tracker);
    db_display_hash_tracker_log_final(backend_name, &framebuffer_hash_tracker);

    db_gl_renderer_shutdown(renderer);
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

static db_glfw_loop_result_t db_glfw_vulkan_frame(void *user_data,
                                                  uint32_t frame_index) {
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
        return DB_GLFW_LOOP_STOP;
    }
    if (frame_result == DB_VK_FRAME_RETRY) {
        return DB_GLFW_LOOP_RETRY;
    }
    return DB_GLFW_LOOP_CONTINUE;
}

static int db_run_glfw_window_vulkan(const db_cli_config_t *cfg) {
    db_validate_runtime_environment(BACKEND_NAME_VK,
                                    DB_RUNTIME_OPT_ALLOW_REMOTE_DISPLAY);
    db_install_signal_handlers();
    const double fps_cap = (cfg != NULL) ? cfg->fps_cap : BENCH_FPS_CAP_D;
    const uint32_t frame_limit = (cfg != NULL) ? cfg->frame_limit : 0U;
    const db_display_hash_settings_t hash_settings =
        db_glfw_hash_settings_for_backend(cfg);
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
    db_renderer_vulkan_1_2_multi_gpu_init(&wsi_config);
    db_display_hash_tracker_t hash_tracker = db_display_hash_tracker_create(
        BACKEND_NAME_VK, hash_settings.state_hash_enabled, "state_hash",
        (cfg != NULL) ? cfg->hash_report : "both");
    const db_glfw_vulkan_loop_ctx_t loop_ctx = {
        .backend_name = BACKEND_NAME_VK,
        .hash_tracker = &hash_tracker,
        .state_hash_enabled = hash_settings.state_hash_enabled,
    };
    const db_glfw_loop_t loop = {
        .backend = BACKEND_NAME_VK,
        .frame_fn = db_glfw_vulkan_frame,
        .fps_cap = fps_cap,
        .frame_limit = frame_limit,
        .user_data = (void *)&loop_ctx,
        .window = window,
    };
    (void)db_glfw_run_loop(&loop);
    uint64_t full_draw_frames = 0U;
    uint64_t dirty_draw_frames = 0U;
    db_renderer_vulkan_1_2_multi_gpu_draw_stats(&full_draw_frames,
                                                &dirty_draw_frames);
    db_infof(BACKEND_NAME_VK,
             "draw stats: full_draw_frames=%llu dirty_draw_frames=%llu",
             (unsigned long long)full_draw_frames,
             (unsigned long long)dirty_draw_frames);
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
