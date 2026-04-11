#include "damage_trace.h"

#include "../core/db_core.h"
#include "../core/db_frame_plan.h"
#include "../core/db_geometry.h"
#include "../core/db_hash.h"
#include "../core/db_log.h"
#include "../core/db_numeric.h"
#include "../core/db_trace.h"
#include "core/db_render_types.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>

enum { DB_DAMAGE_TRACE_DETAIL_LIMIT = 128U };

static uint32_t g_trace_frame = UINT32_MAX;
static uint64_t g_trace_sequence = 0U;

static const char *
db_damage_trace_backend_name(db_damage_trace_backend_t value) {
    static const char *const names[] = {"cpu", "gl1", "gl3", "vulkan",
                                        "display"};
    return ((size_t)value < (sizeof(names) / sizeof(names[0]))) ? names[value]
                                                                : "unknown";
}

static const char *db_damage_trace_stage_name(db_damage_trace_stage_t value) {
    static const char *const names[] = {
        "logical",       "normalized",    "renderer_write",
        "shadow_write",  "staging_write", "upload",
        "texture_image", "render_target", "present",
    };
    return ((size_t)value < (sizeof(names) / sizeof(names[0]))) ? names[value]
                                                                : "unknown";
}

static const char *db_damage_trace_buffer_name(db_damage_trace_buffer_t value) {
    static const char *const names[] = {
        "none",         "logical_plan", "cpu_surface", "gl1_backbuffer",
        "gl1_shadow",   "pbo_unpack",   "pbo_pack",    "gl_texture",
        "gl_fbo",       "gl_default",   "vk_staging",  "vk_image",
        "vk_swapchain",
    };
    return ((size_t)value < (sizeof(names) / sizeof(names[0]))) ? names[value]
                                                                : "unknown";
}

static const char *
db_damage_trace_operation_name(db_damage_trace_operation_t value) {
    static const char *const names[] = {
        "seed",   "rebuild",  "incremental", "draw",
        "copy",   "blit",     "map_write",   "subdata",
        "upload", "fallback", "readback",    "present",
    };
    return ((size_t)value < (sizeof(names) / sizeof(names[0]))) ? names[value]
                                                                : "unknown";
}

static const char *
db_geometry_execution_name(db_geometry_execution_operation_t value) {
    static const char *const names[] = {"no_op", "incremental", "rebuild",
                                        "full_redraw"};
    return ((size_t)value < (sizeof(names) / sizeof(names[0]))) ? names[value]
                                                                : "unknown";
}

static const char *
db_frame_rebuild_reason_name(db_frame_rebuild_reason_t value) {
    static const char *const names[] = {
        "none",     "initial_target", "resize",
        "explicit", "seed",           "geometry_recovery",
    };
    return ((size_t)value < (sizeof(names) / sizeof(names[0]))) ? names[value]
                                                                : "unknown";
}

static const char *
db_target_lifecycle_action_name(db_target_lifecycle_action_t value) {
    static const char *const names[] = {"create", "recreate", "invalidate",
                                        "rebuild", "destroy"};
    return ((size_t)value < (sizeof(names) / sizeof(names[0]))) ? names[value]
                                                                : "unknown";
}

static const char *db_damage_trace_result_name(db_damage_trace_result_t value) {
    static const char *const names[] = {"executed", "skipped", "fallback",
                                        "failed"};
    return ((size_t)value < (sizeof(names) / sizeof(names[0]))) ? names[value]
                                                                : "unknown";
}

int db_damage_trace_level(void) { return db_trace_config_current().damage; }

int db_damage_trace_enabled(void) {
    return DB_BOOL(db_damage_trace_level() > 0);
}

size_t db_damage_trace_detail_count(size_t block_count, int trace_level) {
    if (trace_level < 2) {
        return 0U;
    }
    return (trace_level >= 3)
               ? block_count
               : DB_MIN(block_count, (size_t)DB_DAMAGE_TRACE_DETAIL_LIMIT);
}

uint64_t db_damage_trace_surface_hash(const db_pixel_surface_t *surface) {
    return db_damage_trace_surface_hash_oriented(surface, 0);
}

