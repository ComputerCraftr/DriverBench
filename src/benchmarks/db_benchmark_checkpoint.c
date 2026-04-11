#include "db_benchmark_checkpoint_internal.h"

#include "core/db_core.h"
#include "core/db_format_contract.h"
#include "core/db_frame_plan.h"
#include "core/db_geometry.h"
#include "core/db_geometry_builder.h"
#include "core/db_hash.h"
#include "core/db_log.h"
#include "core/db_numeric.h"
#include "core/db_render_result.h"
#include "core/db_render_types.h"
#include "core/db_sort.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define DB_CHECKPOINT_BACKEND "benchmark_checkpoint"

static size_t checkpoint_size_bytes(uint32_t width, uint32_t height,
                                    db_pixel_format_t format) {
    const size_t pixel_count = db_checked_mul_size(
        DB_CHECKPOINT_BACKEND, "pixel_count", (size_t)width, (size_t)height);
    const size_t pixel_bytes = (format == DB_PIXEL_FORMAT_RGBA16F)
                                   ? DB_RGBA16F_BYTES_PER_PIXEL
                                   : DB_RGBA8_BYTES_PER_PIXEL;
    return db_checked_mul_size(DB_CHECKPOINT_BACKEND, "allocation_size",
                               pixel_count, pixel_bytes);
}

static void checkpoint_apply_blocks(db_benchmark_checkpoint_t *checkpoint,
                                    db_colored_f64_block_view_t blocks) {
    if ((checkpoint == NULL) || (checkpoint->surface.pixels == NULL)) {
        return;
    }
    for (size_t index = 0U; index < blocks.count; index++) {
        const db_colored_f64_block_t *const block = &blocks.blocks[index];
        db_rgb_pixels_fill_damage_block_f64(
            checkpoint->surface.pixel_width, checkpoint->surface.pixel_height,
            checkpoint->surface.pixels, checkpoint->surface.format,
            block->row_start, block->row_count, block->col_start,
            block->col_count, block->rgb);
        const uint32_t row_end = DB_MIN(checkpoint->surface.pixel_height,
                                        block->row_start + block->row_count);
        const uint32_t col_end = DB_MIN(checkpoint->surface.pixel_width,
                                        block->col_start + block->col_count);
        for (uint32_t row = block->row_start; row < row_end; row++) {
            for (uint32_t col = block->col_start; col < col_end; col++) {
                const size_t base =
                    (((size_t)row * checkpoint->surface.pixel_width) + col) *
                    3U;
                memcpy(&checkpoint->canonical_rgb[base], block->rgb,
                       3U * sizeof(double));
            }
        }
    }
}

void db_benchmark_checkpoint_init(db_benchmark_checkpoint_t *checkpoint,
                                  uint32_t width, uint32_t height,
                                  db_pixel_format_t format,
                                  const double seed_rgb[3]) {
    if ((checkpoint == NULL) || (seed_rgb == NULL) || (width == 0U) ||
        (height == 0U)) {
        return;
    }
    *checkpoint = (db_benchmark_checkpoint_t){0};
    checkpoint->allocation_size_bytes =
        checkpoint_size_bytes(width, height, format);
    checkpoint->pixel_count =
        db_checked_mul_size(DB_CHECKPOINT_BACKEND, "canonical_pixel_count",
                            (size_t)width, (size_t)height);
    checkpoint->surface = (db_pixel_surface_t){
        .pixel_width = width,
        .pixel_height = height,
        .pixels = db_malloc_or_fail(DB_CHECKPOINT_BACKEND, "pixels", 1U,
                                    checkpoint->allocation_size_bytes),
        .format = format,
    };
    db_rgb_pixels_fill_solid_f64(width, height, checkpoint->surface.pixels,
                                 format, seed_rgb);
    const size_t component_count = db_checked_mul_size(
        DB_CHECKPOINT_BACKEND, "component_count", checkpoint->pixel_count, 3U);
    checkpoint->canonical_rgb =
        (double *)db_malloc_or_fail(DB_CHECKPOINT_BACKEND, "canonical_rgb",
                                    component_count, sizeof(double));
    checkpoint->overlay_rgb = (double *)db_malloc_or_fail(
        DB_CHECKPOINT_BACKEND, "overlay_rgb", component_count, sizeof(double));
    checkpoint->overlay_generation = (uint32_t *)db_calloc_or_fail(
        DB_CHECKPOINT_BACKEND, "overlay_generation", checkpoint->pixel_count,
        sizeof(uint32_t), DB_CACHELINE_ALIGNMENT_BYTES);
    checkpoint->overlay_dirty_indices = (uint32_t *)db_malloc_or_fail(
        DB_CHECKPOINT_BACKEND, "overlay_dirty_indices", checkpoint->pixel_count,
        sizeof(uint32_t));
    for (size_t index = 0U; index < checkpoint->pixel_count; index++) {
        memcpy(&checkpoint->canonical_rgb[index * 3U], seed_rgb,
               3U * sizeof(double));
    }
    checkpoint->generation = 1U;
    checkpoint->content_revision = 1U;
    checkpoint->enabled = 1;
    const db_log_field_t fields[] = {
        DB_LOG_TOKEN("action", "create"),
        DB_LOG_U64("generation", checkpoint->generation),
        DB_LOG_U64("width", width),
        DB_LOG_U64("height", height),
        DB_LOG_TOKEN("working_format", db_pixel_format_name(format)),
        DB_LOG_U64("allocation_bytes", checkpoint->allocation_size_bytes),
    };
    db_log_info(DB_CHECKPOINT_BACKEND, "benchmark_checkpoint", fields,
                DB_LOG_FIELD_COUNT(fields));
}

