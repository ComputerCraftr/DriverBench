#include "db_benchmark_checkpoint_internal.h"

#include "benchmarks/db_benchmark_emitters.h"
#include "core/db_core.h"
#include "core/db_format_contract.h"
#include "core/db_frame_plan.h"
#include "core/db_hash.h"
#include "core/db_log.h"
#include "core/db_numeric.h"
#include "core/db_render_result.h"
#include "core/db_render_types.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define DB_CHECKPOINT_BACKEND "benchmark_checkpoint"

static int checkpoint_size_bytes(uint32_t width, uint32_t height,
                                 db_pixel_format_t format,
                                 size_t *out_pixel_count,
                                 size_t *out_surface_bytes,
                                 size_t *out_total_bytes) {
    if ((out_pixel_count == NULL) || (out_surface_bytes == NULL) ||
        (out_total_bytes == NULL) || (width == 0U) || (height == 0U) ||
        ((format != DB_PIXEL_FORMAT_RGBA8) &&
         (format != DB_PIXEL_FORMAT_RGBA16F))) {
        return 0;
    }
    size_t pixel_count = 0U;
    if (db_try_mul_size((size_t)width, (size_t)height, &pixel_count) == 0) {
        return 0;
    }
    const size_t pixel_bytes = (format == DB_PIXEL_FORMAT_RGBA16F)
                                   ? DB_RGBA16F_BYTES_PER_PIXEL
                                   : DB_RGBA8_BYTES_PER_PIXEL;
    size_t surface_bytes = 0U;
    size_t generation_bytes = 0U;
    size_t dirty_span_extent = 0U;
    size_t dirty_span_bytes = 0U;
    size_t total_bytes = 0U;
    if ((db_try_mul_size(pixel_count, pixel_bytes, &surface_bytes) == 0) ||
        (db_try_mul_size(pixel_count, sizeof(uint32_t), &generation_bytes) ==
         0) ||
        (db_try_add_size(pixel_count, (size_t)height, &dirty_span_extent) ==
         0) ||
        (db_try_mul_size((dirty_span_extent / 2U) + (dirty_span_extent % 2U),
                         sizeof(db_benchmark_checkpoint_span_t),
                         &dirty_span_bytes) == 0) ||
        (db_try_add_size(surface_bytes, surface_bytes, &total_bytes) == 0) ||
        (db_try_add_size(total_bytes, generation_bytes, &total_bytes) == 0) ||
        (db_try_add_size(total_bytes, dirty_span_bytes, &total_bytes) == 0)) {
        return 0;
    }
    *out_pixel_count = pixel_count;
    *out_surface_bytes = surface_bytes;
    *out_total_bytes = total_bytes;
    return 1;
}

static int checkpoint_pixel_index(const db_benchmark_checkpoint_t *checkpoint,
                                  uint32_t row, uint32_t col, size_t *index) {
    if ((checkpoint == NULL) || (index == NULL) ||
        (row >= checkpoint->surface.pixel_height) ||
        (col >= checkpoint->surface.pixel_width)) {
        return 0;
    }
    size_t row_offset = 0U;
    return DB_BOOL(
        (db_try_mul_size((size_t)row, (size_t)checkpoint->surface.pixel_width,
                         &row_offset) != 0) &&
        (db_try_add_size(row_offset, (size_t)col, index) != 0) &&
        (*index < checkpoint->pixel_count));
}

db_benchmark_checkpoint_status_t
db_benchmark_checkpoint_preflight(uint32_t width, uint32_t height,
                                  db_pixel_format_t format,
                                  size_t *out_allocation_size_bytes) {
    size_t pixel_count = 0U;
    size_t surface_bytes = 0U;
    size_t total_bytes = 0U;
    if (checkpoint_size_bytes(width, height, format, &pixel_count,
                              &surface_bytes, &total_bytes) == 0) {
        return DB_BENCHMARK_CHECKPOINT_CAPACITY;
    }
    if (total_bytes > DB_BENCHMARK_CHECKPOINT_MAX_BYTES) {
        return DB_BENCHMARK_CHECKPOINT_MEMORY_BUDGET;
    }
    if (out_allocation_size_bytes != NULL) {
        *out_allocation_size_bytes = total_bytes;
    }
    return DB_BENCHMARK_CHECKPOINT_OK;
}

