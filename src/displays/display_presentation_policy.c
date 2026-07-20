#include "display_presentation_policy.h"

#include <stdint.h>
#include <string.h>

#include "core/db_core.h"
#include "core/db_geometry.h"
#include "core/db_log.h"
#include "core/db_numeric.h"
#include "core/db_render_ir.h"
#include "core/db_render_types.h"

const char *db_presentation_buffer_age_provider_name(
    db_presentation_buffer_age_provider_t provider) {
    switch (provider) {
    case DB_PRESENTATION_BUFFER_AGE_GLX:
        return "glx";
    case DB_PRESENTATION_BUFFER_AGE_EGL:
        return "egl";
    case DB_PRESENTATION_BUFFER_AGE_SCANOUT:
        return "scanout";
    case DB_PRESENTATION_BUFFER_AGE_UNAVAILABLE:
        return "unavailable";
    }
    return "unavailable";
}

db_presentation_buffer_age_t db_presentation_buffer_age_from_serial(
    uint64_t current_serial, uint64_t previous_serial, int previous_valid,
    uint32_t history_capacity) {
    uint32_t raw_age = 0U;
    if ((previous_valid != 0) && (current_serial > previous_serial)) {
        const uint64_t delta = current_serial - previous_serial;
        raw_age = db_u64_to_u32_saturating(delta);
    }
    return db_presentation_buffer_age_resolve(
        DB_PRESENTATION_BUFFER_AGE_SCANOUT, raw_age, history_capacity);
}

db_presentation_buffer_age_t db_presentation_buffer_age_resolve(
    db_presentation_buffer_age_provider_t provider, uint32_t raw_age,
    uint32_t history_capacity) {
    db_presentation_buffer_age_t result = {
        .provider = provider,
        .raw_age = raw_age,
        .history_capacity = history_capacity,
        .force_full_repair = 1,
    };
    if (provider == DB_PRESENTATION_BUFFER_AGE_UNAVAILABLE) {
        result.fallback_reason = "extension_unavailable";
        return result;
    }
    if (raw_age == 0U) {
        result.fallback_reason = "contents_undefined";
        return result;
    }
    const uint32_t replay_depth = raw_age - 1U;
    if (replay_depth > history_capacity) {
        result.fallback_reason = "history_capacity_exceeded";
        return result;
    }
    result.effective_replay_depth = replay_depth;
    result.valid = 1;
    result.force_full_repair = 0;
    result.fallback_reason = "none";
    return result;
}

void db_presentation_log_buffer_age(const char *backend_name,
                                    const db_presentation_buffer_age_t *age) {
    if ((backend_name == NULL) || (age == NULL)) {
        return;
    }
    const db_log_field_t fields[] = {
        DB_LOG_TOKEN("provider",
                     db_presentation_buffer_age_provider_name(age->provider)),
        DB_LOG_U64("raw_age", age->raw_age),
        DB_LOG_U64("effective_replay_depth", age->effective_replay_depth),
        DB_LOG_U64("history_capacity", age->history_capacity),
        DB_LOG_BOOL("valid", age->valid),
        DB_LOG_BOOL("force_full_repair", age->force_full_repair),
        DB_LOG_TOKEN("fallback_reason", age->fallback_reason),
    };
    db_log_info(backend_name, "presentation_buffer_age", fields,
                DB_LOG_FIELD_COUNT(fields));
}

void db_presentation_damage_history_reset(
    db_presentation_damage_history_t *history) {
    if (history != NULL) {
        *history = (db_presentation_damage_history_t){0};
    }
}

typedef struct {
    db_grid_block_t *blocks;
    size_t capacity;
    size_t count;
} db_presentation_damage_builder_t;

