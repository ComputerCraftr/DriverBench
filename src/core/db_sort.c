#include "db_sort.h"

#include "db_core.h"
#include "db_numeric.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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

static int qsort_compare_u64(const void *lhs_pointer, const void *rhs_pointer) {
    const uint64_t lhs = *(const uint64_t *)lhs_pointer;
    const uint64_t rhs = *(const uint64_t *)rhs_pointer;
    return (lhs > rhs) - (lhs < rhs);
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

db_sort_status_t db_sort_u64_ascending(uint64_t *values, size_t count) {
    if ((values == NULL) && (count > 0U)) {
        return DB_SORT_INVALID_ARGUMENT;
    }
    if (count > (SIZE_MAX / sizeof(*values))) {
        return DB_SORT_SIZE_OVERFLOW;
    }
    if (count > 1U) {
        qsort(values, count, sizeof(*values), qsort_compare_u64);
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

static int stable_sort_budget_is_sufficient(size_t count,
                                            uint64_t comparison_budget,
                                            uint64_t comparison_count) {
    uint64_t levels = 0U;
    size_t width = 1U;
    while (width < count) {
        levels++;
        width = (width > (count / 2U)) ? count : width * 2U;
    }
    uint64_t maximum_comparisons = 0U;
    return DB_BOOL(
        (comparison_count <= comparison_budget) &&
        (db_try_mul_u64((uint64_t)count, levels, &maximum_comparisons) != 0) &&
        (maximum_comparisons <= (comparison_budget - comparison_count)));
}

db_sort_status_t db_sort_records_stable(void *records, void *scratch,
                                        size_t count, size_t record_size,
                                        db_sort_compare_fn_t compare,
                                        uint64_t comparison_budget,
                                        uint64_t *comparison_count) {
    if (((records == NULL) && (count > 0U)) ||
        ((scratch == NULL) && (count > 1U)) || (record_size == 0U) ||
        (compare == NULL) || (comparison_count == NULL)) {
        return DB_SORT_INVALID_ARGUMENT;
    }
    if (count > (SIZE_MAX / record_size)) {
        return DB_SORT_SIZE_OVERFLOW;
    }
    const size_t byte_count = count * record_size;
    if ((count < 2U) || (byte_count == 0U)) {
        return DB_SORT_OK;
    }
    int ranges_overlap = 0;
    if ((db_memory_ranges_overlap(records, byte_count, scratch, byte_count,
                                  &ranges_overlap) == 0) ||
        (ranges_overlap != 0)) {
        return DB_SORT_INVALID_ARGUMENT;
    }
    if (stable_sort_budget_is_sufficient(count, comparison_budget,
                                         *comparison_count) == 0) {
        return DB_SORT_COMPLEXITY_LIMIT;
    }

    unsigned char *source = (unsigned char *)records;
    unsigned char *destination = (unsigned char *)scratch;
    size_t width = 1U;
    while (width < count) {
        size_t first = 0U;
        while (first < count) {
            const size_t middle = first + DB_MIN(width, count - first);
            const size_t end = middle + DB_MIN(width, count - middle);
            size_t left = first;
            size_t right = middle;
            size_t output = first;
            while ((left < middle) && (right < end)) {
                (*comparison_count)++;
                const unsigned char *const left_record =
                    source + (left * record_size);
                const unsigned char *const right_record =
                    source + (right * record_size);
                const unsigned char *selected = NULL;
                if (compare(left_record, right_record) <= 0) {
                    selected = left_record;
                    left++;
                } else {
                    selected = right_record;
                    right++;
                }
                memcpy(destination + (output * record_size), selected,
                       record_size);
                output++;
            }
            while (left < middle) {
                memcpy(destination + (output * record_size),
                       source + (left * record_size), record_size);
                left++;
                output++;
            }
            while (right < end) {
                memcpy(destination + (output * record_size),
                       source + (right * record_size), record_size);
                right++;
                output++;
            }
            first = end;
        }
        unsigned char *const prior_source = source;
        source = destination;
        destination = prior_source;
        width = (width > (count / 2U)) ? count : width * 2U;
    }
    if (source != records) {
        memcpy(records, source, byte_count);
    }
    return DB_SORT_OK;
}
