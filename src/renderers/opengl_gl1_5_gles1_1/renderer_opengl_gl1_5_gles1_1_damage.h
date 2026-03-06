#ifndef DRIVERBENCH_RENDERER_OPENGL_GL1_5_GLES1_1_DAMAGE_H
#define DRIVERBENCH_RENDERER_OPENGL_GL1_5_GLES1_1_DAMAGE_H

#include <stddef.h>
#include <stdint.h>

#include "../renderer_gl_common.h"
#include "../renderer_snake_common.h"

typedef struct {
    const char *backend_name;
    uint32_t pattern;
    uint32_t cols;
    uint32_t rows;
    size_t upload_bytes;
    size_t upload_tile_bytes;
    int force_full_upload;
    const db_snake_plan_t *snake_plan;
    uint32_t pattern_seed;
    const db_history_snake_scratch_t *snake_scratch;
    const db_dirty_row_range_t *damage_row_ranges;
    size_t damage_row_count;
    db_gl_upload_range_t *default_history_range_storage;
    size_t gradient_dirty_range_cap;
    int is_gradient_pattern;
    int is_snake_history_texture_pattern;
} db_gl1_damage_collect_ctx_t;

size_t
db_gl1_collect_pattern_damage_ranges(const db_gl1_damage_collect_ctx_t *ctx,
                                     db_gl_upload_range_t *range_storage,
                                     size_t range_capacity);

#endif
