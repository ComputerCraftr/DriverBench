#ifndef DRIVERBENCH_DISPLAY_CPU_UPLOAD_POLICY_COMMON_H
#define DRIVERBENCH_DISPLAY_CPU_UPLOAD_POLICY_COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "../core/db_core.h"

static inline void
db_display_cpu_upload_mark_force_full(int *force_full_upload) {
    if (force_full_upload == NULL) {
        return;
    }
    *force_full_upload = 1;
}

static inline int db_display_cpu_upload_should_force_full(
    int force_full_upload, const void *upload_ranges_buf,
    size_t upload_ranges_cap, uint32_t pixel_height) {
    if (force_full_upload != 0) {
        return 1;
    }
    if ((upload_ranges_buf == NULL) ||
        (upload_ranges_cap < (size_t)pixel_height)) {
        return 1;
    }
    return 0;
}

static inline void db_display_cpu_upload_ranges_ensure_capacity(
    const char *backend, const char *buffer_name, void **io_buffer,
    size_t *io_capacity, uint32_t pixel_height, size_t element_size) {
    if ((io_buffer == NULL) || (io_capacity == NULL) || (pixel_height == 0U) ||
        (element_size == 0U)) {
        return;
    }

    const size_t needed = (size_t)pixel_height;
    if ((*io_buffer != NULL) && (*io_capacity >= needed)) {
        return;
    }

    const size_t bytes = needed * element_size;
    if (*io_buffer == NULL) {
        *io_buffer =
            db_alloc_array_or_fail(backend, buffer_name, needed, element_size);
    } else {
        void *new_buffer = realloc(*io_buffer, bytes);
        if (new_buffer == NULL) {
            db_failf(backend, "%s realloc failed (bytes=%zu)", buffer_name,
                     bytes);
        }
        *io_buffer = new_buffer;
    }
    *io_capacity = needed;
}

static inline void db_display_cpu_upload_ranges_release(void **io_buffer,
                                                        size_t *io_capacity) {
    if ((io_buffer == NULL) || (io_capacity == NULL)) {
        return;
    }
    if (*io_buffer != NULL) {
        free(*io_buffer);
        *io_buffer = NULL;
    }
    *io_capacity = 0U;
}

#endif
