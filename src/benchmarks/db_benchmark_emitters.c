#include "db_benchmark_emitters.h"

#include <stddef.h>
#include <stdint.h>

#include "benchmarks/db_benchmark_geometry_internal.h"
#include "benchmarks/db_benchmark_gradient_internal.h"
#include "benchmarks/db_benchmark_runtime_internal.h"
#include "benchmarks/db_benchmark_types_internal.h"
#include "core/db_core.h"
#include "core/db_geometry.h"
#include "core/db_geometry_builder.h"
#include "core/db_numeric.h"
#include "core/db_render_types.h"

void db_block_emitter_sink_reset(db_block_emitter_sink_t *sink) {
    db_geometry_builder_reset(sink);
}

db_block_emitter_status_t
db_benchmark_emit_bands(uint32_t cols, uint32_t rows, uint32_t band_count,
                        uint32_t frame_index, db_block_emitter_sink_t *sink) {
    db_block_emitter_sink_reset(sink);
    if ((sink == NULL) || (cols == 0U) || (rows == 0U) || (band_count == 0U)) {
        return DB_BLOCK_EMITTER_STATUS_INVALID;
    }
    const db_grid_block_t full = db_grid_block_full(rows, cols);
    (void)db_geometry_builder_add_damage(sink, &full);
    for (uint32_t band = 0U; band < band_count; band++) {
        const uint32_t col_start =
            db_checked_u64_to_u32("benchmark_emitters", "band_col_start",
                                  ((uint64_t)band * cols) / band_count);
        const uint32_t col_end =
            db_checked_u64_to_u32("benchmark_emitters", "band_col_end",
                                  ((uint64_t)(band + 1U) * cols) / band_count);
        db_colored_f64_block_t block = {
            .row_start = 0U,
            .row_count = rows,
            .col_start = col_start,
            .col_count = col_end - col_start,
            .rgb = {0.0, 0.0, 0.0},
        };
        db_band_color_rgb3(band, band_count, frame_index, block.rgb);
        (void)db_geometry_builder_add_block(sink, &block);
    }
    return sink->status;
}

db_block_emitter_status_t
db_benchmark_emit_gradient(uint32_t cols, uint32_t rows,
                           const db_gradient_damage_plan_t *plan,
                           int full_frame, db_block_emitter_sink_t *sink) {
    db_block_emitter_sink_reset(sink);
    if ((sink == NULL) || (plan == NULL) || (cols == 0U) || (rows == 0U)) {
        return DB_BLOCK_EMITTER_STATUS_INVALID;
    }
    db_grid_block_t damage[2] = {{0}, {0}};
    size_t damage_count = 0U;
    if (full_frame != 0) {
        damage[0] = db_grid_block_full(rows, cols);
        damage_count = 1U;
    } else {
        damage_count = db_gradient_collect_dirty_blocks(
            plan, rows, cols, damage, sizeof(damage) / sizeof(damage[0]));
    }
    for (size_t damage_index = 0U; damage_index < damage_count;
         damage_index++) {
        (void)db_geometry_builder_add_damage(sink, &damage[damage_index]);
        db_gradient_row_segment_iter_t iter = {0};
        db_gradient_row_segment_t segment = {0};
        if (db_gradient_row_segment_iter_init(
                &damage[damage_index], plan->render_state.head_row,
                plan->render_state.direction_down,
                plan->render_state.cycle_index, &iter) == 0) {
            continue;
        }
        while (db_gradient_row_segment_iter_next(&iter, &segment) != 0) {
            const db_colored_f64_block_t block = {
                .row_start = segment.block.row_start,
                .row_count = segment.block.row_count,
                .col_start = segment.block.col_start,
                .col_count = segment.block.col_count,
                .rgb = {segment.rgb[0], segment.rgb[1], segment.rgb[2]},
            };
            (void)db_geometry_builder_add_block(sink, &block);
        }
    }
    return sink->status;
}

db_block_emitter_status_t db_benchmark_emit_grid_state_damage(
    uint32_t cols, uint32_t rows, db_grid_block_view_t damage,
    const double *tile_rgb, size_t tile_count, db_block_emitter_sink_t *sink) {
    if ((sink == NULL) || (tile_rgb == NULL) || (cols == 0U) || (rows == 0U) ||
        ((damage.blocks == NULL) && (damage.count > 0U))) {
        return DB_BLOCK_EMITTER_STATUS_INVALID;
    }
    for (size_t damage_index = 0U; damage_index < damage.count;
         damage_index++) {
        const db_grid_block_t block = damage.blocks[damage_index];
        (void)db_geometry_builder_add_damage(sink, &block);
        const uint32_t row_end =
            DB_MIN(rows, block.row_start + block.row_count);
        const uint32_t col_end =
            DB_MIN(cols, block.col_start + block.col_count);
        for (uint32_t row = block.row_start; row < row_end; row++) {
            uint32_t run_start = block.col_start;
            while (run_start < col_end) {
                const size_t run_index = ((size_t)row * cols) + run_start;
                if (run_index >= tile_count) {
                    break;
                }
                uint32_t run_end = run_start + 1U;
                const double *const run_rgb = &tile_rgb[run_index * 3U];
                while (run_end < col_end) {
                    const size_t next_index = ((size_t)row * cols) + run_end;
                    if ((next_index >= tile_count) ||
                        (db_equal_f64_rgb3(run_rgb,
                                           &tile_rgb[next_index * 3U]) == 0)) {
                        break;
                    }
                    run_end++;
                }
                (void)db_geometry_builder_add_span(sink, row, run_start,
                                                   run_end, run_rgb);
                run_start = run_end;
            }
        }
    }
    return sink->status;
}