void db_benchmark_checkpoint_shutdown(db_benchmark_checkpoint_t *checkpoint) {
    if (checkpoint == NULL) {
        return;
    }
    if (checkpoint->enabled != 0) {
        const db_log_field_t fields[] = {
            DB_LOG_TOKEN("action", "destroy"),
            DB_LOG_U64("generation", checkpoint->generation),
            DB_LOG_U64("content_revision", checkpoint->content_revision),
            DB_LOG_U64("allocation_bytes", checkpoint->allocation_size_bytes),
        };
        db_log_info(DB_CHECKPOINT_BACKEND, "benchmark_checkpoint", fields,
                    DB_LOG_FIELD_COUNT(fields));
    }
    free(checkpoint->surface.pixels);
    free(checkpoint->canonical_rgb);
    free(checkpoint->overlay_rgb);
    free(checkpoint->overlay_generation);
    free(checkpoint->overlay_dirty_indices);
    *checkpoint = (db_benchmark_checkpoint_t){0};
}

void db_benchmark_checkpoint_publish_seed(
    const db_benchmark_checkpoint_t *checkpoint, db_frame_plan_t *plan) {
    if ((checkpoint == NULL) || (checkpoint->enabled == 0) || (plan == NULL)) {
        return;
    }
    plan->rebuild_seed = (db_frame_rebuild_seed_t){
        .kind = DB_FRAME_REBUILD_SEED_RASTER,
        .raster = checkpoint->surface,
        .generation = checkpoint->generation,
        .content_revision = checkpoint->content_revision,
        .committed_frame_index = checkpoint->committed_frame_index,
        .committed_frame_valid = checkpoint->committed_frame_valid,
    };
}

void db_benchmark_checkpoint_overlay_begin(
    db_benchmark_checkpoint_t *checkpoint) {
    if ((checkpoint == NULL) || (checkpoint->enabled == 0)) {
        return;
    }
    checkpoint->active_overlay_generation++;
    if (checkpoint->active_overlay_generation == 0U) {
        memset(checkpoint->overlay_generation, 0,
               checkpoint->pixel_count * sizeof(uint32_t));
        checkpoint->active_overlay_generation = 1U;
    }
    checkpoint->overlay_dirty_count = 0U;
}

void db_benchmark_checkpoint_overlay_write(
    db_benchmark_checkpoint_t *checkpoint,
    const db_colored_f64_block_t *block) {
    if ((checkpoint == NULL) || (block == NULL) || (checkpoint->enabled == 0)) {
        return;
    }
    const uint32_t width = checkpoint->surface.pixel_width;
    const uint32_t row_end = DB_MIN(checkpoint->surface.pixel_height,
                                    block->row_start + block->row_count);
    const uint32_t col_end = DB_MIN(width, block->col_start + block->col_count);
    for (uint32_t row = block->row_start; row < row_end; row++) {
        for (uint32_t col = block->col_start; col < col_end; col++) {
            const size_t index = ((size_t)row * width) + col;
            if (checkpoint->overlay_generation[index] !=
                checkpoint->active_overlay_generation) {
                checkpoint->overlay_generation[index] =
                    checkpoint->active_overlay_generation;
                checkpoint
                    ->overlay_dirty_indices[checkpoint->overlay_dirty_count++] =
                    (uint32_t)index;
            }
            memcpy(&checkpoint->overlay_rgb[index * 3U], block->rgb,
                   3U * sizeof(double));
        }
    }
}

