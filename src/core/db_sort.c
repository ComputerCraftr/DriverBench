#include "db_sort.h"

#include "db_numeric.h"

#include <stdint.h>
#include <stdlib.h>

static int qsort_compare_f64(const void *lhs_pointer, const void *rhs_pointer) {
    const double lhs = *(const double *)lhs_pointer;
    const double rhs = *(const double *)rhs_pointer;
    return db_compare_f64_total(lhs, rhs);
}

static int qsort_compare_u32(const void *lhs_pointer, const void *rhs_pointer) {
    const uint32_t lhs = *(const uint32_t *)lhs_pointer;
    const uint32_t rhs = *(const uint32_t *)rhs_pointer;
    return db_compare_u32(lhs, rhs);
}

db_sort_status_t db_sort_f64_ascending(double *values, size_t count) {
    if ((values == NULL) && (count > 0U)) {
        return DB_SORT_INVALID_ARGUMENT;
    }
    if (count > (SIZE_MAX / sizeof(*values))) {
        return DB_SORT_SIZE_OVERFLOW;
    }
    if (count > 1U) {
        qsort(values, count, sizeof(*values), qsort_compare_f64);
    }
    return DB_SORT_OK;
}

db_sort_status_t db_sort_u32_ascending(uint32_t *values, size_t count) {
    if ((values == NULL) && (count > 0U)) {
        return DB_SORT_INVALID_ARGUMENT;
    }
    if (count > (SIZE_MAX / sizeof(*values))) {
        return DB_SORT_SIZE_OVERFLOW;
    }
    if (count > 1U) {
        qsort(values, count, sizeof(*values), qsort_compare_u32);
    }
    return DB_SORT_OK;
}

db_sort_status_t db_sort_records(void *records, size_t count,
                                 size_t record_size,
                                 db_sort_compare_fn_t compare) {
    if (((records == NULL) && (count > 0U)) || (record_size == 0U) ||
        (compare == NULL)) {
        return DB_SORT_INVALID_ARGUMENT;
    }
    if (count > (SIZE_MAX / record_size)) {
        return DB_SORT_SIZE_OVERFLOW;
    }
    if (count > 1U) {
        qsort(records, count, record_size, compare);
    }
    return DB_SORT_OK;
}
