#include "db_render_ir.h"

#include "db_core.h"
#include "db_geometry.h"
#include "db_numeric.h"
#include "db_sort.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

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

int db_render_ir_region_validate(const db_render_ir_view_t *view,
                                 db_render_ir_region_id_t region_id,
                                 size_t *span_count) {
    if ((view == NULL) || (span_count == NULL) ||
        ((view->region_count > 0U) && (view->regions == NULL)) ||
        ((view->band_count > 0U) && (view->bands == NULL)) ||
        ((view->span_count > 0U) && (view->spans == NULL)) ||
        (region_id >= view->region_count)) {
        return 0;
    }
    const db_render_ir_region_t region = view->regions[region_id];
    if ((region.first_band > view->band_count) ||
        (region.band_count > (view->band_count - region.first_band))) {
        return 0;
    }
    size_t total_spans = 0U;
    int32_t previous_y_end = -1;
    db_render_ir_band_t previous_band = {0};
    int have_previous_band = 0;
    for (uint32_t band_index = 0U; band_index < region.band_count;
         band_index++) {
        const db_render_ir_band_t band =
            view->bands[region.first_band + band_index];
        if ((band.y_start < 0) || (band.y_end <= band.y_start) ||
            (band.y_start < previous_y_end) ||
            (band.first_span > view->span_count) ||
            (band.span_count > (view->span_count - band.first_span)) ||
            (total_spans > (SIZE_MAX - band.span_count))) {
            return 0;
        }
        int32_t previous_x_end = -1;
        for (uint32_t span_index = 0U; span_index < band.span_count;
             span_index++) {
            const db_render_ir_span_t span =
                view->spans[band.first_span + span_index];
            if ((span.x_start < 0) || (span.x_end <= span.x_start) ||
                (span.x_start <= previous_x_end)) {
                return 0;
            }
            previous_x_end = span.x_end;
        }
        if ((have_previous_band != 0) && (band.y_start == previous_y_end) &&
            (band.span_count == previous_band.span_count)) {
            int identical = 1;
            for (uint32_t span_index = 0U; span_index < band.span_count;
                 span_index++) {
                const db_render_ir_span_t lhs =
                    view->spans[band.first_span + span_index];
                const db_render_ir_span_t rhs =
                    view->spans[previous_band.first_span + span_index];
                if ((lhs.x_start != rhs.x_start) || (lhs.x_end != rhs.x_end)) {
                    identical = 0;
                    break;
                }
            }
            if (identical != 0) {
                return 0;
            }
        }
        total_spans += band.span_count;
        previous_y_end = band.y_end;
        previous_band = band;
        have_previous_band = 1;
    }
    *span_count = total_spans;
    return 1;
}

db_render_ir_status_t
db_render_ir_region_import(const db_render_ir_view_t *source,
                           db_render_ir_region_id_t source_region,
                           db_render_ir_store_t *destination,
                           db_render_ir_region_id_t *destination_region) {
    size_t imported_span_count = 0U;
    const db_render_ir_storage_relation_t storage_relation =
        db_render_ir_view_store_relation(source, destination);
    if ((destination == NULL) || (destination->regions == NULL) ||
        (destination->bands == NULL) || (destination->spans == NULL) ||
        (destination_region == NULL) ||
        (storage_relation != DB_RENDER_IR_STORAGE_DISJOINT) ||
        (db_render_ir_region_validate(source, source_region,
                                      &imported_span_count) == 0)) {
        return DB_RENDER_IR_INVALID;
    }
    const db_render_ir_region_t region = source->regions[source_region];
    if (region.band_count == 0U) {
        *destination_region = DB_RENDER_IR_INVALID_ID;
        return DB_RENDER_IR_OK;
    }
    if ((destination->region_count >= destination->region_capacity) ||
        (destination->band_count > destination->band_capacity) ||
        (destination->span_count > destination->span_capacity) ||
        (region.band_count >
         (destination->band_capacity - destination->band_count)) ||
        (imported_span_count >
         (destination->span_capacity - destination->span_count)) ||
        (destination->region_count >= UINT32_MAX) ||
        (destination->band_count > UINT32_MAX) ||
        (destination->span_count > UINT32_MAX)) {
        return DB_RENDER_IR_CAPACITY;
    }

    const uint32_t first_band = (uint32_t)destination->band_count;
    for (uint32_t band_index = 0U; band_index < region.band_count;
         band_index++) {
        const db_render_ir_band_t source_band =
            source->bands[region.first_band + band_index];
        const uint32_t first_span = (uint32_t)destination->span_count;
        for (uint32_t span_index = 0U; span_index < source_band.span_count;
             span_index++) {
            destination->spans[destination->span_count++] =
                source->spans[source_band.first_span + span_index];
        }
        destination->bands[destination->band_count++] = (db_render_ir_band_t){
            .y_start = source_band.y_start,
            .y_end = source_band.y_end,
            .first_span = first_span,
            .span_count = source_band.span_count,
        };
    }
    *destination_region = (uint32_t)destination->region_count;
    destination->regions[destination->region_count++] = (db_render_ir_region_t){
        .first_band = first_band,
        .band_count = region.band_count,
    };
    return DB_RENDER_IR_OK;
}

