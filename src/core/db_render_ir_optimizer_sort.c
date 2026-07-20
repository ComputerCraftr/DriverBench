#include "db_render_ir_optimizer_internal.h"

#include "db_core.h"
#include "db_numeric.h"
#include "db_render_ir.h"
#include "db_sort.h"

#include <stddef.h>
#include <stdint.h>

static int colors_equal(db_render_ir_color_t lhs, db_render_ir_color_t rhs) {
    return (db_equal_f64(lhs.rgba[0], rhs.rgba[0]) != 0) &&
           (db_equal_f64(lhs.rgba[1], rhs.rgba[1]) != 0) &&
           (db_equal_f64(lhs.rgba[2], rhs.rgba[2]) != 0) &&
           (db_equal_f64(lhs.rgba[3], rhs.rgba[3]) != 0);
}

static int compare_fills_row_major(const void *lhs_pointer,
                                   const void *rhs_pointer) {
    const db_render_ir_fill_t *const lhs =
        (const db_render_ir_fill_t *)lhs_pointer;
    const db_render_ir_fill_t *const rhs =
        (const db_render_ir_fill_t *)rhs_pointer;
    if (lhs->rect.y != rhs->rect.y) {
        return (lhs->rect.y > rhs->rect.y) - (lhs->rect.y < rhs->rect.y);
    }
    if (lhs->rect.x != rhs->rect.x) {
        return (lhs->rect.x > rhs->rect.x) - (lhs->rect.x < rhs->rect.x);
    }
    if (lhs->rect.height != rhs->rect.height) {
        return (lhs->rect.height > rhs->rect.height) -
               (lhs->rect.height < rhs->rect.height);
    }
    return (lhs->rect.width > rhs->rect.width) -
           (lhs->rect.width < rhs->rect.width);
}

static int compare_fills_column_major(const void *lhs_pointer,
                                      const void *rhs_pointer) {
    const db_render_ir_fill_t *const lhs =
        (const db_render_ir_fill_t *)lhs_pointer;
    const db_render_ir_fill_t *const rhs =
        (const db_render_ir_fill_t *)rhs_pointer;
    if (lhs->rect.x != rhs->rect.x) {
        return (lhs->rect.x > rhs->rect.x) - (lhs->rect.x < rhs->rect.x);
    }
    if (lhs->rect.width != rhs->rect.width) {
        return (lhs->rect.width > rhs->rect.width) -
               (lhs->rect.width < rhs->rect.width);
    }
    if (lhs->rect.y != rhs->rect.y) {
        return (lhs->rect.y > rhs->rect.y) - (lhs->rect.y < rhs->rect.y);
    }
    return (lhs->rect.height > rhs->rect.height) -
           (lhs->rect.height < rhs->rect.height);
}

static int merge_adjacent_horizontal(db_render_ir_fill_t *previous,
                                     db_render_ir_fill_t next) {
    const int64_t previous_end =
        (int64_t)previous->rect.x + previous->rect.width;
    const int64_t merged_width =
        ((int64_t)next.rect.x + next.rect.width) - previous->rect.x;
    if ((previous_end != next.rect.x) || (merged_width > INT32_MAX)) {
        return 0;
    }
    previous->rect.width = (int32_t)merged_width;
    return 1;
}

static int merge_adjacent_vertical(db_render_ir_fill_t *previous,
                                   db_render_ir_fill_t next) {
    const int64_t previous_end =
        (int64_t)previous->rect.y + previous->rect.height;
    const int64_t merged_height =
        ((int64_t)next.rect.y + next.rect.height) - previous->rect.y;
    if ((previous_end != next.rect.y) || (merged_height > INT32_MAX)) {
        return 0;
    }
    previous->rect.height = (int32_t)merged_height;
    return 1;
}