static int checkpoint_commit_overlay(db_benchmark_checkpoint_t *checkpoint) {
    if ((checkpoint == NULL) || (checkpoint->surface.pixels == NULL) ||
        (checkpoint->overlay_pixels == NULL)) {
        return 0;
    }
    const size_t pixel_bytes =
        db_pixel_surface_pixel_bytes(&checkpoint->surface);
    const uint32_t width = checkpoint->surface.pixel_width;
    size_t expected_pixel_count = 0U;
    size_t expected_surface_bytes = 0U;
    if ((pixel_bytes == 0U) ||
        (checkpoint->overlay_dirty_span_count >
         checkpoint->overlay_dirty_span_capacity) ||
        (db_try_mul_size((size_t)width,
                         (size_t)checkpoint->surface.pixel_height,
                         &expected_pixel_count) == 0) ||
        (db_try_mul_size(expected_pixel_count, pixel_bytes,
                         &expected_surface_bytes) == 0) ||
        (checkpoint->pixel_count != expected_pixel_count) ||
        (checkpoint->surface_size_bytes != expected_surface_bytes)) {
        return 0;
    }
    db_benchmark_checkpoint_span_t previous = {0};
    int have_previous = 0;
    for (size_t offset = 0U; offset < checkpoint->overlay_dirty_span_count;
         offset++) {
        const db_benchmark_checkpoint_span_t span =
            checkpoint->overlay_dirty_spans[offset];
        size_t first_index = 0U;
        size_t byte_offset = 0U;
        size_t copy_bytes = 0U;
        size_t byte_end = 0U;
        if ((span.row >= checkpoint->surface.pixel_height) ||
            (span.col_start >= span.col_end) || (span.col_end > width) ||
            ((have_previous != 0) &&
             ((span.row < previous.row) ||
              ((span.row == previous.row) &&
               (span.col_start <= previous.col_end)))) ||
            (checkpoint_pixel_index(checkpoint, span.row, span.col_start,
                                    &first_index) == 0) ||
            (db_try_mul_size(first_index, pixel_bytes, &byte_offset) == 0) ||
            (db_try_mul_size((size_t)(span.col_end - span.col_start),
                             pixel_bytes, &copy_bytes) == 0) ||
            (db_try_add_size(byte_offset, copy_bytes, &byte_end) == 0) ||
            (byte_end > checkpoint->surface_size_bytes)) {
            return 0;
        }
        previous = span;
        have_previous = 1;
    }
    for (size_t offset = 0U; offset < checkpoint->overlay_dirty_span_count;
         offset++) {
        const db_benchmark_checkpoint_span_t span =
            checkpoint->overlay_dirty_spans[offset];
        size_t first_index = 0U;
        size_t byte_offset = 0U;
        size_t copy_bytes = 0U;
        if ((checkpoint_pixel_index(checkpoint, span.row, span.col_start,
                                    &first_index) == 0) ||
            (db_try_mul_size(first_index, pixel_bytes, &byte_offset) == 0) ||
            (db_try_mul_size((size_t)(span.col_end - span.col_start),
                             pixel_bytes, &copy_bytes) == 0)) {
            DB_RUNTIME_FAIL(DB_CHECKPOINT_BACKEND,
                            "validated checkpoint span became invalid");
        }
        memcpy((uint8_t *)checkpoint->surface.pixels + byte_offset,
               (const uint8_t *)checkpoint->overlay_pixels + byte_offset,
               copy_bytes);
    }
    return 1;
}

const char *
db_benchmark_checkpoint_status_name(db_benchmark_checkpoint_status_t status) {
    switch (status) {
    case DB_BENCHMARK_CHECKPOINT_OK:
        return "ok";
    case DB_BENCHMARK_CHECKPOINT_CAPACITY:
        return "capacity";
    case DB_BENCHMARK_CHECKPOINT_MEMORY_BUDGET:
        return "memory_budget";
    case DB_BENCHMARK_CHECKPOINT_ALLOCATION_FAILED:
        return "allocation_failed";
    case DB_BENCHMARK_CHECKPOINT_UNAVAILABLE:
        return "checkpoint_unavailable";
    }
    return "unknown";
}

