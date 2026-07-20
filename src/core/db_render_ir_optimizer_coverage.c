#include "db_render_ir_optimizer_internal.h"

#include "db_core.h"
#include "db_numeric.h"
#include "db_render_ir.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    db_render_ir_band_t *bands;
    size_t band_count;
    db_render_ir_span_t *spans;
    size_t span_count;
} coverage_region_t;

typedef struct {
    db_render_ir_optimizer_stats_t *stats;
    uint64_t comparison_budget;
    size_t region_capacity;
} coverage_limits_t;

static int coverage_limits_init(db_render_ir_optimizer_workspace_t workspace,
                                size_t input_count, size_t region_capacity,
                                coverage_limits_t *limits) {
    if ((limits == NULL) || (workspace.stats == NULL) ||
        (region_capacity == 0U)) {
        return 0;
    }
    uint64_t operation_budget = 0U;
    uint64_t comparison_count = 0U;
    uint64_t absolute_budget = 0U;
    if ((db_try_mul_u64((uint64_t)input_count, (uint64_t)region_capacity,
                        &operation_budget) == 0) ||
        (db_try_mul_u64(operation_budget, UINT64_C(8), &operation_budget) ==
         0) ||
        (db_try_add_u64(workspace.stats->band_comparisons,
                        workspace.stats->span_comparisons,
                        &comparison_count) == 0) ||
        (db_try_add_u64(comparison_count, operation_budget, &absolute_budget) ==
         0)) {
        return 0;
    }
    *limits = (coverage_limits_t){
        .stats = workspace.stats,
        .comparison_budget = absolute_budget,
        .region_capacity = region_capacity,
    };
    return 1;
}

static int count_band_comparison(const coverage_limits_t *limits) {
    if (limits->stats == NULL) {
        return 1;
    }
    if ((limits->stats->span_comparisons > limits->comparison_budget) ||
        (limits->stats->band_comparisons >=
         (limits->comparison_budget - limits->stats->span_comparisons))) {
        return 0;
    }
    limits->stats->band_comparisons++;
    return 1;
}

static int count_span_comparison(const coverage_limits_t *limits) {
    if (limits->stats == NULL) {
        return 1;
    }
    if ((limits->stats->band_comparisons > limits->comparison_budget) ||
        (limits->stats->span_comparisons >=
         (limits->comparison_budget - limits->stats->band_comparisons))) {
        return 0;
    }
    limits->stats->span_comparisons++;
    return 1;
}

static int append_span(coverage_region_t *region, size_t first_span,
                       size_t capacity, int32_t x_start, int32_t x_end) {
    if (x_end <= x_start) {
        return 1;
    }
    if (region->span_count > first_span) {
        db_render_ir_span_t *const previous =
            &region->spans[region->span_count - 1U];
        if (x_start <= previous->x_end) {
            previous->x_end = DB_MAX(previous->x_end, x_end);
            return 1;
        }
    }
    if (region->span_count >= capacity) {
        return 0;
    }
    region->spans[region->span_count++] =
        (db_render_ir_span_t){.x_start = x_start, .x_end = x_end};
    return 1;
}

static int band_spans_equal(const coverage_region_t *region,
                            const db_render_ir_band_t *lhs,
                            const db_render_ir_band_t *rhs,
                            const coverage_limits_t *limits) {
    if (lhs->span_count != rhs->span_count) {
        return 0;
    }
    for (uint32_t index = 0U; index < lhs->span_count; index++) {
        if (!count_span_comparison(limits)) {
            return -1;
        }
        const db_render_ir_span_t left = region->spans[lhs->first_span + index];
        const db_render_ir_span_t right =
            region->spans[rhs->first_span + index];
        if ((left.x_start != right.x_start) || (left.x_end != right.x_end)) {
            return 0;
        }
    }
    return 1;
}