static int fill_active_at_y(db_render_ir_fill_t fill, int32_t y_position) {
    return DB_BOOL((fill.rect.y <= y_position) &&
                   ((fill.rect.y + fill.rect.height) > y_position));
}

static int compare_spans(const void *lhs_pointer, const void *rhs_pointer) {
    const db_render_ir_span_t *const lhs =
        (const db_render_ir_span_t *)lhs_pointer;
    const db_render_ir_span_t *const rhs =
        (const db_render_ir_span_t *)rhs_pointer;
    if (lhs->x_start != rhs->x_start) {
        return (lhs->x_start > rhs->x_start) - (lhs->x_start < rhs->x_start);
    }
    return (lhs->x_end > rhs->x_end) - (lhs->x_end < rhs->x_end);
}

static int insert_normalized_span(db_render_ir_store_t *store,
                                  size_t first_span,
                                  db_render_ir_span_t inserted) {
    size_t first = first_span;
    while ((first < store->span_count) &&
           (store->spans[first].x_end < inserted.x_start)) {
        first++;
    }
    size_t after = first;
    while ((after < store->span_count) &&
           (store->spans[after].x_start <= inserted.x_end)) {
        inserted.x_start =
            DB_MIN(inserted.x_start, store->spans[after].x_start);
        inserted.x_end = DB_MAX(inserted.x_end, store->spans[after].x_end);
        after++;
    }
    if (first == after) {
        if (store->span_count >= store->span_capacity) {
            return 0;
        }
        const size_t move_count = store->span_count - first;
        size_t move_bytes = 0U;
        if (db_try_mul_size(move_count, sizeof(*store->spans), &move_bytes) ==
            0) {
            return 0;
        }
        memmove(&store->spans[first + 1U], &store->spans[first], move_bytes);
        store->span_count++;
    } else if (after > (first + 1U)) {
        const size_t move_count = store->span_count - after;
        size_t move_bytes = 0U;
        if (db_try_mul_size(move_count, sizeof(*store->spans), &move_bytes) ==
            0) {
            return 0;
        }
        memmove(&store->spans[first + 1U], &store->spans[after], move_bytes);
        store->span_count -= after - first - 1U;
    }
    store->spans[first] = inserted;
    return 1;
}

static int append_normalized_active_spans(db_render_ir_store_t *store,
                                          const db_render_ir_fill_t *fills,
                                          size_t fill_count, int32_t y_position,
                                          size_t first_span) {
    size_t active_count = 0U;
    for (size_t index = 0U; index < fill_count; index++) {
        active_count +=
            (size_t)DB_BOOL(fill_active_at_y(fills[index], y_position) != 0);
    }
    if (active_count > (store->span_capacity - store->span_count)) {
        for (size_t index = 0U; index < fill_count; index++) {
            if (fill_active_at_y(fills[index], y_position) == 0) {
                continue;
            }
            const db_render_ir_rect_t rect = fills[index].rect;
            if (insert_normalized_span(
                    store, first_span,
                    (db_render_ir_span_t){.x_start = rect.x,
                                          .x_end = rect.x + rect.width}) == 0) {
                return 0;
            }
        }
        return 1;
    }
    for (size_t index = 0U; index < fill_count; index++) {
        if (fill_active_at_y(fills[index], y_position) == 0) {
            continue;
        }
        if (store->span_count >= store->span_capacity) {
            return 0;
        }
        const db_render_ir_rect_t rect = fills[index].rect;
        store->spans[store->span_count++] = (db_render_ir_span_t){
            .x_start = rect.x, .x_end = rect.x + rect.width};
    }
    const size_t unsorted_count = store->span_count - first_span;
    if (db_sort_records(&store->spans[first_span], unsorted_count,
                        sizeof(store->spans[first_span]),
                        compare_spans) != DB_SORT_OK) {
        return 0;
    }
    size_t output = first_span;
    for (size_t index = first_span; index < store->span_count; index++) {
        const db_render_ir_span_t span = store->spans[index];
        if ((output > first_span) &&
            (span.x_start <= store->spans[output - 1U].x_end)) {
            store->spans[output - 1U].x_end =
                DB_MAX(store->spans[output - 1U].x_end, span.x_end);
        } else {
            store->spans[output++] = span;
        }
    }
    store->span_count = output;
    return 1;
}