static int
db_presentation_damage_add_one(db_presentation_damage_builder_t *builder,
                               const db_grid_block_t *block) {
    if ((builder == NULL) || (block == NULL) || (block->row_count == 0U) ||
        (block->col_count == 0U)) {
        return 0;
    }
    db_grid_block_t merged = *block;
    size_t index = 0U;
    while (index < builder->count) {
        db_grid_block_t *const existing = &builder->blocks[index];
        const uint64_t block_row_end =
            (uint64_t)merged.row_start + merged.row_count;
        const uint64_t block_col_end =
            (uint64_t)merged.col_start + merged.col_count;
        const uint64_t existing_row_end =
            (uint64_t)existing->row_start + existing->row_count;
        const uint64_t existing_col_end =
            (uint64_t)existing->col_start + existing->col_count;
        if ((block_row_end > UINT32_MAX) || (block_col_end > UINT32_MAX) ||
            (existing_row_end > UINT32_MAX) ||
            (existing_col_end > UINT32_MAX)) {
            return 0;
        }
        const int merge_horizontal =
            DB_BOOL((existing->row_start == merged.row_start) &&
                    (existing->row_count == merged.row_count) &&
                    ((uint64_t)merged.col_start <= existing_col_end) &&
                    ((uint64_t)existing->col_start <= block_col_end));
        const int merge_vertical =
            DB_BOOL((existing->col_start == merged.col_start) &&
                    (existing->col_count == merged.col_count) &&
                    ((uint64_t)merged.row_start <= existing_row_end) &&
                    ((uint64_t)existing->row_start <= block_row_end));
        if (merge_horizontal != 0) {
            const uint32_t start =
                DB_MIN(existing->col_start, merged.col_start);
            const uint64_t end = DB_MAX(existing_col_end, block_col_end);
            merged.col_start = start;
            merged.col_count = (uint32_t)(end - start);
        } else if (merge_vertical != 0) {
            const uint32_t start =
                DB_MIN(existing->row_start, merged.row_start);
            const uint64_t end = DB_MAX(existing_row_end, block_row_end);
            merged.row_start = start;
            merged.row_count = (uint32_t)(end - start);
        } else {
            index++;
            continue;
        }
        const size_t move_count = builder->count - index - 1U;
        const size_t move_bytes =
            db_checked_mul_size("presentation_policy", "damage merge bytes",
                                move_count, sizeof(*builder->blocks));
        memmove(&builder->blocks[index], &builder->blocks[index + 1U],
                move_bytes);
        builder->count--;
        index = 0U;
    }
    if (builder->count >= builder->capacity) {
        return 0;
    }
    builder->blocks[builder->count++] = merged;
    return 1;
}

static int db_presentation_damage_add(db_presentation_damage_builder_t *builder,
                                      db_grid_block_view_t damage) {
    for (size_t index = 0U; index < damage.count; index++) {
        if (db_presentation_damage_add_one(builder, &damage.blocks[index]) ==
            0) {
            return 0;
        }
    }
    return 1;
}

size_t db_presentation_damage_history_resolve(
    db_presentation_damage_history_t *history,
    const db_presentation_buffer_age_t *age, db_grid_block_view_t current,
    uint32_t rows, uint32_t cols, db_grid_block_t *out_blocks,
    size_t out_capacity, int *out_force_full) {
    if (out_force_full != NULL) {
        *out_force_full = 1;
    }
    if ((history == NULL) || (age == NULL) || (out_blocks == NULL) ||
        (out_capacity == 0U) || (rows == 0U) || (cols == 0U)) {
        return 0U;
    }
    db_presentation_damage_builder_t builder = {
        .blocks = out_blocks,
        .capacity = out_capacity,
    };
    int valid = DB_BOOL((history->count > 0U) && (age->valid != 0) &&
                        (age->force_full_repair == 0) &&
                        (age->effective_replay_depth <= history->count));
    if ((valid != 0) && (db_presentation_damage_add(&builder, current) != 0)) {
        for (uint32_t offset = 0U; offset < age->effective_replay_depth;
             offset++) {
            const uint32_t index =
                (history->next_index + DB_PRESENTATION_DAMAGE_HISTORY_LENGTH -
                 1U - offset) %
                DB_PRESENTATION_DAMAGE_HISTORY_LENGTH;
            if ((history->valid[index] == 0) ||
                (db_presentation_damage_add(
                     &builder, (db_grid_block_view_t){
                                   .blocks = history->blocks[index],
                                   .count = history->counts[index]}) == 0)) {
                valid = 0;
                break;
            }
        }
    } else {
        valid = 0;
    }
    if (valid == 0) {
        builder.count = 0U;
        const db_grid_block_t full = db_grid_block_full(rows, cols);
        if (db_presentation_damage_add_one(&builder, &full) == 0) {
            return 0U;
        }
    }

    const uint32_t write_index = history->next_index;
    const size_t stored_count =
        DB_MIN(current.count, (size_t)DB_PRESENTATION_DAMAGE_RECTS_PER_FRAME);
    if ((current.blocks != NULL) && (stored_count > 0U)) {
        const size_t stored_bytes =
            db_checked_mul_size("presentation_policy", "damage history bytes",
                                stored_count, sizeof(*current.blocks));
        memmove(history->blocks[write_index], current.blocks, stored_bytes);
    }
    history->counts[write_index] = stored_count;
    history->valid[write_index] = DB_BOOL(stored_count == current.count);
    history->next_index =
        (write_index + 1U) % DB_PRESENTATION_DAMAGE_HISTORY_LENGTH;
    history->count =
        DB_MIN(history->count + 1U, DB_PRESENTATION_DAMAGE_HISTORY_LENGTH);
    if (out_force_full != NULL) {
        *out_force_full = DB_BOOL(valid == 0);
    }
    return builder.count;
}

