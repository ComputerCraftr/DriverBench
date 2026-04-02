#include "renderer_opengl_gl1_5_gles1_1_damage.h"

#include <stddef.h>
#include <stdint.h>

#include "../../core/db_hash.h"
#include "../renderer_benchmark_common.h"
#include "../renderer_snake_common.h"
#include "../renderer_snake_shape_common.h"

int db_gl1_collect_current_snake_frame_blocks(
    const db_gl1_damage_collect_ctx_t *ctx, db_grid_block_t *damage_blocks,
    size_t damage_capacity, size_t *out_damage_count,
    db_snake_compact_block_t *compact_blocks, size_t compact_capacity,
    size_t *out_compact_count, db_gl1_snake_frame_mode_t *out_frame_mode) {
    if ((out_damage_count == NULL) || (out_compact_count == NULL) ||
        (out_frame_mode == NULL) || (ctx == NULL) ||
        (ctx->snake_plan == NULL) || (ctx->cols == 0U) || (ctx->rows == 0U)) {
        return 0;
    }
    *out_damage_count = 0U;
    *out_compact_count = 0U;
    *out_frame_mode = DB_GL1_SNAKE_FRAME_MODE_COMPACT;

    if ((damage_blocks == NULL) || (damage_capacity == 0U)) {
        return 0;
    }
    if (ctx->force_full_upload != 0) {
        damage_blocks[0] = db_grid_block_full(ctx->rows, ctx->cols);
        *out_damage_count = 1U;
        *out_frame_mode = DB_GL1_SNAKE_FRAME_MODE_FULL_RECOVERY_REQUIRED;
        return 1;
    }

    const int is_grid = (ctx->pattern == DB_PATTERN_SNAKE_GRID);
    const db_snake_plan_t *const plan = ctx->snake_plan;
    const db_snake_region_t region =
        (is_grid != 0)
            ? (db_snake_region_t){
                  .x = 0U,
                  .y = 0U,
                  .width = ctx->cols,
                  .height = ctx->rows,
                  .color_rgb = {0.0, 0.0, 0.0},
              }
            : db_snake_region_from_index(ctx->pattern_seed,
                                         plan->active_shape_index);
    if ((region.width == 0U) || (region.height == 0U) ||
        (ctx->snake_scratch == NULL)) {
        return 0;
    }

    db_snake_shape_cache_t shape_cache = {0};
    const db_snake_shape_cache_t *shape_cache_ptr = NULL;
    if ((ctx->pattern == DB_PATTERN_SNAKE_SHAPES) &&
        (ctx->snake_scratch->shape.row_bounds != NULL)) {
        const db_snake_shape_kind_t shape_kind =
            db_snake_shapes_kind_from_index(ctx->pattern_seed,
                                            plan->active_shape_index,
                                            DB_U32_SALT_PALETTE);
        if (db_snake_shape_cache_init_from_index(
                &shape_cache, ctx->snake_scratch->shape.row_bounds,
                ctx->snake_scratch->shape.row_bounds_capacity,
                ctx->pattern_seed, plan->active_shape_index,
                DB_U32_SALT_PALETTE, &region, shape_kind) != 0) {
            shape_cache_ptr = &shape_cache;
        }
    }

    if (db_snake_collect_blocks_for_plan(
            &region, plan, shape_cache_ptr, ctx->cols, ctx->rows,
            ctx->get_color_bits, ctx->color_user_data, damage_blocks,
            damage_capacity, out_damage_count, compact_blocks, compact_capacity,
            out_compact_count) == 0) {
        return 0;
    }
    if ((compact_blocks != NULL) && (compact_capacity > 0U) &&
        (ctx->get_color_bits != NULL) && (*out_compact_count == 0U) &&
        (*out_damage_count > 0U)) {
        *out_frame_mode = DB_GL1_SNAKE_FRAME_MODE_FULL_RECOVERY_REQUIRED;
    }
    return 1;
}
