#include "damage_trace_region.h"

#include "core/db_core.h"
#include "core/db_geometry.h"
#include "core/db_hash.h"
#include "core/db_log.h"
#include "core/db_numeric.h"
#include "core/db_sort.h"
#include "damage_trace.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct {
    uint32_t start;
    uint32_t end;
} damage_interval_t;

static int compare_intervals(const void *lhs_pointer, const void *rhs_pointer) {
    const damage_interval_t *const lhs = (const damage_interval_t *)lhs_pointer;
    const damage_interval_t *const rhs = (const damage_interval_t *)rhs_pointer;
    if (lhs->start != rhs->start) {
        return (lhs->start > rhs->start) - (lhs->start < rhs->start);
    }
    return (lhs->end > rhs->end) - (lhs->end < rhs->end);
}

static uint64_t hash_u32_le(uint64_t hash, uint32_t value) {
    const uint8_t encoded[4] = {
        (uint8_t)(value & UINT32_C(0xff)),
        (uint8_t)((value >> 8U) & UINT32_C(0xff)),
        (uint8_t)((value >> 16U) & UINT32_C(0xff)),
        (uint8_t)((value >> 24U) & UINT32_C(0xff)),
    };
    return db_fnv1a64_extend(hash, encoded, sizeof(encoded));
}

static uint64_t hash_band(uint64_t hash, uint32_t y_start, uint32_t y_end,
                          const damage_interval_t *spans, size_t span_count) {
    hash = hash_u32_le(hash, y_start);
    hash = hash_u32_le(hash, y_end);
    hash = hash_u32_le(
        hash, db_checked_size_to_u32("damage_trace", "span_count", span_count));
    for (size_t index = 0U; index < span_count; index++) {
        hash = hash_u32_le(hash, spans[index].start);
        hash = hash_u32_le(hash, spans[index].end);
    }
    return hash;
}

static int spans_equal(const damage_interval_t *lhs,
                       const damage_interval_t *rhs, size_t count) {
    for (size_t index = 0U; index < count; index++) {
        if ((lhs[index].start != rhs[index].start) ||
            (lhs[index].end != rhs[index].end)) {
            return 0;
        }
    }
    return 1;
}

static int damage_trace_comparison_available(uint64_t used) {
    return DB_BOOL(used < DB_DAMAGE_TRACE_REGION_COMPARISON_BUDGET);
}

static int merge_intervals(damage_interval_t *intervals, size_t count,
                           db_damage_trace_summary_t *summary,
                           size_t *merged_count_out) {
    size_t merged_count = 0U;
    for (size_t index = 0U; index < count; index++) {
        const damage_interval_t next = intervals[index];
        if (merged_count > 0U) {
            const uint64_t used = summary->band_block_comparisons +
                                  summary->interval_sort_comparisons +
                                  summary->interval_merge_comparisons;
            if (damage_trace_comparison_available(used) == 0) {
                return 0;
            }
            summary->interval_merge_comparisons++;
        }
        if ((merged_count > 0U) &&
            (next.start <= intervals[merged_count - 1U].end)) {
            intervals[merged_count - 1U].end =
                DB_MAX(intervals[merged_count - 1U].end, next.end);
            continue;
        }
        intervals[merged_count++] = next;
    }
    *merged_count_out = merged_count;
    return 1;
}

static void damage_trace_free_workspaces(uint32_t *y_edges,
                                         damage_interval_t *intervals,
                                         damage_interval_t *sort_scratch,
                                         damage_interval_t *prior_spans) {
    free(prior_spans);
    free(sort_scratch);
    free(intervals);
    free(y_edges);
}

