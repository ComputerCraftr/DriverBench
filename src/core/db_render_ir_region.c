#include "db_render_ir.h"

#include "db_geometry.h"
#include "db_numeric.h"

#include <stddef.h>
#include <stdint.h>

typedef enum {
    REGION_UNION = 0,
    REGION_INTERSECTION = 1,
    REGION_SUBTRACT = 2,
} region_operation_t;

static int append_span(db_render_ir_store_t *store, size_t first_output_span,
                       int32_t x_start, int32_t x_end);
static int spans_equal(const db_render_ir_store_t *store,
                       const db_render_ir_band_t *lhs,
                       const db_render_ir_band_t *rhs);

static int fill_active_at_y(db_render_ir_fill_t fill, int32_t y_position) {
    return DB_BOOL((fill.rect.y <= y_position) &&
                   ((fill.rect.y + fill.rect.height) > y_position));
}

db_render_ir_status_t db_render_ir_add_fill_region(
    db_render_ir_store_t *store, const db_render_ir_fill_t *fills,
    size_t fill_count, db_render_ir_region_id_t *region_id) {
    if ((store == NULL) || (region_id == NULL) ||
        ((fills == NULL) && (fill_count > 0U)) ||
        (store->region_count >= store->region_capacity)) {
        return DB_RENDER_IR_INVALID;
    }
    for (size_t index = 0U; index < fill_count; index++) {
        db_grid_block_t checked = {0};
        if (db_render_ir_rect_to_grid_block(fills[index].rect, INT32_MAX,
                                            INT32_MAX, &checked) == 0) {
            return DB_RENDER_IR_INVALID;
        }
    }
    if ((store->region_count > UINT32_MAX) ||
        (store->band_count > UINT32_MAX) || (store->span_count > UINT32_MAX)) {
        return DB_RENDER_IR_CAPACITY;
    }
    const uint32_t first_band = (uint32_t)store->band_count;
    uint32_t band_count = 0U;
    int32_t y_position = INT32_MAX;
    for (size_t index = 0U; index < fill_count; index++) {
        if (db_render_ir_rect_is_empty(fills[index].rect) == 0) {
            y_position = DB_MIN(y_position, fills[index].rect.y);
        }
    }
    while (y_position != INT32_MAX) {
        int32_t end_y = INT32_MAX;
        for (size_t index = 0U; index < fill_count; index++) {
            const db_render_ir_rect_t rect = fills[index].rect;
            const int32_t rect_end = rect.y + rect.height;
            if ((rect.y > y_position) && (rect.y < end_y)) {
                end_y = rect.y;
            }
            if ((rect_end > y_position) && (rect_end < end_y)) {
                end_y = rect_end;
            }
        }
        if (end_y == INT32_MAX) {
            break;
        }
        const size_t first_span = store->span_count;
        int32_t search_after = INT32_MIN;
        for (;;) {
            int32_t span_start = INT32_MAX;
            int32_t span_end = INT32_MIN;
            for (size_t index = 0U; index < fill_count; index++) {
                const db_render_ir_rect_t rect = fills[index].rect;
                if ((fill_active_at_y(fills[index], y_position) != 0) &&
                    (rect.x >= search_after) && (rect.x < span_start)) {
                    span_start = rect.x;
                    span_end = rect.x + rect.width;
                }
            }
            if (span_start == INT32_MAX) {
                break;
            }
            int extended = 1;
            while (extended != 0) {
                extended = 0;
                for (size_t index = 0U; index < fill_count; index++) {
                    const db_render_ir_rect_t rect = fills[index].rect;
                    const int32_t rect_end = rect.x + rect.width;
                    if ((fill_active_at_y(fills[index], y_position) != 0) &&
                        (rect.x <= span_end) && (rect_end > span_end) &&
                        (rect_end > span_start)) {
                        span_end = rect_end;
                        extended = 1;
                    }
                }
            }
            if (!append_span(store, first_span, span_start, span_end)) {
                store->status = DB_RENDER_IR_CAPACITY;
                return store->status;
            }
            if (span_end == INT32_MAX) {
                break;
            }
            search_after = span_end + 1;
        }
        const size_t span_count = store->span_count - first_span;
        if (span_count > 0U) {
            if ((first_span > UINT32_MAX) || (span_count > UINT32_MAX)) {
                store->status = DB_RENDER_IR_CAPACITY;
                return store->status;
            }
            if (store->band_count >= store->band_capacity) {
                store->status = DB_RENDER_IR_CAPACITY;
                return store->status;
            }
            const db_render_ir_band_t band = {
                .y_start = y_position,
                .y_end = end_y,
                .first_span = (uint32_t)first_span,
                .span_count = (uint32_t)span_count,
            };
            if ((band_count > 0U) &&
                (store->bands[store->band_count - 1U].y_end == y_position) &&
                spans_equal(store, &store->bands[store->band_count - 1U],
                            &band)) {
                store->bands[store->band_count - 1U].y_end = end_y;
                store->span_count = first_span;
            } else {
                store->bands[store->band_count++] = band;
                band_count++;
            }
        }
        y_position = end_y;
    }
    *region_id = (uint32_t)store->region_count;
    store->regions[store->region_count++] = (db_render_ir_region_t){
        .first_band = first_band, .band_count = band_count};
    return DB_RENDER_IR_OK;
}

