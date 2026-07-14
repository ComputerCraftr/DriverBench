#include "db_benchmark_emitters.h"

#include "benchmarks/db_benchmark_geometry_internal.h"
#include "benchmarks/db_benchmark_gradient_internal.h"
#include "benchmarks/db_benchmark_runtime_internal.h"
#include "benchmarks/db_benchmark_types_internal.h"
#include "core/db_core.h"
#include "core/db_geometry.h"
#include "core/db_numeric.h"
#include "core/db_render_ir.h"
#include "core/db_render_types.h"

#include <stddef.h>
#include <stdint.h>

void db_benchmark_ir_emitter_reset(db_benchmark_ir_emitter_t *emitter) {
    if (emitter != NULL) {
        emitter->logical_count = 0U;
        emitter->fill_count = 0U;
        emitter->status = DB_BENCHMARK_IR_EMITTER_OK;
    }
}

int db_benchmark_ir_emitter_add_damage(db_benchmark_ir_emitter_t *emitter,
                                       const db_grid_block_t *block) {
    if ((emitter == NULL) || (block == NULL) || (block->row_count == 0U) ||
        (block->col_count == 0U)) {
        if (emitter != NULL) {
            emitter->status = DB_BENCHMARK_IR_EMITTER_INVALID;
        }
        return 0;
    }
    if (emitter->logical_count >= emitter->logical_capacity) {
        emitter->status = DB_BENCHMARK_IR_EMITTER_CAPACITY;
        return 0;
    }
    emitter->logical_blocks[emitter->logical_count++] = *block;
    return 1;
}

int db_benchmark_ir_emitter_add_rect(db_benchmark_ir_emitter_t *emitter,
                                     uint32_t row_start, uint32_t row_count,
                                     uint32_t col_start, uint32_t col_count,
                                     const double rgb[3]) {
    if ((emitter == NULL) || (rgb == NULL) || (row_count == 0U) ||
        (col_count == 0U) || (row_start > INT32_MAX) ||
        (row_count > INT32_MAX) || (col_start > INT32_MAX) ||
        (col_count > INT32_MAX)) {
        if (emitter != NULL) {
            emitter->status = DB_BENCHMARK_IR_EMITTER_INVALID;
        }
        return 0;
    }
    if (emitter->fill_count > 0U) {
        db_render_ir_fill_t *const previous =
            &emitter->fills[emitter->fill_count - 1U];
        if ((previous->rect.x == (int32_t)col_start) &&
            (previous->rect.width == (int32_t)col_count) &&
            ((previous->rect.y + previous->rect.height) ==
             (int32_t)row_start) &&
            (db_equal_f64_rgb3(previous->color.rgba, rgb) != 0)) {
            previous->rect.height += (int32_t)row_count;
            return 1;
        }
    }
    if (emitter->fill_count >= emitter->fill_capacity) {
        emitter->status = DB_BENCHMARK_IR_EMITTER_CAPACITY;
        return 0;
    }
    emitter->fills[emitter->fill_count++] = (db_render_ir_fill_t){
        .rect = {.x = (int32_t)col_start,
                 .y = (int32_t)row_start,
                 .width = (int32_t)col_count,
                 .height = (int32_t)row_count},
        .color = {.rgba = {rgb[0], rgb[1], rgb[2], 1.0}},
    };
    return 1;
}

int db_benchmark_ir_emitter_add_span(db_benchmark_ir_emitter_t *emitter,
                                     uint32_t row, uint32_t col_start,
                                     uint32_t col_end, const double rgb[3]) {
    if (col_end <= col_start) {
        if (emitter != NULL) {
            emitter->status = DB_BENCHMARK_IR_EMITTER_INVALID;
        }
        return 0;
    }
    return db_benchmark_ir_emitter_add_rect(emitter, row, 1U, col_start,
                                            col_end - col_start, rgb);
}