db_benchmark_checkpoint_status_t db_benchmark_checkpoint_init(
    db_benchmark_checkpoint_t *checkpoint, uint32_t width, uint32_t height,
    db_pixel_format_t format, const double seed_rgb[3]) {
    if ((checkpoint == NULL) || (seed_rgb == NULL) || (width == 0U) ||
        (height == 0U)) {
        return DB_BENCHMARK_CHECKPOINT_UNAVAILABLE;
    }
    *checkpoint = (db_benchmark_checkpoint_t){0};
    const db_benchmark_checkpoint_status_t preflight =
        db_benchmark_checkpoint_preflight(width, height, format,
                                          &checkpoint->allocation_size_bytes);
    if (preflight != DB_BENCHMARK_CHECKPOINT_OK) {
        return preflight;
    }
    if (checkpoint_size_bytes(width, height, format, &checkpoint->pixel_count,
                              &checkpoint->surface_size_bytes,
                              &checkpoint->allocation_size_bytes) == 0) {
        return DB_BENCHMARK_CHECKPOINT_CAPACITY;
    }
    checkpoint->surface = (db_pixel_surface_t){
        .pixel_width = width,
        .pixel_height = height,
        .pixels = malloc(checkpoint->surface_size_bytes),
        .format = format,
    };
    checkpoint->overlay_pixels = malloc(checkpoint->surface_size_bytes);
    checkpoint->overlay_generation =
        (uint32_t *)calloc(checkpoint->pixel_count, sizeof(uint32_t));
    size_t dirty_span_extent = 0U;
    size_t dirty_span_bytes = 0U;
    if (db_try_add_size(checkpoint->pixel_count, (size_t)height,
                        &dirty_span_extent) == 0) {
        db_benchmark_checkpoint_shutdown(checkpoint);
        return DB_BENCHMARK_CHECKPOINT_CAPACITY;
    }
    checkpoint->overlay_dirty_span_capacity =
        (dirty_span_extent / 2U) + (dirty_span_extent % 2U);
    if (db_try_mul_size(checkpoint->overlay_dirty_span_capacity,
                        sizeof(*checkpoint->overlay_dirty_spans),
                        &dirty_span_bytes) == 0) {
        db_benchmark_checkpoint_shutdown(checkpoint);
        return DB_BENCHMARK_CHECKPOINT_CAPACITY;
    }
    checkpoint->overlay_dirty_spans =
        (db_benchmark_checkpoint_span_t *)malloc(dirty_span_bytes);
    if ((checkpoint->surface.pixels == NULL) ||
        (checkpoint->overlay_pixels == NULL) ||
        (checkpoint->overlay_generation == NULL) ||
        (checkpoint->overlay_dirty_spans == NULL)) {
        db_benchmark_checkpoint_shutdown(checkpoint);
        return DB_BENCHMARK_CHECKPOINT_ALLOCATION_FAILED;
    }
    db_rgb_pixels_fill_solid_f64(width, height, checkpoint->surface.pixels,
                                 format, seed_rgb);
    checkpoint->generation = 1U;
    checkpoint->content_revision = 1U;
    checkpoint->overlay_valid = 1;
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
    return DB_BENCHMARK_CHECKPOINT_OK;
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
    free(checkpoint->overlay_pixels);
    free(checkpoint->overlay_generation);
    free(checkpoint->overlay_dirty_spans);
    *checkpoint = (db_benchmark_checkpoint_t){0};
}

void db_benchmark_checkpoint_overlay_begin(
    db_benchmark_checkpoint_t *checkpoint) {
    if ((checkpoint == NULL) || (checkpoint->enabled == 0)) {
        return;
    }
    uint32_t next_generation = 0U;
    if (db_try_add_u32(checkpoint->active_overlay_generation, 1U,
                       &next_generation) == 0) {
        const size_t generation_bytes = db_checked_mul_size(
            DB_CHECKPOINT_BACKEND, "overlay_generation_bytes",
            checkpoint->pixel_count, sizeof(*checkpoint->overlay_generation));
        memset(checkpoint->overlay_generation, 0, generation_bytes);
        next_generation = 1U;
    }
    checkpoint->active_overlay_generation = next_generation;
    checkpoint->overlay_dirty_span_count = 0U;
    checkpoint->dirty_span_search_comparisons = 0U;
    checkpoint->overlay_valid = 1;
}

