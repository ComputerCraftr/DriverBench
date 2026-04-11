#ifndef DRIVERBENCH_DB_SORT_H
#define DRIVERBENCH_DB_SORT_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    DB_SORT_OK = 0,
    DB_SORT_INVALID_ARGUMENT = 1,
} db_sort_status_t;

/*
 * Canonical scalar ordering policy:
 * - numeric values are ascending;
 * - negative zero precedes positive zero;
 * - NaNs follow numeric values and are ordered by representation;
 * - sorting is in place and performs no DriverBench allocation.
 */
db_sort_status_t db_sort_f64_ascending(double *values, size_t count);
db_sort_status_t db_sort_u32_ascending(uint32_t *values, size_t count);

#endif
