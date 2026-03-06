#include "renderer_opengl_gl1_5_gles1_1_damage.h"

#include <stddef.h>
#include <stdint.h>

#include "../../config/benchmark_config.h"
#include "../renderer_gl_common.h"

size_t
db_gl1_collect_pattern_damage_ranges(const db_gl1_damage_collect_ctx_t *ctx,
                                     db_gl_upload_range_t *range_storage,
                                     size_t range_capacity) {
    if (ctx == NULL) {
        return 0U;
    }
    db_gl_upload_range_t upload_ranges[BENCH_SNAKE_PHASE_WINDOW_TILES] = {
        {0U, 0U, 0U}};
    db_gl_pattern_upload_collect_t collect_ctx = {
        .pattern = ctx->pattern,
        .cols = ctx->cols,
        .rows = ctx->rows,
        .upload_bytes = ctx->upload_bytes,
        .upload_tile_bytes = ctx->upload_tile_bytes,
        .force_full_upload = ctx->force_full_upload,
        .snake_plan = ctx->snake_plan,
        .snake_prev_start =
            (ctx->snake_plan != NULL) ? ctx->snake_plan->prev_start : 0U,
        .snake_prev_count =
            (ctx->snake_plan != NULL) ? ctx->snake_plan->prev_count : 0U,
        .pattern_seed = ctx->pattern_seed,
        .snake_scratch = ctx->snake_scratch,
        .damage_row_ranges = ctx->damage_row_ranges,
        .damage_row_count = ctx->damage_row_count,
    };
    db_gl_upload_range_t *local_range_storage = upload_ranges;
    size_t local_range_capacity = BENCH_SNAKE_PHASE_WINDOW_TILES;
    if (ctx->is_snake_history_texture_pattern != 0) {
        local_range_storage = ctx->default_history_range_storage;
        local_range_capacity = (ctx->snake_scratch != NULL)
                                   ? ctx->snake_scratch->span_capacity
                                   : 0U;
    } else if (ctx->is_gradient_pattern != 0) {
        local_range_capacity = ctx->gradient_dirty_range_cap;
    } else {
        local_range_capacity = 1U;
    }
    if ((range_storage != NULL) && (range_capacity > 0U)) {
        local_range_storage = range_storage;
        local_range_capacity = range_capacity;
    }
    return db_gl_collect_pattern_upload_ranges(
        &collect_ctx, local_range_storage, local_range_capacity);
}