uint64_t
db_damage_trace_surface_hash_oriented(const db_pixel_surface_t *surface,
                                      int rows_bottom_to_top) {
    if ((surface == NULL) || (surface->pixels == NULL) ||
        (surface->pixel_width == 0U) || (surface->pixel_height == 0U)) {
        return 0U;
    }
    const size_t pixel_bytes = db_pixel_surface_pixel_bytes(surface);
    const size_t stride_bytes =
        db_checked_mul_size("damage_trace", "surface_stride",
                            (size_t)surface->pixel_width, pixel_bytes);
    if (surface->format == DB_PIXEL_FORMAT_RGBA16F) {
        return db_hash_rgba16f_pixels_canonical(
            (const uint16_t *)surface->pixels, surface->pixel_width,
            surface->pixel_height, stride_bytes, rows_bottom_to_top);
    }
    return db_hash_rgba8_pixels_canonical(surface->pixels, surface->pixel_width,
                                          surface->pixel_height, stride_bytes,
                                          rows_bottom_to_top);
}

db_damage_trace_summary_t
db_damage_trace_summarize(const db_damage_trace_event_t *event) {
    db_damage_trace_summary_t summary = {0};
    if ((event == NULL) || (event->blocks == NULL) ||
        (event->block_count == 0U)) {
        return summary;
    }

    const size_t count = event->block_count;
    uint64_t summed_units = 0U;
    uint32_t min_x = UINT32_MAX;
    uint32_t min_y = UINT32_MAX;
    uint32_t max_x = 0U;
    uint32_t max_y = 0U;

    for (size_t i = 0U; i < count; i++) {
        const db_damage_block_t block = event->blocks[i];
        const uint64_t x_end64 =
            (uint64_t)block.col_start + (uint64_t)block.col_count;
        const uint64_t y_end64 =
            (uint64_t)block.row_start + (uint64_t)block.row_count;
        if ((block.col_count == 0U) || (block.row_count == 0U) ||
            (x_end64 > event->width) || (y_end64 > event->height)) {
            summary.rejected_block_count++;
            continue;
        }
        const uint32_t x_end = (uint32_t)x_end64;
        const uint32_t y_end = (uint32_t)y_end64;
        min_x = DB_MIN(min_x, block.col_start);
        min_y = DB_MIN(min_y, block.row_start);
        max_x = DB_MAX(max_x, x_end);
        max_y = DB_MAX(max_y, y_end);
        summed_units += (uint64_t)block.col_count * (uint64_t)block.row_count;
        summary.valid_block_count++;
    }
    if (summary.valid_block_count == 0U) {
        return summary;
    }
    summary.bounds = (db_damage_block_t){
        .row_start = min_y,
        .row_count = max_y - min_y,
        .col_start = min_x,
        .col_count = max_x - min_x,
    };

    const size_t map_size = db_checked_mul_size(
        "damage_trace", "coverage_map_size", event->width, event->height);
    uint8_t *const coverage = (uint8_t *)calloc(map_size, sizeof(*coverage));
    if (coverage == NULL) {
        DB_RUNTIME_FAIL("damage_trace",
                        "failed to allocate %zu-byte coverage map", map_size);
    }
    for (size_t i = 0U; i < count; i++) {
        const db_damage_block_t block = event->blocks[i];
        const uint64_t x_end64 =
            (uint64_t)block.col_start + (uint64_t)block.col_count;
        const uint64_t y_end64 =
            (uint64_t)block.row_start + (uint64_t)block.row_count;
        if ((block.col_count == 0U) || (block.row_count == 0U) ||
            (x_end64 > event->width) || (y_end64 > event->height)) {
            continue;
        }
        for (uint32_t y = block.row_start; y < (uint32_t)y_end64; y++) {
            const size_t row_offset = db_checked_mul_size(
                "damage_trace", "coverage_row", y, event->width);
            for (uint32_t x = block.col_start; x < (uint32_t)x_end64; x++) {
                uint8_t *const cell = &coverage[row_offset + x];
                if (*cell == 0U) {
                    *cell = 1U;
                    summary.covered_units++;
                }
            }
        }
    }
    summary.duplicate_units = summed_units - summary.covered_units;
    summary.union_hash = db_fnv_blockhash_u64(
        coverage, map_size, DB_U32_SALT_PALETTE, DB_FNV1A64_OFFSET);
    free(coverage);
    return summary;
}