static void
checkpoint_record_span_comparison(db_benchmark_checkpoint_t *checkpoint) {
    if (checkpoint->dirty_span_search_comparisons != UINT64_MAX) {
        checkpoint->dirty_span_search_comparisons++;
    }
}

typedef struct {
    size_t first;
    size_t after;
    uint32_t col_start;
    uint32_t col_end;
} checkpoint_span_location_t;

static checkpoint_span_location_t
checkpoint_find_dirty_span(db_benchmark_checkpoint_t *checkpoint, uint32_t row,
                           uint32_t col_start, uint32_t col_end,
                           int record_comparisons) {
    size_t lower = 0U;
    size_t upper = checkpoint->overlay_dirty_span_count;
    while (lower < upper) {
        const size_t middle = lower + ((upper - lower) / 2U);
        const db_benchmark_checkpoint_span_t candidate =
            checkpoint->overlay_dirty_spans[middle];
        if (record_comparisons != 0) {
            checkpoint_record_span_comparison(checkpoint);
        }
        if ((candidate.row < row) ||
            ((candidate.row == row) && (candidate.col_end < col_start))) {
            lower = middle + 1U;
        } else {
            upper = middle;
        }
    }
    checkpoint_span_location_t location = {
        .first = lower,
        .after = lower,
        .col_start = col_start,
        .col_end = col_end,
    };
    while ((location.after < checkpoint->overlay_dirty_span_count) &&
           (checkpoint->overlay_dirty_spans[location.after].row == row) &&
           (checkpoint->overlay_dirty_spans[location.after].col_start <=
            location.col_end)) {
        if (record_comparisons != 0) {
            checkpoint_record_span_comparison(checkpoint);
        }
        location.col_start =
            DB_MIN(location.col_start,
                   checkpoint->overlay_dirty_spans[location.after].col_start);
        location.col_end =
            DB_MAX(location.col_end,
                   checkpoint->overlay_dirty_spans[location.after].col_end);
        location.after++;
    }
    return location;
}

static int checkpoint_add_dirty_span(db_benchmark_checkpoint_t *checkpoint,
                                     uint32_t row, uint32_t col_start,
                                     uint32_t col_end) {
    const checkpoint_span_location_t location =
        checkpoint_find_dirty_span(checkpoint, row, col_start, col_end, 1);
    const size_t first = location.first;
    const size_t after = location.after;
    col_start = location.col_start;
    col_end = location.col_end;
    if (first == after) {
        if (checkpoint->overlay_dirty_span_count >=
            checkpoint->overlay_dirty_span_capacity) {
            return 0;
        }
        const size_t move_count = checkpoint->overlay_dirty_span_count - first;
        const size_t move_bytes = db_checked_mul_size(
            DB_CHECKPOINT_BACKEND, "dirty_span_insert_bytes", move_count,
            sizeof(*checkpoint->overlay_dirty_spans));
        memmove(&checkpoint->overlay_dirty_spans[first + 1U],
                &checkpoint->overlay_dirty_spans[first], move_bytes);
        checkpoint->overlay_dirty_span_count++;
    } else if (after > (first + 1U)) {
        const size_t move_count = checkpoint->overlay_dirty_span_count - after;
        const size_t move_bytes = db_checked_mul_size(
            DB_CHECKPOINT_BACKEND, "dirty_span_merge_bytes", move_count,
            sizeof(*checkpoint->overlay_dirty_spans));
        memmove(&checkpoint->overlay_dirty_spans[first + 1U],
                &checkpoint->overlay_dirty_spans[after], move_bytes);
        checkpoint->overlay_dirty_span_count -= after - first - 1U;
    }
    checkpoint->overlay_dirty_spans[first] = (db_benchmark_checkpoint_span_t){
        .row = row, .col_start = col_start, .col_end = col_end};
    return 1;
}