static int append_band(coverage_region_t *output,
                       const coverage_region_t *input,
                       const db_render_ir_band_t *input_band, int32_t y_start,
                       int32_t y_end, const db_render_ir_rect_t *added_rect,
                       size_t capacity, const coverage_limits_t *limits) {
    if (y_end <= y_start) {
        return 1;
    }
    const size_t first_span = output->span_count;
    int rect_pending = added_rect != NULL;
    if (input_band != NULL) {
        for (uint32_t index = 0U; index < input_band->span_count; index++) {
            if (!count_span_comparison(limits)) {
                return -1;
            }
            const db_render_ir_span_t span =
                input->spans[input_band->first_span + index];
            if ((rect_pending != 0) && (added_rect->x <= span.x_start)) {
                if (!append_span(output, first_span, capacity, added_rect->x,
                                 added_rect->x + added_rect->width)) {
                    return 0;
                }
                rect_pending = 0;
            }
            if (!append_span(output, first_span, capacity, span.x_start,
                             span.x_end)) {
                return 0;
            }
        }
    }
    if ((rect_pending != 0) &&
        !append_span(output, first_span, capacity, added_rect->x,
                     added_rect->x + added_rect->width)) {
        return 0;
    }
    const size_t span_count = output->span_count - first_span;
    if ((span_count == 0U) || (first_span > UINT32_MAX) ||
        (span_count > UINT32_MAX)) {
        return span_count == 0U;
    }
    const db_render_ir_band_t band = {
        .y_start = y_start,
        .y_end = y_end,
        .first_span = (uint32_t)first_span,
        .span_count = (uint32_t)span_count,
    };
    if ((output->band_count > 0U) &&
        (output->bands[output->band_count - 1U].y_end == y_start)) {
        const int equal = band_spans_equal(
            output, &output->bands[output->band_count - 1U], &band, limits);
        if (equal < 0) {
            return -1;
        }
        if (equal != 0) {
            output->bands[output->band_count - 1U].y_end = y_end;
            output->span_count = first_span;
            return 1;
        }
    }
    if (output->band_count >= capacity) {
        return 0;
    }
    output->bands[output->band_count++] = band;
    return 1;
}

static int union_rect(const coverage_region_t *input, coverage_region_t *output,
                      db_render_ir_rect_t rect, size_t capacity,
                      const coverage_limits_t *limits) {
    output->band_count = 0U;
    output->span_count = 0U;
    const int32_t rect_bottom = rect.y + rect.height;
    size_t band_index = 0U;
    int32_t y_position = input->band_count > 0U
                             ? DB_MIN(input->bands[0].y_start, rect.y)
                             : rect.y;
    for (;;) {
        while ((band_index < input->band_count) &&
               (input->bands[band_index].y_end <= y_position)) {
            band_index++;
        }
        const db_render_ir_band_t *active_band = NULL;
        int32_t next_y = INT32_MAX;
        if (band_index < input->band_count) {
            const db_render_ir_band_t *const candidate =
                &input->bands[band_index];
            if (!count_band_comparison(limits)) {
                return -1;
            }
            if (candidate->y_start <= y_position) {
                active_band = candidate;
                next_y = candidate->y_end;
            } else {
                next_y = candidate->y_start;
            }
        }
        const int rect_active =
            DB_BOOL((rect.y <= y_position) && (rect_bottom > y_position));
        if (rect.y > y_position) {
            next_y = DB_MIN(next_y, rect.y);
        } else if (rect_bottom > y_position) {
            next_y = DB_MIN(next_y, rect_bottom);
        }
        if (next_y == INT32_MAX) {
            break;
        }
        if ((active_band != NULL) || (rect_active != 0)) {
            const int appended =
                append_band(output, input, active_band, y_position, next_y,
                            rect_active != 0 ? &rect : NULL, capacity, limits);
            if (appended <= 0) {
                return appended;
            }
        }
        y_position = next_y;
    }
    return 1;
}

static int append_visible_fill(db_render_ir_fill_t *output, size_t capacity,
                               size_t *output_count, db_render_ir_fill_t source,
                               int32_t x_start, int32_t x_end, int32_t y_start,
                               int32_t y_end) {
    if ((x_end <= x_start) || (y_end <= y_start)) {
        return 1;
    }
    if (*output_count >= capacity) {
        return 0;
    }
    source.rect = (db_render_ir_rect_t){
        .x = x_start,
        .y = y_start,
        .width = x_end - x_start,
        .height = y_end - y_start,
    };
    output[(*output_count)++] = source;
    return 1;
}