static const db_render_ir_band_t *
active_band(const db_render_ir_store_t *store,
            const db_render_ir_region_t *region, int32_t y_position) {
    for (uint32_t index = 0U; index < region->band_count; index++) {
        const db_render_ir_band_t *const band =
            &store->bands[region->first_band + index];
        if ((band->y_start <= y_position) && (band->y_end > y_position)) {
            return band;
        }
    }
    return NULL;
}

static int32_t first_y(const db_render_ir_store_t *store,
                       const db_render_ir_region_t *lhs,
                       const db_render_ir_region_t *rhs) {
    int32_t result = INT32_MAX;
    if (lhs->band_count > 0U) {
        result = store->bands[lhs->first_band].y_start;
    }
    if (rhs->band_count > 0U) {
        result = DB_MIN(result, store->bands[rhs->first_band].y_start);
    }
    return result;
}

static int32_t next_y(const db_render_ir_store_t *store,
                      const db_render_ir_region_t *lhs,
                      const db_render_ir_region_t *rhs, int32_t current) {
    int32_t result = INT32_MAX;
    const db_render_ir_region_t regions[2] = {*lhs, *rhs};
    for (size_t region_index = 0U; region_index < 2U; region_index++) {
        for (uint32_t index = 0U; index < regions[region_index].band_count;
             index++) {
            const db_render_ir_band_t band =
                store->bands[regions[region_index].first_band + index];
            if ((band.y_start > current) && (band.y_start < result)) {
                result = band.y_start;
            }
            if ((band.y_end > current) && (band.y_end < result)) {
                result = band.y_end;
            }
        }
    }
    return result;
}

static int append_span(db_render_ir_store_t *store, size_t first_output_span,
                       int32_t x_start, int32_t x_end) {
    if (x_end <= x_start) {
        return 1;
    }
    if (store->span_count >= store->span_capacity) {
        return 0;
    }
    if (store->span_count > first_output_span) {
        db_render_ir_span_t *const previous =
            &store->spans[store->span_count - 1U];
        if (x_start <= previous->x_end) {
            previous->x_end = DB_MAX(previous->x_end, x_end);
            return 1;
        }
    }
    store->spans[store->span_count++] =
        (db_render_ir_span_t){.x_start = x_start, .x_end = x_end};
    return 1;
}

static int emit_union_spans(db_render_ir_store_t *store,
                            const db_render_ir_band_t *lhs,
                            const db_render_ir_band_t *rhs,
                            size_t first_output_span) {
    uint32_t lhs_index = 0U;
    uint32_t rhs_index = 0U;
    while (((lhs != NULL) && (lhs_index < lhs->span_count)) ||
           ((rhs != NULL) && (rhs_index < rhs->span_count))) {
        const int lhs_available =
            DB_BOOL((lhs != NULL) && (lhs_index < lhs->span_count));
        const int rhs_available =
            DB_BOOL((rhs != NULL) && (rhs_index < rhs->span_count));
        db_render_ir_span_t span = {0};
        if ((rhs_available == 0) ||
            ((lhs_available != 0) &&
             (store->spans[lhs->first_span + lhs_index].x_start <=
              store->spans[rhs->first_span + rhs_index].x_start))) {
            span = store->spans[lhs->first_span + lhs_index++];
        } else {
            span = store->spans[rhs->first_span + rhs_index++];
        }
        if (!append_span(store, first_output_span, span.x_start, span.x_end)) {
            return 0;
        }
    }
    return 1;
}