db_damage_trace_summary_t
db_damage_trace_emit(const db_damage_trace_event_t *event) {
    db_damage_trace_summary_t summary = {0};
    if ((event == NULL) || (db_damage_trace_enabled() == 0)) {
        return summary;
    }
    if (event->frame_index != g_trace_frame) {
        g_trace_frame = event->frame_index;
        g_trace_sequence = 0U;
    }
    summary = db_damage_trace_summarize(event);
    summary.sequence = ++g_trace_sequence;
    const char *const reason = (event->reason != NULL) ? event->reason : "none";
    const char *const format =
        (event->pixel_format == DB_PIXEL_FORMAT_RGBA16F) ? "rgba16f" : "rgba8";
    const int trace_level = db_damage_trace_level();
    const size_t emitted_count =
        db_damage_trace_detail_count(event->block_count, trace_level);
    const size_t omitted_count = event->block_count - emitted_count;
    const db_log_field_t fields[] = {
        DB_LOG_U64("frame", event->frame_index),
        DB_LOG_U64("seq", summary.sequence),
        DB_LOG_TOKEN("backend", db_damage_trace_backend_name(event->backend)),
        DB_LOG_TOKEN("stage", db_damage_trace_stage_name(event->stage)),
        DB_LOG_TOKEN("op", db_damage_trace_operation_name(event->operation)),
        DB_LOG_TOKEN("src", db_damage_trace_buffer_name(event->source)),
        DB_LOG_U64("src_index", event->source_index),
        DB_LOG_TOKEN("dst", db_damage_trace_buffer_name(event->destination)),
        DB_LOG_U64("dst_index", event->destination_index),
        DB_LOG_TOKEN("space", (event->space == DB_DAMAGE_TRACE_SPACE_GRID)
                                  ? "grid"
                                  : "pixel"),
        DB_LOG_U64("width", event->width),
        DB_LOG_U64("height", event->height),
        DB_LOG_TOKEN("format", format),
        DB_LOG_U64("stride", event->row_stride_bytes),
        DB_LOG_U64("blocks", event->block_count),
        DB_LOG_U64("emitted_count", (trace_level >= 2) ? emitted_count : 0U),
        DB_LOG_U64("omitted_count",
                   (trace_level >= 2) ? omitted_count : event->block_count),
        DB_LOG_U64("valid", summary.valid_block_count),
        DB_LOG_U64("rejected", summary.rejected_block_count),
        DB_LOG_BOOL("truncated", summary.truncated),
        DB_LOG_U64("pixels", summary.covered_units),
        DB_LOG_U64("duplicate_pixels", summary.duplicate_units),
        DB_LOG_HEX64("union_hash", summary.union_hash),
        DB_LOG_U64("bounds_x", summary.bounds.col_start),
        DB_LOG_U64("bounds_y", summary.bounds.row_start),
        DB_LOG_U64("bounds_width", summary.bounds.col_count),
        DB_LOG_U64("bounds_height", summary.bounds.row_count),
        DB_LOG_U64("offset", event->transfer_offset_bytes),
        DB_LOG_U64("bytes", event->transfer_size_bytes),
        DB_LOG_HEX64("src_hash", event->source_hash),
        DB_LOG_HEX64("dst_hash", event->destination_hash),
        DB_LOG_TOKEN("reason", reason),
        DB_LOG_TOKEN("result", db_damage_trace_result_name(event->result)),
        DB_LOG_TOKEN("target",
                     (event->target != NULL) ? event->target : "none"),
        DB_LOG_U64("target_generation", event->target_generation),
        DB_LOG_TOKEN("present_method", (event->present_method != NULL)
                                           ? event->present_method
                                           : "none"),
    };
    db_log_info("damage_trace", "damage_summary", fields,
                DB_LOG_FIELD_COUNT(fields));

    if ((trace_level >= 2) && (event->blocks != NULL)) {
        for (size_t i = 0U; i < emitted_count; i++) {
            const db_damage_block_t block = event->blocks[i];
            const uint64_t x_end =
                (uint64_t)block.col_start + (uint64_t)block.col_count;
            const uint64_t y_end =
                (uint64_t)block.row_start + (uint64_t)block.row_count;
            const int valid =
                DB_BOOL((block.col_count > 0U) && (block.row_count > 0U) &&
                        (x_end <= event->width) && (y_end <= event->height));
            const db_log_field_t block_fields[] = {
                DB_LOG_U64("frame", event->frame_index),
                DB_LOG_U64("event_seq", summary.sequence),
                DB_LOG_U64("index", i),
                DB_LOG_TOKEN("backend",
                             db_damage_trace_backend_name(event->backend)),
                DB_LOG_TOKEN("stage", db_damage_trace_stage_name(event->stage)),
                DB_LOG_TOKEN("space",
                             (event->space == DB_DAMAGE_TRACE_SPACE_GRID)
                                 ? "grid"
                                 : "pixel"),
                DB_LOG_U64("x", block.col_start),
                DB_LOG_U64("y", block.row_start),
                DB_LOG_U64("width", block.col_count),
                DB_LOG_U64("height", block.row_count),
                DB_LOG_U64("offset", event->transfer_offset_bytes),
                DB_LOG_U64("bytes", event->transfer_size_bytes),
                DB_LOG_BOOL("valid", valid),
            };
            db_log_info("damage_trace", "damage_block", block_fields,
                        DB_LOG_FIELD_COUNT(block_fields));
        }
    }
    return summary;
}

