#ifndef DRIVERBENCH_DB_ALLOC_POLICY_H
#define DRIVERBENCH_DB_ALLOC_POLICY_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "db_core.h"

static inline size_t db_size_grow_capacity_3_2(size_t current_capacity,
                                               size_t required_capacity,
                                               size_t minimum_capacity) {
    size_t capacity = current_capacity;
    if (capacity < minimum_capacity) {
        capacity = minimum_capacity;
    }
    if (capacity >= required_capacity) {
        return capacity;
    }
    while (capacity < required_capacity) {
        const size_t growth = capacity / 2U;
        if (growth > (SIZE_MAX - capacity)) {
            return required_capacity;
        }
        const size_t grown = capacity + growth;
        if (grown <= capacity) {
            return required_capacity;
        }
        capacity = grown;
    }
    return capacity;
}

static inline uint32_t db_u32_grow_capacity_3_2(uint32_t current_capacity,
                                                uint32_t required_capacity,
                                                uint32_t minimum_capacity) {
    uint32_t capacity = current_capacity;
    if (capacity < minimum_capacity) {
        capacity = minimum_capacity;
    }
    if (capacity >= required_capacity) {
        return capacity;
    }
    while (capacity < required_capacity) {
        const uint32_t growth = capacity / 2U;
        if (growth > (UINT32_MAX - capacity)) {
            return required_capacity;
        }
        const uint32_t grown = capacity + growth;
        if (grown <= capacity) {
            return required_capacity;
        }
        capacity = grown;
    }
    return capacity;
}

static inline void
db_reserve_array_capacity_or_fail(void **buffer, size_t *capacity,
                                  size_t required_capacity,
                                  size_t minimum_capacity, size_t element_size,
                                  const char *backend, const char *field_name) {
    if ((buffer == NULL) || (capacity == NULL)) {
        db_failf((backend != NULL) ? backend : "alloc_policy",
                 "%s reserve target is null",
                 (field_name != NULL) ? field_name : "buffer");
    }
    if (*capacity >= required_capacity) {
        return;
    }
    const size_t new_capacity = db_size_grow_capacity_3_2(
        *capacity, required_capacity, minimum_capacity);
    if (new_capacity < required_capacity) {
        db_failf(backend, "%s reserve capacity overflow",
                 (field_name != NULL) ? field_name : "buffer");
    }
    if (*buffer == NULL) {
        *buffer =
            db_malloc_or_fail(backend, field_name, new_capacity, element_size);
        *capacity = new_capacity;
        return;
    }
    void *const grown = realloc(*buffer, new_capacity * element_size);
    if (grown == NULL) {
        db_failf(backend, "failed to grow %s",
                 (field_name != NULL) ? field_name : "buffer");
    }
    *buffer = grown;
    *capacity = new_capacity;
}

static inline void db_reserve_aligned_array_capacity_or_fail(
    void **buffer, size_t *capacity, size_t required_capacity,
    size_t minimum_capacity, size_t element_size, size_t alignment,
    size_t preserve_count, const char *backend, const char *field_name) {
    if ((buffer == NULL) || (capacity == NULL)) {
        db_failf((backend != NULL) ? backend : "alloc_policy",
                 "%s aligned reserve target is null",
                 (field_name != NULL) ? field_name : "buffer");
    }
    if (*capacity >= required_capacity) {
        return;
    }
    const size_t new_capacity = db_size_grow_capacity_3_2(
        *capacity, required_capacity, minimum_capacity);
    if (new_capacity < required_capacity) {
        db_failf(backend, "%s aligned reserve capacity overflow",
                 (field_name != NULL) ? field_name : "buffer");
    }
    void *const new_buffer = db_calloc_or_fail(
        backend, field_name, new_capacity, element_size, alignment);
    if ((*buffer != NULL) && (preserve_count > 0U)) {
        memcpy(new_buffer, *buffer, preserve_count * element_size);
    }
    free(*buffer);
    *buffer = new_buffer;
    *capacity = new_capacity;
}

#endif