static int checkpoint_write_preflight(db_benchmark_checkpoint_t *checkpoint,
                                      uint32_t row_start, uint32_t row_end,
                                      uint32_t col_start, uint32_t col_end) {
    if ((checkpoint->overlay_pixels == NULL) ||
        (checkpoint->overlay_generation == NULL) ||
        (checkpoint->overlay_dirty_spans == NULL) ||
        (checkpoint->overlay_dirty_span_count >
         checkpoint->overlay_dirty_span_capacity)) {
        return 0;
    }
    size_t last_pixel = 0U;
    if (checkpoint_pixel_index(checkpoint, row_end - 1U, col_end - 1U,
                               &last_pixel) == 0) {
        return 0;
    }
    size_t insertions = 0U;
    size_t removals = 0U;
    for (uint32_t row = row_start; row < row_end; row++) {
        const checkpoint_span_location_t location =
            checkpoint_find_dirty_span(checkpoint, row, col_start, col_end, 0);
        if (location.first == location.after) {
            if (db_try_add_size(insertions, 1U, &insertions) == 0) {
                return 0;
            }
        } else {
            const size_t merged_removals = location.after - location.first - 1U;
            if (db_try_add_size(removals, merged_removals, &removals) == 0) {
                return 0;
            }
        }
    }
    if (removals > checkpoint->overlay_dirty_span_count) {
        return 0;
    }
    size_t required_count = checkpoint->overlay_dirty_span_count - removals;
    return (db_try_add_size(required_count, insertions, &required_count) !=
            0) &&
           (required_count <= checkpoint->overlay_dirty_span_capacity);
}

void db_benchmark_checkpoint_overlay_write(
    db_benchmark_checkpoint_t *checkpoint, uint32_t row_start,
    uint32_t row_count, uint32_t col_start, uint32_t col_count,
    const double rgb[3]) {
    if ((checkpoint == NULL) || (rgb == NULL) || (checkpoint->enabled == 0)) {
        return;
    }
    const uint32_t width = checkpoint->surface.pixel_width;
    const uint32_t row_end = db_checked_u64_to_u32(
        DB_CHECKPOINT_BACKEND, "checkpoint_row_end",
        DB_MIN((uint64_t)checkpoint->surface.pixel_height,
               (uint64_t)row_start + (uint64_t)row_count));
    const uint32_t col_end = db_checked_u64_to_u32(
        DB_CHECKPOINT_BACKEND, "checkpoint_col_end",
        DB_MIN((uint64_t)width, (uint64_t)col_start + (uint64_t)col_count));
    if ((row_start >= row_end) || (col_start >= col_end)) {
        return;
    }
    if (checkpoint_write_preflight(checkpoint, row_start, row_end, col_start,
                                   col_end) == 0) {
        checkpoint->overlay_valid = 0;
        return;
    }
    for (uint32_t row = row_start; row < row_end; row++) {
        if (checkpoint_add_dirty_span(checkpoint, row, col_start, col_end) ==
            0) {
            checkpoint->overlay_valid = 0;
            return;
        }
        for (uint32_t col = col_start; col < col_end; col++) {
            size_t index = 0U;
            if (checkpoint_pixel_index(checkpoint, row, col, &index) == 0) {
                checkpoint->overlay_valid = 0;
                return;
            }
            if (checkpoint->overlay_generation[index] !=
                checkpoint->active_overlay_generation) {
                checkpoint->overlay_generation[index] =
                    checkpoint->active_overlay_generation;
            }
            db_rgb_pixels_write_index_f64(checkpoint->overlay_pixels,
                                          checkpoint->surface.format, index,
                                          rgb);
        }
    }
}

