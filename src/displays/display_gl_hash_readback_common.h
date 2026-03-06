#ifndef DRIVERBENCH_DISPLAY_GL_HASH_READBACK_COMMON_H
#define DRIVERBENCH_DISPLAY_GL_HASH_READBACK_COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "../core/db_core.h"
#include "../core/db_hash.h"
#include "../renderers/renderer_gl_common.h"
#include "display_gl_renderer_select_common.h"

typedef struct {
    uint8_t *bytes;
    size_t size;
} db_gl_framebuffer_hash_scratch_t;

typedef struct {
    uint16_t *pixels;
    size_t pixel_count;
} db_gl_framebuffer_hash_f16_scratch_t;

typedef struct {
    const char *backend_name;
    int framebuffer_height_px;
    int framebuffer_width_px;
    db_gl_framebuffer_hash_scratch_t *scratch;
} db_display_gl_hash_rgba8_cb_ctx_t;

typedef struct {
    const char *backend_name;
    int framebuffer_height_px;
    int framebuffer_width_px;
    db_gl_framebuffer_hash_f16_scratch_t *scratch;
} db_display_gl_hash_rgba16f_cb_ctx_t;

static inline const uint8_t *db_gl_read_framebuffer_rgba8_or_fail(
    const char *backend, int width_px, int height_px,
    db_gl_framebuffer_hash_scratch_t *scratch) {
    if ((width_px <= 0) || (height_px <= 0)) {
        return NULL;
    }

    const uint64_t pixel_count =
        (uint64_t)(uint32_t)width_px * (uint64_t)(uint32_t)height_px;
    const uint64_t byte_count_u64 = pixel_count * 4U;
    if (byte_count_u64 > (uint64_t)SIZE_MAX) {
        db_failf(backend, "Framebuffer hash size overflow");
    }

    const size_t byte_count = (size_t)byte_count_u64;
    if ((scratch->bytes == NULL) || (scratch->size < byte_count)) {
        free(scratch->bytes);
        scratch->bytes = (uint8_t *)db_alloc_aligned_array_or_fail(
            backend, "gl_hash_rgba8_bytes", byte_count, sizeof(uint8_t),
            DB_CACHELINE_ALIGNMENT_BYTES);
        scratch->size = byte_count;
    }

    db_gl_set_pack_alignment_1();
    db_gl_read_pixels_rgba8(0, 0, width_px, height_px, scratch->bytes);
    return scratch->bytes;
}

static inline void
db_gl_hash_scratch_release(db_gl_framebuffer_hash_scratch_t *scratch) {
    free(scratch->bytes);
    scratch->bytes = NULL;
    scratch->size = 0U;
}

static inline const uint16_t *db_gl_read_framebuffer_rgba16f_or_fail(
    const char *backend, int width_px, int height_px,
    db_gl_framebuffer_hash_f16_scratch_t *scratch) {
    if ((width_px <= 0) || (height_px <= 0)) {
        return NULL;
    }

    const uint64_t pixel_count_u64 =
        (uint64_t)(uint32_t)width_px * (uint64_t)(uint32_t)height_px;
    if (pixel_count_u64 > (uint64_t)SIZE_MAX) {
        db_failf(backend, "Framebuffer hash pixel count overflow");
    }

    const size_t pixel_count = (size_t)pixel_count_u64;
    if (pixel_count > (SIZE_MAX / 4U)) {
        db_failf(backend, "Framebuffer hash f16 value count overflow");
    }
    const size_t required_f16_values = pixel_count * 4U;
    if ((scratch->pixels == NULL) || (scratch->pixel_count < pixel_count)) {
        free(scratch->pixels);
        scratch->pixels = (uint16_t *)db_alloc_aligned_array_or_fail(
            backend, "gl_hash_rgba16f_pixels", required_f16_values,
            sizeof(uint16_t), DB_CACHELINE_ALIGNMENT_BYTES);
        scratch->pixel_count = pixel_count;
    }

    db_gl_set_pack_alignment_1();
    db_gl_read_pixels_rgba16f(0, 0, width_px, height_px, scratch->pixels);
    return scratch->pixels;
}

static inline uint64_t db_gl_hash_framebuffer_rgba16f_or_fail(
    const char *backend, int width_px, int height_px,
    db_gl_framebuffer_hash_f16_scratch_t *scratch, int canonicalize) {
    const uint16_t *rgba16f = db_gl_read_framebuffer_rgba16f_or_fail(
        backend, width_px, height_px, scratch);
    if (rgba16f == NULL) {
        db_failf(backend, "failed to read RGBA16F framebuffer for hashing");
    }
    const uint32_t width_u32 = (uint32_t)width_px;
    const uint32_t height_u32 = (uint32_t)height_px;
    const size_t row_stride_bytes = ((size_t)width_u32) * 4U * sizeof(uint16_t);
    return db_hash_rgba16f_pixels_canonical(rgba16f, width_u32, height_u32,
                                            row_stride_bytes, canonicalize);
}

static inline void
db_gl_hash_f16_scratch_release(db_gl_framebuffer_hash_f16_scratch_t *scratch) {
    free(scratch->pixels);
    scratch->pixels = NULL;
    scratch->pixel_count = 0U;
}

static inline uint64_t
db_display_gl_renderer_ops_state_hash_cb(void *user_data) {
    const db_display_gl_renderer_ops_t *renderer_ops =
        (const db_display_gl_renderer_ops_t *)user_data;
    if ((renderer_ops == NULL) || (renderer_ops->state_hash == NULL)) {
        return 0U;
    }
    return renderer_ops->state_hash();
}

static inline uint64_t
db_display_gl_hash_rgba8_framebuffer_cb(void *user_data) {
    const db_display_gl_hash_rgba8_cb_ctx_t *ctx =
        (const db_display_gl_hash_rgba8_cb_ctx_t *)user_data;
    if ((ctx == NULL) || (ctx->backend_name == NULL) ||
        (ctx->scratch == NULL) || (ctx->framebuffer_width_px <= 0) ||
        (ctx->framebuffer_height_px <= 0)) {
        return 0U;
    }
    const uint8_t *framebuffer_pixels = db_gl_read_framebuffer_rgba8_or_fail(
        ctx->backend_name, ctx->framebuffer_width_px,
        ctx->framebuffer_height_px, ctx->scratch);
    return db_hash_rgba8_pixels_canonical(
        framebuffer_pixels,
        db_checked_int_to_u32(ctx->backend_name, "fb_w",
                              ctx->framebuffer_width_px),
        db_checked_int_to_u32(ctx->backend_name, "fb_h",
                              ctx->framebuffer_height_px),
        (size_t)db_checked_int_to_u32(ctx->backend_name, "fb_row_bytes",
                                      ctx->framebuffer_width_px) *
            4U,
        1);
}

static inline uint64_t
db_display_gl_hash_rgba16f_framebuffer_cb(void *user_data) {
    const db_display_gl_hash_rgba16f_cb_ctx_t *ctx =
        (const db_display_gl_hash_rgba16f_cb_ctx_t *)user_data;
    if ((ctx == NULL) || (ctx->backend_name == NULL) ||
        (ctx->scratch == NULL) || (ctx->framebuffer_width_px <= 0) ||
        (ctx->framebuffer_height_px <= 0)) {
        return 0U;
    }
    return db_gl_hash_framebuffer_rgba16f_or_fail(
        ctx->backend_name, ctx->framebuffer_width_px,
        ctx->framebuffer_height_px, ctx->scratch, 1);
}

#endif