static int subtract_coverage(db_render_ir_fill_t source,
                             const coverage_region_t *coverage,
                             db_render_ir_fill_t *output, size_t capacity,
                             size_t *output_count,
                             const coverage_limits_t *limits) {
    const int32_t source_right = source.rect.x + source.rect.width;
    const int32_t source_bottom = source.rect.y + source.rect.height;
    const size_t first_piece = *output_count;
    size_t band_index = 0U;
    int32_t y_position = source.rect.y;
    while (y_position < source_bottom) {
        while ((band_index < coverage->band_count) &&
               (coverage->bands[band_index].y_end <= y_position)) {
            band_index++;
        }
        const db_render_ir_band_t *active_band = NULL;
        int32_t next_y = source_bottom;
        if (band_index < coverage->band_count) {
            const db_render_ir_band_t *const candidate =
                &coverage->bands[band_index];
            if (!count_band_comparison(limits)) {
                return -1;
            }
            if (candidate->y_start <= y_position) {
                active_band = candidate;
                next_y = DB_MIN(next_y, candidate->y_end);
            } else {
                next_y = DB_MIN(next_y, candidate->y_start);
            }
        }
        int32_t cursor = source.rect.x;
        if (active_band != NULL) {
            for (uint32_t span_index = 0U; span_index < active_band->span_count;
                 span_index++) {
                if (!count_span_comparison(limits)) {
                    return -1;
                }
                const db_render_ir_span_t span =
                    coverage->spans[active_band->first_span + span_index];
                if ((span.x_end <= cursor) || (span.x_start >= source_right)) {
                    continue;
                }
                if (!append_visible_fill(output, capacity, output_count, source,
                                         cursor,
                                         DB_MIN(span.x_start, source_right),
                                         y_position, next_y)) {
                    return 0;
                }
                cursor = DB_MAX(cursor, span.x_end);
                if (cursor >= source_right) {
                    break;
                }
            }
        }
        if (!append_visible_fill(output, capacity, output_count, source, cursor,
                                 source_right, y_position, next_y)) {
            return 0;
        }
        y_position = next_y;
    }
    if ((limits->stats != NULL) && (*output_count > (first_piece + 1U))) {
        limits->stats->region_splits += *output_count - first_piece - 1U;
        if (limits->stats->region_splits > limits->region_capacity) {
            return -1;
        }
    }
    return 1;
}

db_render_ir_status_t db_render_ir_eliminate_overwrites(
    const db_render_ir_fill_t *input, size_t input_count,
    db_render_ir_optimizer_workspace_t workspace, size_t region_span_capacity,
    db_render_ir_fill_t *output, size_t *output_count) {
    if ((input == NULL) || (output == NULL) || (output_count == NULL) ||
        (workspace.coverage_bands == NULL) ||
        (workspace.coverage_band_scratch == NULL) ||
        (workspace.coverage_spans == NULL) ||
        (workspace.coverage_span_scratch == NULL)) {
        return DB_RENDER_IR_INVALID;
    }
    coverage_region_t coverage = {
        .bands = workspace.coverage_bands,
        .spans = workspace.coverage_spans,
    };
    coverage_region_t next = {
        .bands = workspace.coverage_band_scratch,
        .spans = workspace.coverage_span_scratch,
    };
    coverage_limits_t limits = {0};
    if (coverage_limits_init(workspace, input_count, region_span_capacity,
                             &limits) == 0) {
        return DB_RENDER_IR_COMPLEXITY_LIMIT;
    }
    size_t visible_count = 0U;
    for (size_t reverse = input_count; reverse > 0U; reverse--) {
        const db_render_ir_fill_t source = input[reverse - 1U];
        const int subtracted =
            subtract_coverage(source, &coverage, output, workspace.capacity,
                              &visible_count, &limits);
        if (subtracted <= 0) {
            return subtracted < 0 ? DB_RENDER_IR_COMPLEXITY_LIMIT
                                  : DB_RENDER_IR_CAPACITY;
        }
        const int united = union_rect(&coverage, &next, source.rect,
                                      workspace.capacity, &limits);
        if (united <= 0) {
            return united < 0 ? DB_RENDER_IR_COMPLEXITY_LIMIT
                              : DB_RENDER_IR_CAPACITY;
        }
        const coverage_region_t prior = coverage;
        coverage = next;
        next = prior;
    }
    if (workspace.stats != NULL) {
        workspace.stats->emitted_spans = visible_count;
        if (workspace.stats->emitted_spans > region_span_capacity) {
            return DB_RENDER_IR_COMPLEXITY_LIMIT;
        }
    }
    *output_count = visible_count;
    return DB_RENDER_IR_OK;
}

