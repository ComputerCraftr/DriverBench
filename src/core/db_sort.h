#ifndef DRIVERBENCH_DB_SORT_H
#define DRIVERBENCH_DB_SORT_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    DB_SORT_OK = 0,
    DB_SORT_INVALID_ARGUMENT = 1,
    DB_SORT_SIZE_OVERFLOW = 2,
    DB_SORT_COMPLEXITY_LIMIT = 3,
} db_sort_status_t;

typedef int (*db_sort_compare_fn_t)(const void *lhs, const void *rhs);

/*
 * Canonical scalar ordering policy:
 * - numeric values are ascending;
 * - negative zero precedes positive zero;
 * - NaNs follow numeric values and are ordered by representation;
 * - sorting is in place and performs no DriverBench allocation.
 */
[[nodiscard]] db_sort_status_t db_sort_f64_ascending(double *values,
                                                     size_t count);
[[nodiscard]] db_sort_status_t db_sort_u32_ascending(uint32_t *values,
                                                     size_t count);
[[nodiscard]] db_sort_status_t db_sort_u64_ascending(uint64_t *values,
                                                     size_t count);
[[nodiscard]] db_sort_status_t db_sort_records(void *records, size_t count,
                                               size_t record_size,
                                               db_sort_compare_fn_t compare);
[[nodiscard]] db_sort_status_t
db_sort_records_stable(void *records, void *scratch, size_t count,
                       size_t record_size, db_sort_compare_fn_t compare,
                       uint64_t comparison_budget, uint64_t *comparison_count);

#endif