size_t db_presentation_damage_history_resolve_ir(
    db_presentation_damage_history_t *history,
    const db_presentation_buffer_age_t *age, const db_render_ir_view_t *ir,
    db_render_ir_region_id_t damage_region, uint32_t rows, uint32_t cols,
    db_grid_block_t *out_blocks, size_t out_capacity, int *out_force_full) {
    db_grid_block_t current[DB_PRESENTATION_DAMAGE_RECTS_PER_FRAME] = {};
    int overflow = 0;
    const size_t current_count = db_render_ir_region_copy_grid_blocks(
        ir, damage_region, current, DB_PRESENTATION_DAMAGE_RECTS_PER_FRAME,
        &overflow);
    db_presentation_buffer_age_t resolved_age =
        (age != NULL) ? *age : (db_presentation_buffer_age_t){0};
    if (overflow != 0) {
        resolved_age.valid = 0;
        resolved_age.force_full_repair = 1;
        resolved_age.fallback_reason = "damage_capacity_exceeded";
    }
    return db_presentation_damage_history_resolve(
        history, &resolved_age,
        (db_grid_block_view_t){.blocks = current, .count = current_count}, rows,
        cols, out_blocks, out_capacity, out_force_full);
}

static uint32_t db_scale_edge_floor(uint32_t edge, uint32_t source_extent,
                                    uint32_t destination_extent) {
    return (uint32_t)(((uint64_t)edge * destination_extent) / source_extent);
}

static uint32_t db_scale_edge_ceil(uint32_t edge, uint32_t source_extent,
                                   uint32_t destination_extent) {
    return (
        uint32_t)((((uint64_t)edge * destination_extent) + source_extent - 1U) /
                  source_extent);
}

size_t db_presentation_map_logical_damage(
    db_grid_block_view_t logical, uint32_t grid_rows, uint32_t grid_cols,
    uint32_t destination_width, uint32_t destination_height,
    db_damage_block_t *output, size_t output_capacity, int *out_overflow) {
    if (out_overflow != NULL) {
        *out_overflow = 0;
    }
    if ((grid_rows == 0U) || (grid_cols == 0U) || (destination_width == 0U) ||
        (destination_height == 0U) ||
        ((logical.count > 0U) && (logical.blocks == NULL)) ||
        ((output_capacity > 0U) && (output == NULL))) {
        if (out_overflow != NULL) {
            *out_overflow = 1;
        }
        return 0U;
    }
    size_t count = 0U;
    for (size_t index = 0U; index < logical.count; index++) {
        const db_grid_block_t *const block = &logical.blocks[index];
        if ((block->row_count == 0U) || (block->col_count == 0U) ||
            (block->row_start >= grid_rows) ||
            (block->col_start >= grid_cols)) {
            continue;
        }
        if (count == output_capacity) {
            if (out_overflow != NULL) {
                *out_overflow = 1;
            }
            return count;
        }
        const uint32_t row_end = db_checked_u64_to_u32(
            "presentation_policy", "logical_row_end",
            DB_MIN((uint64_t)grid_rows,
                   (uint64_t)block->row_start + (uint64_t)block->row_count));
        const uint32_t col_end = db_checked_u64_to_u32(
            "presentation_policy", "logical_col_end",
            DB_MIN((uint64_t)grid_cols,
                   (uint64_t)block->col_start + (uint64_t)block->col_count));
        const uint32_t x0 =
            db_scale_edge_floor(block->col_start, grid_cols, destination_width);
        const uint32_t x1 =
            db_scale_edge_ceil(col_end, grid_cols, destination_width);
        const uint32_t y0 = db_scale_edge_floor(block->row_start, grid_rows,
                                                destination_height);
        const uint32_t y1 =
            db_scale_edge_ceil(row_end, grid_rows, destination_height);
        if ((x1 > x0) && (y1 > y0)) {
            output[count++] = (db_damage_block_t){
                .row_start = y0,
                .row_count = DB_MIN(y1, destination_height) - y0,
                .col_start = x0,
                .col_count = DB_MIN(x1, destination_width) - x0,
            };
        }
    }
    return count;
}
