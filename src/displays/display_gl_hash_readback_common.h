#ifndef DRIVERBENCH_DISPLAY_GL_HASH_READBACK_COMMON_H
#define DRIVERBENCH_DISPLAY_GL_HASH_READBACK_COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "../core/db_alloc_policy.h"
#include "../core/db_core.h"
#include "../core/db_hash.h"
#include "../renderers/renderer_gl_common.h"

typedef struct {
    uint8_t *bytes;
    size_t size;
} db_gl_framebuffer_hash_scratch_t;

typedef struct {
    uint16_t *pixels;
    size_t value_count;
} db_gl_framebuffer_hash_f16_scratch_t;

static inline const uint8_t *db_gl_read_framebuffer_rgba8_or_fail(
    const char *backend, int width_px, int height_px,
    db_gl_framebuffer_hash_scratch_t *scratch) {
    if ((width_px <= 0) || (height_px <= 0)) {
        return NULL;
    }

    const size_t width_size =
        db_checked_int_to_size(backend, "hash_fb_width_px", width_px);
    const size_t height_size =
        db_checked_int_to_size(backend, "hash_fb_height_px", height_px);
    const size_t pixel_count = db_checked_mul_size(
        backend, "gl_hash_rgba8_pixel_count", width_size, height_size);
    const size_t byte_count = db_checked_mul_size(
        backend, "gl_hash_rgba8_byte_count", pixel_count, 4U);
    db_reserve_aligned_array_capacity_or_fail(
        (void **)&scratch->bytes, &scratch->size, byte_count, byte_count,
        sizeof(uint8_t), DB_CACHELINE_ALIGNMENT_BYTES, 0U, backend,
        "gl_hash_rgba8_bytes");

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

    const size_t width_size =
        db_checked_int_to_size(backend, "hash_fb_width_px", width_px);
    const size_t height_size =
        db_checked_int_to_size(backend, "hash_fb_height_px", height_px);
    const size_t pixel_count = db_checked_mul_size(
        backend, "gl_hash_rgba16f_pixel_count", width_size, height_size);
    const size_t required_f16_values = db_checked_mul_size(
        backend, "gl_hash_rgba16f_value_count", pixel_count, 4U);
    db_reserve_aligned_array_capacity_or_fail(
        (void **)&scratch->pixels, &scratch->value_count, required_f16_values,
        required_f16_values, sizeof(uint16_t), DB_CACHELINE_ALIGNMENT_BYTES, 0U,
        backend, "gl_hash_rgba16f_pixels");

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
    const uint32_t width_u32 =
        db_checked_int_to_u32(backend, "hash_fb_width_px", width_px);
    const uint32_t height_u32 =
        db_checked_int_to_u32(backend, "hash_fb_height_px", height_px);
    const size_t row_stride_bytes = db_checked_mul_size(
        backend, "gl_hash_rgba16f_row_stride_bytes",
        db_checked_int_to_size(backend, "hash_fb_width_px", width_px),
        4U * sizeof(uint16_t));
    return db_hash_rgba16f_pixels_canonical(rgba16f, width_u32, height_u32,
                                            row_stride_bytes, canonicalize);
}

static inline void
db_gl_hash_f16_scratch_release(db_gl_framebuffer_hash_f16_scratch_t *scratch) {
    free(scratch->pixels);
    scratch->pixels = NULL;
    scratch->value_count = 0U;
}

#endif