void db_damage_trace_emit_frame_plan(db_damage_trace_backend_t backend,
                                     const char *target,
                                     uint32_t target_generation,
                                     const db_frame_plan_t *plan) {
    if ((plan == NULL) || (db_damage_trace_enabled() == 0)) {
        return;
    }
    const db_log_field_t fields[] = {
        DB_LOG_U64("frame", plan->frame_index),
        DB_LOG_U64("simulation_ticks", plan->simulation_tick_count),
        DB_LOG_U64("simulation_chunks", plan->simulation_chunk_count),
        DB_LOG_U64("simulation_boundaries", plan->simulation_boundary_count),
        DB_LOG_U64("simulation_terminal_items",
                   plan->simulation_terminal_item_count),
        DB_LOG_TOKEN("backend", db_damage_trace_backend_name(backend)),
        DB_LOG_TOKEN("target", (target != NULL) ? target : "none"),
        DB_LOG_U64("target_generation", target_generation),
        DB_LOG_U64("grid_width", plan->grid_cols),
        DB_LOG_U64("grid_height", plan->grid_rows),
        DB_LOG_U64("pixel_width", plan->pixel_width),
        DB_LOG_U64("pixel_height", plan->pixel_height),
        DB_LOG_TOKEN("geometry_operation",
                     db_geometry_execution_name(plan->geometry.operation)),
        DB_LOG_U64("logical_count", plan->geometry.logical_damage.count),
        DB_LOG_U64("current_count", plan->geometry.current_blocks.count),
        DB_LOG_TOKEN(
            "rebuild_seed",
            (plan->rebuild_seed.kind == DB_FRAME_REBUILD_SEED_GEOMETRY)
                ? "geometry"
                : ((plan->rebuild_seed.kind == DB_FRAME_REBUILD_SEED_RASTER)
                       ? "raster"
                       : "none")),
        DB_LOG_U64("rebuild_count", plan->rebuild_seed.geometry.count),
        DB_LOG_U64("rebuild_seed_generation", plan->rebuild_seed.generation),
        DB_LOG_U64("repair_count", plan->geometry.repair_union.count),
        DB_LOG_U64("replay_depth", plan->geometry.replay_depth),
        DB_LOG_BOOL("overflowed", plan->geometry.overflowed),
        DB_LOG_BOOL("rebuild_required", plan->rebuild_required),
        DB_LOG_TOKEN("rebuild_reason",
                     db_frame_rebuild_reason_name(plan->rebuild_reason)),
        DB_LOG_BOOL("force_full_upload", plan->force_full_upload),
        DB_LOG_BOOL("seeded_background", plan->seeded_background),
        DB_LOG_BOOL("seeded_shadow_ring", plan->seeded_shadow_ring),
        DB_LOG_HEX64("expected_state_hash", plan->expected_state_hash),
    };
    db_log_info("damage_trace", "frame_plan", fields,
                DB_LOG_FIELD_COUNT(fields));
}