db_render_ir_status_t db_render_ir_add_fill_region(
    db_render_ir_store_t *store, const db_render_ir_fill_t *fills,
    size_t fill_count, db_render_ir_region_id_t *region_id) {
    if ((store == NULL) || (store->regions == NULL) || (store->bands == NULL) ||
        (store->spans == NULL) || (region_id == NULL) ||
        ((fills == NULL) && (fill_count > 0U)) ||
        (store->region_count >= store->region_capacity)) {
        return DB_RENDER_IR_INVALID;
    }
    if ((store->band_count > store->band_capacity) ||
        (store->span_count > store->span_capacity)) {
        return DB_RENDER_IR_CAPACITY;
    }
    for (size_t index = 0U; index < fill_count; index++) {
        db_grid_block_t checked = {0};
        if (db_render_ir_rect_to_grid_block(fills[index].rect, INT32_MAX,
                                            INT32_MAX, &checked) == 0) {
            return DB_RENDER_IR_INVALID;
        }
    }
    if ((store->region_count >= UINT32_MAX) ||
        (store->band_count > UINT32_MAX) || (store->span_count > UINT32_MAX)) {
        return DB_RENDER_IR_CAPACITY;
    }
    const size_t initial_band_count = store->band_count;
    const size_t initial_span_count = store->span_count;
    const uint32_t first_band = (uint32_t)initial_band_count;
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
        if (append_normalized_active_spans(store, fills, fill_count, y_position,
                                           first_span) == 0) {
            store->status = DB_RENDER_IR_CAPACITY;
            store->band_count = initial_band_count;
            store->span_count = initial_span_count;
            return store->status;
        }
        const size_t span_count = store->span_count - first_span;
        if (span_count > 0U) {
            if ((first_span > UINT32_MAX) || (span_count > UINT32_MAX)) {
                store->status = DB_RENDER_IR_CAPACITY;
                store->band_count = initial_band_count;
                store->span_count = initial_span_count;
                return store->status;
            }
            if (store->band_count >= store->band_capacity) {
                store->status = DB_RENDER_IR_CAPACITY;
                store->band_count = initial_band_count;
                store->span_count = initial_span_count;
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

typedef struct {
    db_render_ir_region_t region;
    uint32_t index;
} region_band_cursor_t;

static const db_render_ir_band_t *
region_cursor_band(const db_render_ir_store_t *store,
                   region_band_cursor_t *cursor, int32_t y_position) {
    while (cursor->index < cursor->region.band_count) {
        const db_render_ir_band_t *const band =
            &store->bands[cursor->region.first_band + cursor->index];
        if (band->y_end <= y_position) {
            cursor->index++;
            continue;
        }
        return (band->y_start <= y_position) ? band : NULL;
    }
    return NULL;
}

static int32_t region_cursor_next_y(const db_render_ir_store_t *store,
                                    const region_band_cursor_t *cursor,
                                    int32_t y_position) {
    if (cursor->index >= cursor->region.band_count) {
        return INT32_MAX;
    }
    const db_render_ir_band_t band =
        store->bands[cursor->region.first_band + cursor->index];
    return (band.y_start > y_position) ? band.y_start : band.y_end;
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
    uint32_t rhs_index = 0U;
    for (uint32_t lhs_index = 0U; lhs_index < lhs->span_count; lhs_index++) {
        const db_render_ir_span_t left =
            store->spans[lhs->first_span + lhs_index];
        int32_t cursor = left.x_start;
        if (rhs != NULL) {
            while (
                (rhs_index < rhs->span_count) &&
                (store->spans[rhs->first_span + rhs_index].x_end <= cursor)) {
                rhs_index++;
            }
            uint32_t scan = rhs_index;
            while (scan < rhs->span_count) {
                const db_render_ir_span_t right =
                    store->spans[rhs->first_span + scan];
                if (right.x_start >= left.x_end) {
                    break;
                }
                if (!append_span(store, first_output_span, cursor,
                                 DB_MIN(right.x_start, left.x_end))) {
                    return 0;
                }
                cursor = DB_MAX(cursor, right.x_end);
                if (right.x_end <= left.x_end) {
                    scan++;
                }
                if (cursor >= left.x_end) {
                    break;
                }
            }
            rhs_index = scan;
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
    if ((store == NULL) || (store->regions == NULL) || (store->bands == NULL) ||
        (store->spans == NULL) || (region_id == NULL) ||
        (lhs_id >= store->region_count) || (rhs_id >= store->region_count) ||
        (store->region_count >= store->region_capacity)) {
        return DB_RENDER_IR_INVALID;
    }
    const db_render_ir_view_t view = db_render_ir_store_view(store);
    size_t lhs_span_count = 0U;
    size_t rhs_span_count = 0U;
    if ((db_render_ir_region_validate(&view, lhs_id, &lhs_span_count) == 0) ||
        (db_render_ir_region_validate(&view, rhs_id, &rhs_span_count) == 0)) {
        return DB_RENDER_IR_INVALID;
    }
    if ((store->region_count >= UINT32_MAX) ||
        (store->band_count > UINT32_MAX) || (store->span_count > UINT32_MAX)) {
        return DB_RENDER_IR_CAPACITY;
    }
    const db_render_ir_region_t lhs = store->regions[lhs_id];
    const db_render_ir_region_t rhs = store->regions[rhs_id];
    const size_t initial_band_count = store->band_count;
    const size_t initial_span_count = store->span_count;
    const uint32_t first_band = (uint32_t)initial_band_count;
    uint32_t band_count = 0U;
    region_band_cursor_t lhs_cursor = {.region = lhs};
    region_band_cursor_t rhs_cursor = {.region = rhs};
    int32_t y_position = INT32_MAX;
    if (lhs.band_count > 0U) {
        y_position = store->bands[lhs.first_band].y_start;
    }
    if (rhs.band_count > 0U) {
        y_position = DB_MIN(y_position, store->bands[rhs.first_band].y_start);
    }
    while (y_position != INT32_MAX) {
        const db_render_ir_band_t *const lhs_band =
            region_cursor_band(store, &lhs_cursor, y_position);
        const db_render_ir_band_t *const rhs_band =
            region_cursor_band(store, &rhs_cursor, y_position);
        const int32_t end_y =
            DB_MIN(region_cursor_next_y(store, &lhs_cursor, y_position),
                   region_cursor_next_y(store, &rhs_cursor, y_position));
        if (end_y == INT32_MAX) {
            break;
        }
        const size_t first_span = store->span_count;
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
            store->band_count = initial_band_count;
            store->span_count = initial_span_count;
            return store->status;
        }
        const size_t span_count = store->span_count - first_span;
        if (span_count > 0U) {
            if ((first_span > UINT32_MAX) || (span_count > UINT32_MAX)) {
                store->status = DB_RENDER_IR_CAPACITY;
                store->band_count = initial_band_count;
                store->span_count = initial_span_count;
                return store->status;
            }
            if (store->band_count >= store->band_capacity) {
                store->status = DB_RENDER_IR_CAPACITY;
                store->band_count = initial_band_count;
                store->span_count = initial_span_count;
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
    size_t ignored_span_count = 0U;
    if (db_render_ir_region_validate(view, region_id, &ignored_span_count) ==
        0) {
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
            uint64_t span_area = 0U;
            if ((db_try_mul_u64(width, height, &span_area) == 0) ||
                (db_try_add_u64(area, span_area, &area) == 0)) {
                return 0U;
            }
        }
    }
    return area;
}

int db_render_ir_regions_equal(const db_render_ir_view_t *view,
                               db_render_ir_region_id_t lhs,
                               db_render_ir_region_id_t rhs) {
    size_t lhs_span_count = 0U;
    size_t rhs_span_count = 0U;
    if ((db_render_ir_region_validate(view, lhs, &lhs_span_count) == 0) ||
        (db_render_ir_region_validate(view, rhs, &rhs_span_count) == 0) ||
        (lhs_span_count != rhs_span_count)) {
        return 0;
    }
    const db_render_ir_region_t left = view->regions[lhs];
    const db_render_ir_region_t right = view->regions[rhs];
    if (left.band_count != right.band_count) {
        return 0;
    }
    for (uint32_t band_index = 0U; band_index < left.band_count; band_index++) {
        const db_render_ir_band_t left_band =
            view->bands[left.first_band + band_index];
        const db_render_ir_band_t right_band =
            view->bands[right.first_band + band_index];
        if ((left_band.y_start != right_band.y_start) ||
            (left_band.y_end != right_band.y_end) ||
            (left_band.span_count != right_band.span_count)) {
            return 0;
        }
        for (uint32_t span_index = 0U; span_index < left_band.span_count;
             span_index++) {
            const db_render_ir_span_t left_span =
                view->spans[left_band.first_span + span_index];
            const db_render_ir_span_t right_span =
                view->spans[right_band.first_span + span_index];
            if ((left_span.x_start != right_span.x_start) ||
                (left_span.x_end != right_span.x_end)) {
                return 0;
            }
        }
    }
    return 1;
}