int db_benchmark_checkpoint_overlay_publish(
    db_benchmark_checkpoint_t *checkpoint, db_benchmark_ir_emitter_t *emitter) {
    if ((checkpoint == NULL) || (emitter == NULL) ||
        (checkpoint->overlay_valid == 0) ||
        (checkpoint->overlay_dirty_span_count == 0U)) {
        return DB_BOOL((checkpoint != NULL) && (emitter != NULL) &&
                       (checkpoint->overlay_valid != 0));
    }
    const size_t pixel_bytes =
        db_pixel_surface_pixel_bytes(&checkpoint->surface);
    const uint8_t *const overlay = (const uint8_t *)checkpoint->overlay_pixels;
    for (size_t span_index = 0U;
         span_index < checkpoint->overlay_dirty_span_count; span_index++) {
        const db_benchmark_checkpoint_span_t dirty =
            checkpoint->overlay_dirty_spans[span_index];
        uint32_t col_start = dirty.col_start;
        while (col_start < dirty.col_end) {
            size_t first = 0U;
            if (checkpoint_pixel_index(checkpoint, dirty.row, col_start,
                                       &first) == 0) {
                checkpoint->overlay_valid = 0;
                return 0;
            }
            const size_t first_byte = db_checked_mul_size(
                DB_CHECKPOINT_BACKEND, "overlay_run_first_byte", first,
                pixel_bytes);
            uint32_t col_end = col_start + 1U;
            double rgb[3] = {0};
            db_rgb_pixels_read_index_f64(checkpoint->overlay_pixels,
                                         checkpoint->surface.format, first,
                                         rgb);
            while (col_end < dirty.col_end) {
                size_t next = 0U;
                if (checkpoint_pixel_index(checkpoint, dirty.row, col_end,
                                           &next) == 0) {
                    checkpoint->overlay_valid = 0;
                    return 0;
                }
                const size_t next_byte = db_checked_mul_size(
                    DB_CHECKPOINT_BACKEND, "overlay_run_next_byte", next,
                    pixel_bytes);
                if (memcmp(overlay + first_byte, overlay + next_byte,
                           pixel_bytes) != 0) {
                    break;
                }
                col_end++;
            }
            if (db_benchmark_ir_emitter_add_span(emitter, dirty.row, col_start,
                                                 col_end, rgb) == 0) {
                return 0;
            }
            col_start = col_end;
        }
    }
    return 1;
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
    size_t index = 0U;
    if (checkpoint_pixel_index(checkpoint, row, col, &index) == 0) {
        out_rgb[0] = 0.0;
        out_rgb[1] = 0.0;
        out_rgb[2] = 0.0;
        return;
    }
    const void *source = checkpoint->surface.pixels;
    if (checkpoint->overlay_generation[index] ==
        checkpoint->active_overlay_generation) {
        source = checkpoint->overlay_pixels;
    }
    db_rgb_pixels_read_index_f64(source, checkpoint->surface.format, index,
                                 out_rgb);
}

int db_benchmark_checkpoint_commit(db_benchmark_checkpoint_t *checkpoint,
                                   const db_frame_plan_t *plan,
                                   const db_render_result_t *result) {
    if ((checkpoint == NULL) || (plan == NULL) || (result == NULL) ||
        (result->success == 0)) {
        return 0;
    }
    if (checkpoint->enabled == 0) {
        return 1;
    }
    if (checkpoint->overlay_valid == 0) {
        return 0;
    }
    if (checkpoint->content_revision == UINT64_MAX) {
        checkpoint->overlay_valid = 0;
        return 0;
    }
    if (checkpoint_commit_overlay(checkpoint) == 0) {
        const db_log_field_t fields[] = {
            DB_LOG_TOKEN("code", "checkpoint_update_rejected"),
            DB_LOG_U64("frame", plan->frame_index),
        };
        db_log_error(DB_CHECKPOINT_BACKEND, "checkpoint_error", fields,
                     DB_LOG_FIELD_COUNT(fields));
        checkpoint->overlay_valid = 0;
        return 0;
    }
    checkpoint->content_revision =
        db_checked_add_u64(DB_CHECKPOINT_BACKEND, "content_revision",
                           checkpoint->content_revision, 1U);
    checkpoint->committed_frame_index = plan->frame_index;
    checkpoint->committed_frame_valid = 1;
    if (result->working_hash_valid != 0) {
        const size_t row_stride_bytes = db_checked_mul_size(
            DB_CHECKPOINT_BACKEND, "checkpoint hash row stride",
            checkpoint->surface.pixel_width,
            db_pixel_surface_pixel_bytes(&checkpoint->surface));
        const uint64_t checkpoint_hash = db_hash_working_rgba8(
            checkpoint->surface.pixels, checkpoint->surface.format,
            checkpoint->surface.pixel_width, checkpoint->surface.pixel_height,
            row_stride_bytes, 0);
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
    return 1;
}
