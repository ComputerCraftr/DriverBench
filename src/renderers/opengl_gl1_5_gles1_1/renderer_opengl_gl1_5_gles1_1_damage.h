#ifndef DRIVERBENCH_RENDERER_OPENGL_GL1_5_GLES1_1_DAMAGE_H
#define DRIVERBENCH_RENDERER_OPENGL_GL1_5_GLES1_1_DAMAGE_H

#include <stddef.h>
#include <stdint.h>

#include "../renderer_gl_common.h"
#include "../renderer_snake_common.h"

typedef struct {
    uint32_t pattern;
    uint32_t cols;
    uint32_t rows;
    int force_full_upload;
    const db_snake_plan_t *snake_plan;
    uint32_t pattern_seed;
    const db_history_snake_scratch_t *snake_scratch;
    db_snake_get_color_bits_cb_t get_color_bits;
    void *color_user_data;
} db_gl1_damage_collect_ctx_t;

typedef enum {
    DB_GL1_SNAKE_FRAME_MODE_COMPACT = 0,
    DB_GL1_SNAKE_FRAME_MODE_FULL_RECOVERY_REQUIRED = 1,
} db_gl1_snake_frame_mode_t;

int db_gl1_collect_current_snake_frame_blocks(
    const db_gl1_damage_collect_ctx_t *ctx, db_grid_block_t *damage_blocks,
    size_t damage_capacity, size_t *out_damage_count,
    db_snake_compact_block_t *compact_blocks, size_t compact_capacity,
    size_t *out_compact_count, db_gl1_snake_frame_mode_t *out_frame_mode);

#endif