db_damage_trace_summary_t
db_damage_trace_summarize_regions(const db_damage_trace_event_t *event) {
    db_damage_trace_summary_t summary = {0};
    if ((event == NULL) || (event->blocks == NULL) ||
        (event->block_count == 0U)) {
        return summary;
    }
    uint64_t maximum_y_intervals = 0U;
    uint64_t maximum_band_block_comparisons = 0U;
    if ((db_try_mul_u64((uint64_t)event->block_count, 2U,
                        &maximum_y_intervals) == 0) ||
        (maximum_y_intervals == 0U) ||
        (db_try_mul_u64(maximum_y_intervals - 1U, (uint64_t)event->block_count,
                        &maximum_band_block_comparisons) == 0) ||
        (maximum_band_block_comparisons >
         DB_DAMAGE_TRACE_REGION_COMPARISON_BUDGET)) {
        summary.rejected_block_count = event->block_count;
        summary.truncated = 1;
        return summary;
    }
    size_t edge_capacity = 0U;
    size_t edge_bytes = 0U;
    size_t interval_bytes = 0U;
    if ((db_try_mul_size(event->block_count, 2U, &edge_capacity) == 0) ||
        (db_try_mul_size(edge_capacity, sizeof(uint32_t), &edge_bytes) == 0) ||
        (db_try_mul_size(event->block_count, sizeof(damage_interval_t),
                         &interval_bytes) == 0)) {
        summary.rejected_block_count = event->block_count;
        summary.truncated = 1;
        return summary;
    }
    uint32_t *const y_edges = (uint32_t *)malloc(edge_bytes);
    damage_interval_t *const intervals =
        (damage_interval_t *)malloc(interval_bytes);
    damage_interval_t *const sort_scratch =
        (damage_interval_t *)malloc(interval_bytes);
    damage_interval_t *const prior_spans =
        (damage_interval_t *)malloc(interval_bytes);
    if ((y_edges == NULL) || (intervals == NULL) || (sort_scratch == NULL) ||
        (prior_spans == NULL)) {
        damage_trace_free_workspaces(y_edges, intervals, sort_scratch,
                                     prior_spans);
        summary.rejected_block_count = event->block_count;
        summary.truncated = 1;
        return summary;
    }

    uint64_t summed_units = 0U;
    uint32_t min_x = UINT32_MAX;
    uint32_t min_y = UINT32_MAX;
    uint32_t max_x = 0U;
    uint32_t max_y = 0U;
    size_t edge_count = 0U;
    for (size_t index = 0U; index < event->block_count; index++) {
        const db_damage_block_t block = event->blocks[index];
        const uint64_t x_end =
            (uint64_t)block.col_start + (uint64_t)block.col_count;
        const uint64_t y_end =
            (uint64_t)block.row_start + (uint64_t)block.row_count;
        if ((block.col_count == 0U) || (block.row_count == 0U) ||
            (x_end > event->width) || (y_end > event->height)) {
            summary.rejected_block_count++;
            continue;
        }
        y_edges[edge_count++] = block.row_start;
        y_edges[edge_count++] = (uint32_t)y_end;
        min_x = DB_MIN(min_x, block.col_start);
        min_y = DB_MIN(min_y, block.row_start);
        max_x = DB_MAX(max_x, (uint32_t)x_end);
        max_y = DB_MAX(max_y, (uint32_t)y_end);
        const uint64_t block_units =
            (uint64_t)block.col_count * (uint64_t)block.row_count;
        if (db_try_add_u64(summed_units, block_units, &summed_units) == 0) {
            summary.truncated = 1;
        }
        summary.valid_block_count++;
    }
    if (summary.valid_block_count == 0U) {
        damage_trace_free_workspaces(y_edges, intervals, sort_scratch,
                                     prior_spans);
        return summary;
    }
    if (db_sort_u32_ascending(y_edges, edge_count) != DB_SORT_OK) {
        DB_RUNTIME_FAIL("damage_trace", "failed to sort region edges");
    }
    size_t unique_edge_count = 0U;
    for (size_t index = 0U; index < edge_count; index++) {
        if ((unique_edge_count == 0U) ||
            (y_edges[index] != y_edges[unique_edge_count - 1U])) {
            y_edges[unique_edge_count++] = y_edges[index];
        }
    }

    uint64_t hash = DB_FNV1A64_OFFSET;
    hash = hash_u32_le(hash, DB_U32_SALT_PALETTE);
    hash = hash_u32_le(hash, event->width);
    hash = hash_u32_le(hash, event->height);
    size_t prior_span_count = 0U;
    uint32_t prior_y_start = 0U;
    uint32_t prior_y_end = 0U;
    const size_t y_interval_count = unique_edge_count - 1U;
    uint64_t band_block_comparisons = 0U;
    if ((db_try_mul_u64((uint64_t)y_interval_count,
                        (uint64_t)event->block_count,
                        &band_block_comparisons) == 0) ||
        (band_block_comparisons > DB_DAMAGE_TRACE_REGION_COMPARISON_BUDGET)) {
        summary.truncated = 1;
        damage_trace_free_workspaces(y_edges, intervals, sort_scratch,
                                     prior_spans);
        return summary;
    }
    for (size_t edge = 0U; edge + 1U < unique_edge_count; edge++) {
        const uint32_t y_start = y_edges[edge];
        const uint32_t y_end = y_edges[edge + 1U];
        size_t interval_count = 0U;
        for (size_t index = 0U; index < event->block_count; index++) {
            summary.band_block_comparisons++;
            const db_damage_block_t block = event->blocks[index];
            const uint64_t block_x_end =
                (uint64_t)block.col_start + block.col_count;
            const uint64_t block_y_end =
                (uint64_t)block.row_start + block.row_count;
            if ((block.col_count == 0U) || (block.row_count == 0U) ||
                (block_x_end > event->width) || (block_y_end > event->height) ||
                (block.row_start > y_start) || (block_y_end < y_end)) {
                continue;
            }
            intervals[interval_count++] = (damage_interval_t){
                .start = block.col_start, .end = (uint32_t)block_x_end};
        }
        if (interval_count == 0U) {
            continue;
        }
        const uint64_t non_sort_comparisons =
            summary.band_block_comparisons + summary.interval_merge_comparisons;
        const uint64_t sort_budget =
            DB_DAMAGE_TRACE_REGION_COMPARISON_BUDGET - non_sort_comparisons;
        if (db_sort_records_stable(
                intervals, sort_scratch, interval_count, sizeof(*intervals),
                compare_intervals, sort_budget,
                &summary.interval_sort_comparisons) != DB_SORT_OK) {
            summary.truncated = 1;
            damage_trace_free_workspaces(y_edges, intervals, sort_scratch,
                                         prior_spans);
            return summary;
        }
        size_t merged_count = 0U;
        if (merge_intervals(intervals, interval_count, &summary,
                            &merged_count) == 0) {
            summary.truncated = 1;
            damage_trace_free_workspaces(y_edges, intervals, sort_scratch,
                                         prior_spans);
            return summary;
        }
        interval_count = merged_count;
        uint64_t row_units = 0U;
        for (size_t index = 0U; index < interval_count; index++) {
            row_units += intervals[index].end - intervals[index].start;
        }
        summary.covered_units += row_units * (y_end - y_start);
        if ((prior_span_count == interval_count) && (prior_y_end == y_start) &&
            (spans_equal(prior_spans, intervals, interval_count) != 0)) {
            prior_y_end = y_end;
            continue;
        }
        if (prior_span_count > 0U) {
            hash = hash_band(hash, prior_y_start, prior_y_end, prior_spans,
                             prior_span_count);
        }
        for (size_t index = 0U; index < interval_count; index++) {
            prior_spans[index] = intervals[index];
        }
        prior_span_count = interval_count;
        prior_y_start = y_start;
        prior_y_end = y_end;
    }
    if (prior_span_count > 0U) {
        hash = hash_band(hash, prior_y_start, prior_y_end, prior_spans,
                         prior_span_count);
    }
    summary.union_hash = hash;
    if (summary.truncated == 0) {
        summary.duplicate_units = summed_units - summary.covered_units;
    }
    summary.bounds = (db_damage_block_t){
        .row_start = min_y,
        .row_count = max_y - min_y,
        .col_start = min_x,
        .col_count = max_x - min_x,
    };
    damage_trace_free_workspaces(y_edges, intervals, sort_scratch, prior_spans);
    return summary;
}
