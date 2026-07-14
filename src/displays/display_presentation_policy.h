#ifndef DRIVERBENCH_DISPLAY_PRESENTATION_POLICY_H
#define DRIVERBENCH_DISPLAY_PRESENTATION_POLICY_H

#include <stddef.h>
#include <stdint.h>

#include "core/db_geometry.h"
#include "core/db_render_ir.h"

typedef enum {
    DB_PRESENTATION_BUFFER_AGE_UNAVAILABLE = 0,
    DB_PRESENTATION_BUFFER_AGE_GLX = 1,
    DB_PRESENTATION_BUFFER_AGE_EGL = 2,
    DB_PRESENTATION_BUFFER_AGE_SCANOUT = 3,
} db_presentation_buffer_age_provider_t;

typedef struct {
    db_presentation_buffer_age_provider_t provider;
    uint32_t raw_age;
    uint32_t effective_replay_depth;
    uint32_t history_capacity;
    int valid;
    int force_full_repair;
    const char *fallback_reason;
} db_presentation_buffer_age_t;

enum {
    DB_PRESENTATION_DAMAGE_HISTORY_LENGTH = 8U,
    DB_PRESENTATION_DAMAGE_RECTS_PER_FRAME = 128U,
};

typedef struct {
    db_grid_block_t blocks[DB_PRESENTATION_DAMAGE_HISTORY_LENGTH]
                          [DB_PRESENTATION_DAMAGE_RECTS_PER_FRAME];
    size_t counts[DB_PRESENTATION_DAMAGE_HISTORY_LENGTH];
    int valid[DB_PRESENTATION_DAMAGE_HISTORY_LENGTH];
    uint32_t next_index;
    uint32_t count;
} db_presentation_damage_history_t;

typedef struct {
    uint32_t destination_width;
    uint32_t destination_height;
    db_pixel_block_view_t damage;
    db_presentation_buffer_age_t buffer_age;
    int force_full;
    const char *repair_reason;
} db_gl_presentation_frame_t;

const char *db_presentation_buffer_age_provider_name(
    db_presentation_buffer_age_provider_t provider);
db_presentation_buffer_age_t db_presentation_buffer_age_resolve(
    db_presentation_buffer_age_provider_t provider, uint32_t raw_age,
    uint32_t history_capacity);
db_presentation_buffer_age_t db_presentation_buffer_age_from_serial(
    uint64_t current_serial, uint64_t previous_serial, int previous_valid,
    uint32_t history_capacity);
void db_presentation_log_buffer_age(const char *backend_name,
                                    const db_presentation_buffer_age_t *age);
void db_presentation_damage_history_reset(
    db_presentation_damage_history_t *history);
size_t db_presentation_damage_history_resolve(
    db_presentation_damage_history_t *history,
    const db_presentation_buffer_age_t *age, db_grid_block_view_t current,
    uint32_t rows, uint32_t cols, db_grid_block_t *out_blocks,
    size_t out_capacity, int *out_force_full);
size_t db_presentation_damage_history_resolve_ir(
    db_presentation_damage_history_t *history,
    const db_presentation_buffer_age_t *age, const db_render_ir_view_t *ir,
    db_render_ir_region_id_t damage_region, uint32_t rows, uint32_t cols,
    db_grid_block_t *out_blocks, size_t out_capacity, int *out_force_full);
size_t db_presentation_map_logical_damage(
    db_grid_block_view_t logical, uint32_t grid_rows, uint32_t grid_cols,
    uint32_t destination_width, uint32_t destination_height,
    db_damage_block_t *output, size_t output_capacity, int *out_overflow);

#endif