static int emit_intersection_spans(db_render_ir_store_t *store,
                                   const db_render_ir_band_t *lhs,
                                   const db_render_ir_band_t *rhs,
                                   size_t first_output_span) {
    if ((lhs == NULL) || (rhs == NULL)) {
        return 1;
    }
    uint32_t lhs_index = 0U;
    uint32_t rhs_index = 0U;
    while ((lhs_index < lhs->span_count) && (rhs_index < rhs->span_count)) {
        const db_render_ir_span_t left =
            store->spans[lhs->first_span + lhs_index];
        const db_render_ir_span_t right =
            store->spans[rhs->first_span + rhs_index];
        if (!append_span(store, first_output_span,
                         DB_MAX(left.x_start, right.x_start),
                         DB_MIN(left.x_end, right.x_end))) {
            return 0;
        }
        if (left.x_end <= right.x_end) {
            lhs_index++;
        }
        if (right.x_end <= left.x_end) {
            rhs_index++;
        }
    }
    return 1;
}

static int emit_subtract_spans(db_render_ir_store_t *store,
                               const db_render_ir_band_t *lhs,
                               const db_render_ir_band_t *rhs,
                               size_t first_output_span) {
    if (lhs == NULL) {
        return 1;
    }
    for (uint32_t lhs_index = 0U; lhs_index < lhs->span_count; lhs_index++) {
        const db_render_ir_span_t left =
            store->spans[lhs->first_span + lhs_index];
        int32_t cursor = left.x_start;
        if (rhs != NULL) {
            for (uint32_t rhs_index = 0U; rhs_index < rhs->span_count;
                 rhs_index++) {
                const db_render_ir_span_t right =
                    store->spans[rhs->first_span + rhs_index];
                if ((right.x_end <= cursor) || (right.x_start >= left.x_end)) {
                    continue;
                }
                if (!append_span(store, first_output_span, cursor,
                                 DB_MIN(right.x_start, left.x_end))) {
                    return 0;
                }
                cursor = DB_MAX(cursor, right.x_end);
                if (cursor >= left.x_end) {
                    break;
                }
            }
        }
        if (!append_span(store, first_output_span, cursor, left.x_end)) {
            return 0;
        }
    }
    return 1;
}

static int spans_equal(const db_render_ir_store_t *store,
                       const db_render_ir_band_t *lhs,
                       const db_render_ir_band_t *rhs) {
    if (lhs->span_count != rhs->span_count) {
        return 0;
    }
    for (uint32_t index = 0U; index < lhs->span_count; index++) {
        const db_render_ir_span_t left = store->spans[lhs->first_span + index];
        const db_render_ir_span_t right = store->spans[rhs->first_span + index];
        if ((left.x_start != right.x_start) || (left.x_end != right.x_end)) {
            return 0;
        }
    }
    return 1;
}