static int merge_horizontal(db_render_ir_fill_t *fills, size_t *count,
                            db_render_ir_optimizer_stats_t *stats,
                            uint64_t comparison_budget) {
    size_t output_count = 0U;
    for (size_t index = 0U; index < *count; index++) {
        const db_render_ir_fill_t next = fills[index];
        if (output_count > 0U) {
            db_render_ir_fill_t *const previous = &fills[output_count - 1U];
            if (stats != NULL) {
                stats->sort_merge_comparisons++;
                if (stats->sort_merge_comparisons > comparison_budget) {
                    return 0;
                }
            }
            if (colors_equal(previous->color, next.color) &&
                (previous->rect.y == next.rect.y) &&
                (previous->rect.height == next.rect.height) &&
                (merge_adjacent_horizontal(previous, next) != 0)) {
                continue;
            }
        }
        fills[output_count++] = next;
    }
    *count = output_count;
    return 1;
}

static int merge_vertical(db_render_ir_fill_t *fills, size_t *count,
                          db_render_ir_optimizer_stats_t *stats,
                          uint64_t comparison_budget) {
    size_t output_count = 0U;
    for (size_t index = 0U; index < *count; index++) {
        const db_render_ir_fill_t next = fills[index];
        if (output_count > 0U) {
            db_render_ir_fill_t *const previous = &fills[output_count - 1U];
            if (stats != NULL) {
                stats->sort_merge_comparisons++;
                if (stats->sort_merge_comparisons > comparison_budget) {
                    return 0;
                }
            }
            if (colors_equal(previous->color, next.color) &&
                (previous->rect.x == next.rect.x) &&
                (previous->rect.width == next.rect.width) &&
                (merge_adjacent_vertical(previous, next) != 0)) {
                continue;
            }
        }
        fills[output_count++] = next;
    }
    *count = output_count;
    return 1;
}

static int sort_comparison_budget(size_t count, uint64_t *budget) {
    uint64_t levels = 0U;
    size_t remaining = count > 0U ? count - 1U : 0U;
    while (remaining > 0U) {
        levels++;
        remaining >>= 1U;
    }
    uint64_t scaled_count = 0U;
    uint64_t merge_budget = 0U;
    uint64_t adjacent_budget = 0U;
    return DB_BOOL(
        (db_try_mul_u64((uint64_t)count, UINT64_C(2), &scaled_count) != 0) &&
        (db_try_mul_u64(scaled_count, levels, &merge_budget) != 0) &&
        (db_try_mul_u64((uint64_t)count, UINT64_C(2), &adjacent_budget) != 0) &&
        (db_try_add_u64(merge_budget, adjacent_budget, budget) != 0));
}

db_render_ir_status_t
db_render_ir_sort_and_merge_fills(db_render_ir_fill_t *fills,
                                  db_render_ir_fill_t *scratch, size_t *count,
                                  db_render_ir_optimizer_stats_t *stats) {
    if ((fills == NULL) || (scratch == NULL) || (count == NULL)) {
        return DB_RENDER_IR_INVALID;
    }
    for (size_t index = 0U; index < *count; index++) {
        int32_t x_end = 0;
        int32_t y_end = 0;
        if (db_render_ir_rect_endpoints(fills[index].rect, &x_end, &y_end) ==
            0) {
            return DB_RENDER_IR_INVALID;
        }
    }
    uint64_t comparison_budget = 0U;
    if (sort_comparison_budget(*count, &comparison_budget) == 0) {
        return DB_RENDER_IR_COMPLEXITY_LIMIT;
    }
    uint64_t local_comparisons = 0U;
    uint64_t *const comparisons =
        (stats != NULL) ? &stats->sort_merge_comparisons : &local_comparisons;
    if ((db_sort_records_stable(fills, scratch, *count, sizeof(*fills),
                                compare_fills_row_major, comparison_budget,
                                comparisons) != DB_SORT_OK) ||
        (merge_horizontal(fills, count, stats, comparison_budget) == 0) ||
        (db_sort_records_stable(fills, scratch, *count, sizeof(*fills),
                                compare_fills_column_major, comparison_budget,
                                comparisons) != DB_SORT_OK) ||
        (merge_vertical(fills, count, stats, comparison_budget) == 0)) {
        return DB_RENDER_IR_COMPLEXITY_LIMIT;
    }
    return DB_RENDER_IR_OK;
}