void db_damage_trace_emit_target_lifecycle(
    const db_target_lifecycle_event_t *event) {
    if ((event == NULL) || (db_damage_trace_enabled() == 0)) {
        return;
    }
    const db_log_field_t fields[] = {
        DB_LOG_TOKEN("backend", db_damage_trace_backend_name(event->backend)),
        DB_LOG_TOKEN("action", db_target_lifecycle_action_name(event->action)),
        DB_LOG_TOKEN("target", event->target),
        DB_LOG_U64("target_id", event->target_id),
        DB_LOG_U64("generation", event->generation),
        DB_LOG_U64("old_width", event->old_width),
        DB_LOG_U64("old_height", event->old_height),
        DB_LOG_U64("new_width", event->new_width),
        DB_LOG_U64("new_height", event->new_height),
        DB_LOG_TOKEN("format", (event->format == DB_PIXEL_FORMAT_RGBA16F)
                                   ? "rgba16f"
                                   : "rgba8"),
        DB_LOG_TOKEN("cause", (event->cause != NULL) ? event->cause : "none"),
        DB_LOG_BOOL("valid_before", event->valid_before),
        DB_LOG_BOOL("valid_after", event->valid_after),
    };
    db_log_info("damage_trace", "target_lifecycle", fields,
                DB_LOG_FIELD_COUNT(fields));
}

db_damage_trace_summary_t
db_damage_trace_emit_grid(const db_damage_trace_event_t *event,
                          const db_grid_block_t *blocks, size_t block_count) {
    db_damage_trace_summary_t summary = {0};
    if ((event == NULL) || (db_damage_trace_enabled() == 0)) {
        return summary;
    }
    if ((blocks == NULL) || (block_count == 0U)) {
        db_damage_trace_event_t empty_event = *event;
        empty_event.blocks = NULL;
        empty_event.block_count = 0U;
        return db_damage_trace_emit(&empty_event);
    }
    db_damage_block_t *const converted =
        (db_damage_block_t *)calloc(block_count, sizeof(*converted));
    if (converted == NULL) {
        DB_RUNTIME_FAIL("damage_trace",
                        "failed to allocate %zu grid trace blocks",
                        block_count);
    }
    for (size_t i = 0U; i < block_count; i++) {
        converted[i] = db_damage_block_from_grid_block(&blocks[i]);
    }
    db_damage_trace_event_t converted_event = *event;
    converted_event.blocks = converted;
    converted_event.block_count = block_count;
    summary = db_damage_trace_emit(&converted_event);
    free(converted);
    return summary;
}

db_damage_trace_summary_t
db_damage_trace_emit_colored_grid(const db_damage_trace_event_t *event,
                                  const db_colored_f64_block_t *blocks,
                                  size_t block_count) {
    db_damage_trace_summary_t summary = {0};
    if ((event == NULL) || (db_damage_trace_enabled() == 0)) {
        return summary;
    }
    if ((blocks == NULL) || (block_count == 0U)) {
        db_damage_trace_event_t empty_event = *event;
        empty_event.blocks = NULL;
        empty_event.block_count = 0U;
        return db_damage_trace_emit(&empty_event);
    }
    db_damage_block_t *const converted =
        (db_damage_block_t *)calloc(block_count, sizeof(*converted));
    if (converted == NULL) {
        DB_RUNTIME_FAIL("damage_trace",
                        "failed to allocate %zu colored trace blocks",
                        block_count);
    }
    for (size_t i = 0U; i < block_count; i++) {
        converted[i] = (db_damage_block_t){
            .row_start = blocks[i].row_start,
            .row_count = blocks[i].row_count,
            .col_start = blocks[i].col_start,
            .col_count = blocks[i].col_count,
        };
    }
    db_damage_trace_event_t converted_event = *event;
    converted_event.blocks = converted;
    converted_event.block_count = block_count;
    summary = db_damage_trace_emit(&converted_event);
    free(converted);
    return summary;
}