db_benchmark_ir_emitter_status_t
db_benchmark_emit_bands(uint32_t cols, uint32_t rows, uint32_t band_count,
                        uint32_t frame_index,
                        db_benchmark_ir_emitter_t *emitter) {
    db_benchmark_ir_emitter_reset(emitter);
    if ((emitter == NULL) || (cols == 0U) || (rows == 0U) ||
        (band_count == 0U)) {
        return DB_BENCHMARK_IR_EMITTER_INVALID;
    }
    const db_grid_block_t full = db_grid_block_full(rows, cols);
    (void)db_benchmark_ir_emitter_add_damage(emitter, &full);
    for (uint32_t band = 0U; band < band_count; band++) {
        const uint32_t col_start =
            db_checked_u64_to_u32("benchmark_emitters", "band_col_start",
                                  ((uint64_t)band * cols) / band_count);
        const uint32_t col_end =
            db_checked_u64_to_u32("benchmark_emitters", "band_col_end",
                                  ((uint64_t)(band + 1U) * cols) / band_count);
        double rgb[3] = {0};
        db_band_color_rgb3(band, band_count, frame_index, rgb);
        (void)db_benchmark_ir_emitter_add_rect(emitter, 0U, rows, col_start,
                                               col_end - col_start, rgb);
    }
    return emitter->status;
}

db_benchmark_ir_emitter_status_t
db_benchmark_emit_gradient(uint32_t cols, uint32_t rows,
                           const db_gradient_damage_plan_t *plan,
                           int full_frame, db_benchmark_ir_emitter_t *emitter) {
    db_benchmark_ir_emitter_reset(emitter);
    if ((emitter == NULL) || (plan == NULL) || (cols == 0U) || (rows == 0U)) {
        return DB_BENCHMARK_IR_EMITTER_INVALID;
    }
    db_grid_block_t damage[2] = {{0}, {0}};
    const size_t damage_count =
        (full_frame != 0)
            ? (damage[0] = db_grid_block_full(rows, cols), 1U)
            : db_gradient_collect_dirty_blocks(plan, rows, cols, damage, 2U);
    for (size_t damage_index = 0U; damage_index < damage_count;
         damage_index++) {
        (void)db_benchmark_ir_emitter_add_damage(emitter,
                                                 &damage[damage_index]);
        db_gradient_row_segment_iter_t iter = {0};
        db_gradient_row_segment_t segment = {0};
        if (db_gradient_row_segment_iter_init(
                &damage[damage_index], plan->render_state.head_row,
                plan->render_state.direction_down,
                plan->render_state.cycle_index, &iter) == 0) {
            continue;
        }
        while (db_gradient_row_segment_iter_next(&iter, &segment) != 0) {
            (void)db_benchmark_ir_emitter_add_rect(
                emitter, segment.block.row_start, segment.block.row_count,
                segment.block.col_start, segment.block.col_count, segment.rgb);
        }
    }
    return emitter->status;
}

db_benchmark_ir_emitter_status_t
db_benchmark_emit_grid_state_damage(uint32_t cols, uint32_t rows,
                                    db_grid_block_view_t damage,
                                    const double *tile_rgb, size_t tile_count,
                                    db_benchmark_ir_emitter_t *emitter) {
    if ((emitter == NULL) || (tile_rgb == NULL) || (cols == 0U) ||
        (rows == 0U) || ((damage.blocks == NULL) && (damage.count > 0U))) {
        return DB_BENCHMARK_IR_EMITTER_INVALID;
    }
    for (size_t damage_index = 0U; damage_index < damage.count;
         damage_index++) {
        const db_grid_block_t block = damage.blocks[damage_index];
        (void)db_benchmark_ir_emitter_add_damage(emitter, &block);
        const uint32_t row_end = db_checked_u64_to_u32(
            "benchmark_emitters", "grid_damage_row_end",
            DB_MIN((uint64_t)rows,
                   (uint64_t)block.row_start + (uint64_t)block.row_count));
        const uint32_t col_end = db_checked_u64_to_u32(
            "benchmark_emitters", "grid_damage_col_end",
            DB_MIN((uint64_t)cols,
                   (uint64_t)block.col_start + (uint64_t)block.col_count));
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
                (void)db_benchmark_ir_emitter_add_span(emitter, row, run_start,
                                                       run_end, run_rgb);
                run_start = run_end;
            }
        }
    }
    return emitter->status;
}
