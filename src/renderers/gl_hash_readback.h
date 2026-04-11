#ifndef DRIVERBENCH_RENDERER_GL_HASH_READBACK_H
#define DRIVERBENCH_RENDERER_GL_HASH_READBACK_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "core/db_core.h"
#include "core/db_hash.h"
#include "gl_common.h"

typedef struct {
    db_gl_upload_stream_t stream;
} db_gl_framebuffer_hash_scratch_t;

static inline void
db_gl_hash_scratch_init_if_needed(const char *backend,
                                  db_gl_framebuffer_hash_scratch_t *scratch) {
    if ((scratch->stream.buffer_reserved_bytes != 0U) ||
        (scratch->stream.client_reserved_bytes != 0U)) {
        return;
    }
    db_gl_stream_upload_capability_t capability =
        db_gl_stream_upload_capability_probe(
            DB_GL_UPLOAD_TARGET_PBO_PACK_BUFFER, 64U, NULL, 0);
    db_gl_upload_stream_init(&scratch->stream,
                             DB_GL_UPLOAD_TARGET_PBO_PACK_BUFFER, capability,
                             0U, 1);
    (void)db_gl_upload_stream_create_owned_buffer(&scratch->stream, backend);
    db_gl_upload_stream_log_selection(&scratch->stream, backend,
                                      "framebuffer_readback");
}

static inline const uint8_t *db_gl_read_framebuffer_rgba8_or_fail(
    const char *backend, uint32_t width_px, uint32_t height_px,
    db_gl_framebuffer_hash_scratch_t *scratch) {
    if ((width_px == 0U) || (height_px == 0U)) {
        return NULL;
    }

    const size_t width_size =
        db_checked_u32_to_size(backend, "hash_fb_width_px", width_px);
    const size_t height_size =
        db_checked_u32_to_size(backend, "hash_fb_height_px", height_px);
    const size_t pixel_count = db_checked_mul_size(
        backend, "gl_hash_rgba8_pixel_count", width_size, height_size);
    const size_t byte_count = db_checked_mul_size(
        backend, "gl_hash_rgba8_byte_count", pixel_count, 4U);

    db_gl_hash_scratch_init_if_needed(backend, scratch);
    (void)db_gl_upload_stream_prepare_storage(&scratch->stream, backend,
                                              byte_count);

    db_gl_set_pack_alignment_1();
    if (db_gl_stream_upload_uses_buffer_object(&scratch->stream.capability) !=
        0) {
        (void)db_gl_upload_stream_bind(&scratch->stream);
        db_gl_read_pixels_rgba8(0, 0, width_px, height_px, NULL);
        (void)db_gl_upload_stream_unbind_target(
            DB_GL_UPLOAD_TARGET_PBO_PACK_BUFFER);
    } else {
        if (scratch->stream.client_storage == NULL) {
            DB_RUNTIME_FAIL(backend,
                            "failed to allocate client storage for rgba8 hash");
        }
        db_gl_read_pixels_rgba8(0, 0, width_px, height_px,
                                scratch->stream.client_storage);
    }

    void *mapped = db_gl_upload_stream_begin_read(&scratch->stream, backend, 0U,
                                                  byte_count);
    if (mapped == NULL) {
        if (db_gl_stream_upload_uses_buffer_object(
                &scratch->stream.capability) != 0) {
            db_gl_stream_upload_force_client_fallback(
                &scratch->stream.capability, 1);
        }
        (void)db_gl_upload_stream_prepare_storage(&scratch->stream, backend,
                                                  byte_count);
        if (scratch->stream.client_storage != NULL) {
            db_gl_read_pixels_rgba8(0, 0, width_px, height_px,
                                    scratch->stream.client_storage);
            mapped = scratch->stream.client_storage;
        }
    }

    if (mapped == NULL) {
        DB_RUNTIME_FAIL(backend, "failed to map scratch stream for rgba8 hash");
    }
    return (const uint8_t *)mapped;
}

