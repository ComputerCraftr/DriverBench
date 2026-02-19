#ifndef DRIVERBENCH_DISPLAY_GLFW_WINDOW_GL_HASH_COMMON_H
#define DRIVERBENCH_DISPLAY_GLFW_WINDOW_GL_HASH_COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "../../core/db_core.h"

typedef struct {
    uint8_t *bytes;
    size_t size;
} db_gl_framebuffer_hash_scratch_t;

static inline uint64_t db_gl_hash_framebuffer_rgba8_or_fail(
    const char *backend, int width_px, int height_px,
    db_gl_framebuffer_hash_scratch_t *scratch) {
    if ((width_px <= 0) || (height_px <= 0)) {
        return 0U;
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
        scratch->bytes = (uint8_t *)malloc(byte_count);
        if (scratch->bytes == NULL) {
            db_failf(backend,
                     "Failed to allocate framebuffer hash buffer (%zu bytes)",
                     byte_count);
        }
        scratch->size = byte_count;
    }

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width_px, height_px, GL_RGBA, GL_UNSIGNED_BYTE,
                 scratch->bytes);
    return db_fnv1a64_bytes(scratch->bytes, byte_count);
}

static inline void
db_gl_hash_scratch_release(db_gl_framebuffer_hash_scratch_t *scratch) {
    free(scratch->bytes);
    scratch->bytes = NULL;
    scratch->size = 0U;
}

#endif