db_render_ir_status_t db_render_ir_build_fill_region_bounded(
    const db_render_ir_fill_t *fills, size_t fill_count,
    db_render_ir_optimizer_workspace_t workspace, size_t region_span_capacity,
    db_render_ir_store_t *destination,
    db_render_ir_region_id_t *destination_region) {
    if ((fills == NULL) || (fill_count == 0U) || (destination == NULL) ||
        (destination_region == NULL) || (workspace.coverage_bands == NULL) ||
        (workspace.coverage_band_scratch == NULL) ||
        (workspace.coverage_spans == NULL) ||
        (workspace.coverage_span_scratch == NULL) ||
        (workspace.capacity == 0U) || (region_span_capacity == 0U)) {
        return DB_RENDER_IR_INVALID;
    }
    coverage_limits_t limits = {0};
    if (coverage_limits_init(workspace, fill_count, region_span_capacity,
                             &limits) == 0) {
        return DB_RENDER_IR_COMPLEXITY_LIMIT;
    }
    coverage_region_t coverage = {
        .bands = workspace.coverage_bands,
        .spans = workspace.coverage_spans,
    };
    coverage_region_t next = {
        .bands = workspace.coverage_band_scratch,
        .spans = workspace.coverage_span_scratch,
    };
    for (size_t index = 0U; index < fill_count; index++) {
        if (db_render_ir_rect_is_empty(fills[index].rect) != 0) {
            return DB_RENDER_IR_INVALID;
        }
        const int united = union_rect(&coverage, &next, fills[index].rect,
                                      workspace.capacity, &limits);
        if (united <= 0) {
            return united < 0 ? DB_RENDER_IR_COMPLEXITY_LIMIT
                              : DB_RENDER_IR_CAPACITY;
        }
        const coverage_region_t prior = coverage;
        coverage = next;
        next = prior;
    }
    if ((coverage.band_count == 0U) || (coverage.band_count > UINT32_MAX) ||
        (coverage.span_count > UINT32_MAX) ||
        (destination->region_count >= destination->region_capacity) ||
        (destination->band_count > destination->band_capacity) ||
        (destination->span_count > destination->span_capacity) ||
        (coverage.band_count >
         (destination->band_capacity - destination->band_count)) ||
        (coverage.span_count >
         (destination->span_capacity - destination->span_count)) ||
        (destination->region_count >= UINT32_MAX) ||
        (destination->band_count > UINT32_MAX) ||
        (destination->span_count > UINT32_MAX)) {
        return DB_RENDER_IR_CAPACITY;
    }
    const uint32_t first_band = (uint32_t)destination->band_count;
    const uint32_t first_span = (uint32_t)destination->span_count;
    for (size_t index = 0U; index < coverage.band_count; index++) {
        uint32_t adjusted_first_span = 0U;
        if (db_try_add_u32(first_span, coverage.bands[index].first_span,
                           &adjusted_first_span) == 0) {
            return DB_RENDER_IR_ARITHMETIC_OVERFLOW;
        }
        coverage.bands[index].first_span = adjusted_first_span;
    }
    for (size_t index = 0U; index < coverage.span_count; index++) {
        destination->spans[destination->span_count + index] =
            coverage.spans[index];
    }
    for (size_t index = 0U; index < coverage.band_count; index++) {
        destination->bands[destination->band_count + index] =
            coverage.bands[index];
    }
    *destination_region = (uint32_t)destination->region_count;
    destination->regions[destination->region_count] = (db_render_ir_region_t){
        .first_band = first_band, .band_count = (uint32_t)coverage.band_count};
    destination->span_count += coverage.span_count;
    destination->band_count += coverage.band_count;
    destination->region_count++;
    return DB_RENDER_IR_OK;
}