static inline void
db_gl_hash_scratch_release(db_gl_framebuffer_hash_scratch_t *scratch) {
    (void)db_gl_upload_stream_end_read(&scratch->stream, "hash_readback");
    db_gl_upload_stream_shutdown(&scratch->stream);
}

static inline const uint16_t *db_gl_read_framebuffer_rgba16f_or_fail(
    const char *backend, uint32_t width_px, uint32_t height_px,
    db_gl_framebuffer_hash_scratch_t *scratch) {
    if ((width_px == 0U) || (height_px == 0U)) {
        return NULL;
    }

    const size_t width_size =
        db_checked_u32_to_size(backend, "hash_fb_width_px", width_px);
    const size_t height_size =
        db_checked_u32_to_size(backend, "hash_fb_height_px", height_px);
    const size_t pixel_count = db_checked_mul_size(
        backend, "gl_hash_rgba16f_pixel_count", width_size, height_size);
    const size_t required_f16_values = db_checked_mul_size(
        backend, "gl_hash_rgba16f_value_count", pixel_count, 4U);
    const size_t byte_count =
        db_checked_mul_size(backend, "gl_hash_rgba16f_byte_count",
                            required_f16_values, sizeof(uint16_t));

    db_gl_hash_scratch_init_if_needed(backend, scratch);
    (void)db_gl_upload_stream_prepare_storage(&scratch->stream, backend,
                                              byte_count);

    db_gl_set_pack_alignment_1();
    if (db_gl_stream_upload_uses_buffer_object(&scratch->stream.capability) !=
        0) {
        (void)db_gl_upload_stream_bind(&scratch->stream);
        db_gl_read_pixels_rgba16f(0, 0, width_px, height_px, NULL);
        (void)db_gl_upload_stream_unbind_target(
            DB_GL_UPLOAD_TARGET_PBO_PACK_BUFFER);
    } else {
        if (scratch->stream.client_storage == NULL) {
            DB_RUNTIME_FAIL(
                backend, "failed to allocate client storage for rgba16f hash");
        }
        db_gl_read_pixels_rgba16f(0, 0, width_px, height_px,
                                  (uint16_t *)scratch->stream.client_storage);
    }

    void *mapped = db_gl_upload_stream_begin_read(&scratch->stream, backend, 0U,
                                                  byte_count);
    if (mapped == NULL) {
        if (db_gl_stream_upload_uses_buffer_object(
                &scratch->stream.capability) != 0) {
            db_gl_stream_upload_force_client_fallback(
                &scratch->stream.capability, 1);
        }
        (void)db_gl_upload_stream_prepare_storage(&scratch->stream, backend,
                                                  byte_count);
        if (scratch->stream.client_storage != NULL) {
            db_gl_read_pixels_rgba16f(
                0, 0, width_px, height_px,
                (uint16_t *)scratch->stream.client_storage);
            mapped = scratch->stream.client_storage;
        }
    }

    if (mapped == NULL) {
        DB_RUNTIME_FAIL(backend,
                        "failed to map scratch stream for rgba16f hash");
    }
    return (const uint16_t *)mapped;
}

static inline uint64_t db_gl_hash_framebuffer_rgba16f_or_fail(
    const char *backend, uint32_t width_px, uint32_t height_px,
    db_gl_framebuffer_hash_scratch_t *scratch, int canonicalize) {
    const uint16_t *rgba16f = db_gl_read_framebuffer_rgba16f_or_fail(
        backend, width_px, height_px, scratch);
    if (rgba16f == NULL) {
        DB_RUNTIME_FAIL(backend,
                        "failed to read RGBA16F framebuffer for hashing");
    }
    const size_t row_stride_bytes = db_checked_mul_size(
        backend, "gl_hash_rgba16f_row_stride_bytes",
        db_checked_u32_to_size(backend, "hash_fb_width_px", width_px),
        4U * sizeof(uint16_t));
    const uint64_t hash =
        db_hash_working_rgba8(rgba16f, DB_PIXEL_FORMAT_RGBA16F, width_px,
                              height_px, row_stride_bytes, canonicalize);
    (void)db_gl_upload_stream_end_read(&scratch->stream, backend);
    return hash;
}
#endif