void db_benchmark_checkpoint_overlay_publish(
    db_benchmark_checkpoint_t *checkpoint, db_geometry_builder_t *builder) {
    if ((checkpoint == NULL) || (builder == NULL) ||
        (checkpoint->overlay_dirty_count == 0U)) {
        return;
    }
    (void)db_sort_u32_ascending(checkpoint->overlay_dirty_indices,
                                checkpoint->overlay_dirty_count);
    const uint32_t width = checkpoint->surface.pixel_width;
    size_t offset = 0U;
    while (offset < checkpoint->overlay_dirty_count) {
        const uint32_t first = checkpoint->overlay_dirty_indices[offset];
        const uint32_t row = first / width;
        const uint32_t col_start = first % width;
        uint32_t col_end = col_start + 1U;
        const double *const rgb = &checkpoint->overlay_rgb[(size_t)first * 3U];
        offset++;
        while (offset < checkpoint->overlay_dirty_count) {
            const uint32_t next = checkpoint->overlay_dirty_indices[offset];
            if ((next / width != row) || (next % width != col_end) ||
                (db_equal_f64_rgb3(
                     rgb, &checkpoint->overlay_rgb[(size_t)next * 3U]) == 0)) {
                break;
            }
            col_end++;
            offset++;
        }
        (void)db_geometry_builder_add_span(builder, row, col_start, col_end,
                                           rgb);
    }
}

void db_benchmark_checkpoint_read_with_overlay(
    const db_benchmark_checkpoint_t *checkpoint, uint32_t row, uint32_t col,
    double out_rgb[3]) {
    if (out_rgb == NULL) {
        return;
    }
    if ((checkpoint == NULL) || (checkpoint->enabled == 0) ||
        (checkpoint->surface.pixels == NULL) ||
        (row >= checkpoint->surface.pixel_height) ||
        (col >= checkpoint->surface.pixel_width)) {
        out_rgb[0] = 0.0;
        out_rgb[1] = 0.0;
        out_rgb[2] = 0.0;
        return;
    }
    const size_t index = ((size_t)row * checkpoint->surface.pixel_width) + col;
    const double *source = &checkpoint->canonical_rgb[index * 3U];
    if (checkpoint->overlay_generation[index] ==
        checkpoint->active_overlay_generation) {
        source = &checkpoint->overlay_rgb[index * 3U];
    }
    memcpy(out_rgb, source, 3U * sizeof(double));
}

void db_benchmark_checkpoint_commit(db_benchmark_checkpoint_t *checkpoint,
                                    const db_frame_plan_t *plan,
                                    const db_render_result_t *result) {
    if ((checkpoint == NULL) || (checkpoint->enabled == 0) || (plan == NULL) ||
        (result == NULL) || (result->success == 0)) {
        return;
    }
    checkpoint_apply_blocks(checkpoint, plan->geometry.current_blocks);
    checkpoint->content_revision++;
    checkpoint->committed_frame_index = plan->frame_index;
    checkpoint->committed_frame_valid = 1;
    if (result->working_hash_valid != 0) {
        const uint64_t checkpoint_hash = db_hash_working_rgba8(
            checkpoint->surface.pixels, checkpoint->surface.format,
            checkpoint->surface.pixel_width, checkpoint->surface.pixel_height,
            (size_t)checkpoint->surface.pixel_width *
                db_pixel_surface_pixel_bytes(&checkpoint->surface),
            0);
        if (checkpoint_hash != result->working_hash) {
            const db_log_field_t fields[] = {
                DB_LOG_TOKEN("code", "checkpoint_hash_mismatch"),
                DB_LOG_U64("frame", plan->frame_index),
                DB_LOG_HEX64("checkpoint_hash", checkpoint_hash),
                DB_LOG_HEX64("renderer_hash", result->working_hash),
            };
            db_log_fail(DB_CHECKPOINT_BACKEND, "checkpoint_error", fields,
                        DB_LOG_FIELD_COUNT(fields));
        }
    }
}
