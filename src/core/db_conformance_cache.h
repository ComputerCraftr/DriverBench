#ifndef DRIVERBENCH_CONFORMANCE_CACHE_H
#define DRIVERBENCH_CONFORMANCE_CACHE_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    DB_CONFORMANCE_UNTESTED = 0,
    DB_CONFORMANCE_CONFORMING = 1,
    DB_CONFORMANCE_NONCONFORMING = 2,
} db_conformance_result_t;

typedef enum {
    DB_CONFORMANCE_CACHE_MISS = 0,
    DB_CONFORMANCE_CACHE_HIT = 1,
    DB_CONFORMANCE_CACHE_INVALID = 2,
    DB_CONFORMANCE_CACHE_IO_ERROR = 3,
} db_conformance_cache_status_t;

const char *db_conformance_result_name(db_conformance_result_t result);
const char *
db_conformance_cache_status_name(db_conformance_cache_status_t status);
db_conformance_cache_status_t
db_conformance_cache_read(const char *path, const void *serialized_key,
                          size_t key_size, db_conformance_result_t *out_result);
db_conformance_cache_status_t
db_conformance_cache_write(const char *path, const void *serialized_key,
                           size_t key_size, db_conformance_result_t result);
int db_conformance_cache_disabled(void);

#endif
