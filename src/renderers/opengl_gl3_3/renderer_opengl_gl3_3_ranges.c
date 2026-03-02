#include "renderer_opengl_gl3_3_ranges.h"

#include <stddef.h>
#include <stdint.h>

#include "../../core/db_core.h"
#include "../renderer_benchmark_common.h"
#include "../renderer_snake_common.h"
#include "../renderer_snake_shape_common.h"

int db_gl3_step_span_row_range(const char *backend_name, uint32_t region_width,
                               uint32_t region_height, uint32_t span_start,
                               uint32_t span_count,
                               db_dirty_row_range_t *out_range) {
    if ((out_range == NULL) || (region_width == 0U) || (region_height == 0U) ||
        (span_count == 0U)) {
        return 0;
    }
    const uint64_t total_tiles = (uint64_t)region_width * region_height;
    uint64_t start = span_start;
    if (start > total_tiles) {
        start = total_tiles;
    }
    uint64_t end = start + span_count;
    if (end > total_tiles) {
        end = total_tiles;
    }
    if (end <= start) {
        return 0;
    }

    const uint32_t row_start = db_checked_u64_to_u32(
        backend_name, "gl3_dirty_row_start", start / region_width);
    const uint32_t row_end_exclusive =
        db_checked_u64_to_u32(backend_name, "gl3_dirty_row_end",
                              (end - 1U) / region_width) +
        1U;
    out_range->row_start = row_start;
    out_range->row_count = row_end_exclusive - row_start;
    return (out_range->row_count > 0U) ? 1 : 0;
}

size_t db_gl3_collect_snake_dirty_rows(const char *backend_name,
                                       const db_snake_plan_t *plan,
                                       const db_snake_region_t *region,
                                       db_dirty_row_range_t out[4]) {
    if ((plan == NULL) || (region == NULL) || (out == NULL) ||
        (region->width == 0U) || (region->height == 0U)) {
        return 0U;
    }

    size_t range_count = 0U;
    db_dirty_row_range_t raw[4] = {{0U, 0U}, {0U, 0U}, {0U, 0U}, {0U, 0U}};
    db_dirty_row_range_t local = {0U, 0U};
    if ((range_count < 4U) &&
        (db_gl3_step_span_row_range(backend_name, region->width, region->height,
                                    plan->prev_start, plan->prev_count,
                                    &local) != 0)) {
        local.row_start += region->y;
        raw[range_count++] = local;
    }
    if ((range_count < 4U) &&
        (db_gl3_step_span_row_range(backend_name, region->width, region->height,
                                    plan->active_cursor, plan->batch_size,
                                    &local) != 0)) {
        local.row_start += region->y;
        raw[range_count++] = local;
    }
    return db_gradient_normalize_row_ranges(raw, range_count, out, 4U);
}