static db_render_ir_status_t region_binary(db_render_ir_store_t *store,
                                           db_render_ir_region_id_t lhs_id,
                                           db_render_ir_region_id_t rhs_id,
                                           db_render_ir_region_id_t *region_id,
                                           region_operation_t operation) {
    if ((store == NULL) || (region_id == NULL) ||
        (lhs_id >= store->region_count) || (rhs_id >= store->region_count) ||
        (store->region_count >= store->region_capacity)) {
        return DB_RENDER_IR_INVALID;
    }
    if ((store->region_count > UINT32_MAX) ||
        (store->band_count > UINT32_MAX) || (store->span_count > UINT32_MAX)) {
        return DB_RENDER_IR_CAPACITY;
    }
    const db_render_ir_region_t lhs = store->regions[lhs_id];
    const db_render_ir_region_t rhs = store->regions[rhs_id];
    const uint32_t first_band = (uint32_t)store->band_count;
    uint32_t band_count = 0U;
    int32_t y_position = first_y(store, &lhs, &rhs);
    while (y_position != INT32_MAX) {
        const int32_t end_y = next_y(store, &lhs, &rhs, y_position);
        if (end_y == INT32_MAX) {
            break;
        }
        if (store->band_count >= store->band_capacity) {
            store->status = DB_RENDER_IR_CAPACITY;
            return store->status;
        }
        const size_t first_span = store->span_count;
        const db_render_ir_band_t *const lhs_band =
            active_band(store, &lhs, y_position);
        const db_render_ir_band_t *const rhs_band =
            active_band(store, &rhs, y_position);
        int emitted = 0;
        switch (operation) {
        case REGION_UNION:
            emitted = emit_union_spans(store, lhs_band, rhs_band, first_span);
            break;
        case REGION_INTERSECTION:
            emitted =
                emit_intersection_spans(store, lhs_band, rhs_band, first_span);
            break;
        case REGION_SUBTRACT:
            emitted =
                emit_subtract_spans(store, lhs_band, rhs_band, first_span);
            break;
        }
        if (emitted == 0) {
            store->status = DB_RENDER_IR_CAPACITY;
            return store->status;
        }
        const size_t span_count = store->span_count - first_span;
        if (span_count > 0U) {
            if ((first_span > UINT32_MAX) || (span_count > UINT32_MAX)) {
                store->status = DB_RENDER_IR_CAPACITY;
                return store->status;
            }
            db_render_ir_band_t band = {
                .y_start = y_position,
                .y_end = end_y,
                .first_span = (uint32_t)first_span,
                .span_count = (uint32_t)span_count,
            };
            if ((band_count > 0U) &&
                (store->bands[store->band_count - 1U].y_end == y_position) &&
                spans_equal(store, &store->bands[store->band_count - 1U],
                            &band)) {
                store->bands[store->band_count - 1U].y_end = end_y;
                store->span_count = first_span;
            } else {
                store->bands[store->band_count++] = band;
                band_count++;
            }
        }
        y_position = end_y;
    }
    *region_id = (uint32_t)store->region_count;
    store->regions[store->region_count++] = (db_render_ir_region_t){
        .first_band = first_band, .band_count = band_count};
    return DB_RENDER_IR_OK;
}

db_render_ir_status_t db_render_ir_region_union(
    db_render_ir_store_t *store, db_render_ir_region_id_t lhs,
    db_render_ir_region_id_t rhs, db_render_ir_region_id_t *region_id) {
    return region_binary(store, lhs, rhs, region_id, REGION_UNION);
}

db_render_ir_status_t db_render_ir_region_intersection(
    db_render_ir_store_t *store, db_render_ir_region_id_t lhs,
    db_render_ir_region_id_t rhs, db_render_ir_region_id_t *region_id) {
    return region_binary(store, lhs, rhs, region_id, REGION_INTERSECTION);
}

db_render_ir_status_t db_render_ir_region_subtract(
    db_render_ir_store_t *store, db_render_ir_region_id_t lhs,
    db_render_ir_region_id_t rhs, db_render_ir_region_id_t *region_id) {
    return region_binary(store, lhs, rhs, region_id, REGION_SUBTRACT);
}

uint64_t db_render_ir_region_area(const db_render_ir_view_t *view,
                                  db_render_ir_region_id_t region_id) {
    if ((view == NULL) || (region_id >= view->region_count)) {
        return 0U;
    }
    uint64_t area = 0U;
    const db_render_ir_region_t region = view->regions[region_id];
    for (uint32_t band_index = 0U; band_index < region.band_count;
         band_index++) {
        const db_render_ir_band_t band =
            view->bands[region.first_band + band_index];
        const uint64_t height = (uint64_t)(uint32_t)(band.y_end - band.y_start);
        for (uint32_t span_index = 0U; span_index < band.span_count;
             span_index++) {
            const db_render_ir_span_t span =
                view->spans[band.first_span + span_index];
            const uint64_t width =
                (uint64_t)(uint32_t)(span.x_end - span.x_start);
            area += width * height;
        }
    }
    return area;
}
