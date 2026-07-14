#include "db_gradient_divergence.h"

#include "core/db_byte_codec.h"
#include "core/db_hash.h"
#include "core/db_log.h"
#include "core/db_numeric.h"
#include "core/db_render_ir.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    DB_GRADIENT_DIVERGENCE_HEX_CAPACITY = 9U,
    DB_GRADIENT_DIVERGENCE_LINE_CAPACITY = 2048U,
};

const char *
db_gradient_divergence_stage_name(db_gradient_divergence_stage_t stage) {
    switch (stage) {
    case DB_GRADIENT_DIVERGENCE_NONE:
        return "none";
    case DB_GRADIENT_DIVERGENCE_IR_SEMANTICS:
        return "ir_semantics";
    case DB_GRADIENT_DIVERGENCE_LOWERING:
        return "lowering";
    case DB_GRADIENT_DIVERGENCE_COORDINATE:
        return "coordinate";
    case DB_GRADIENT_DIVERGENCE_COVERAGE:
        return "coverage";
    case DB_GRADIENT_DIVERGENCE_CONVERSION:
        return "conversion";
    case DB_GRADIENT_DIVERGENCE_READBACK:
        return "readback";
    }
    return "unknown";
}

db_gradient_divergence_t
db_gradient_compare_rgba8(const uint8_t *expected, const uint8_t *observed,
                          uint32_t width, uint32_t height,
                          const db_gradient_compare_context_t *context) {
    db_gradient_divergence_t result = {0};
    if (context != NULL) {
        result.context = *context;
        result.command_index = context->command_index;
    }
    if ((expected == NULL) || (observed == NULL)) {
        result.divergent = 1;
        result.stage = DB_GRADIENT_DIVERGENCE_READBACK;
        return result;
    }
    const db_rgba8_pixel_diff_t diff =
        db_rgba8_pixel_diff(expected, observed, width, height);
    if (diff.mismatch_count > 0U) {
        result.divergent = 1;
        result.pixel_x = diff.first_x;
        result.pixel_y = diff.first_y;
        result.row = result.pixel_y;
        memcpy(result.expected_rgba8, diff.expected_rgba,
               sizeof(result.expected_rgba8));
        memcpy(result.observed_rgba8, diff.actual_rgba,
               sizeof(result.observed_rgba8));
        result.component = diff.first_component;
        result.stage = DB_GRADIENT_DIVERGENCE_CONVERSION;
        if ((context != NULL) &&
            (context->expected_sentinel != context->observed_sentinel)) {
            result.stage = DB_GRADIENT_DIVERGENCE_COVERAGE;
        }
        return result;
    }
    return result;
}

int db_gradient_divergence_write(const char *path,
                                 const db_gradient_divergence_t *divergence) {
    if ((path == NULL) || (path[0] == '\0') || (divergence == NULL)) {
        return 0;
    }
    FILE *const output = fopen(path, "ab");
    if (output == NULL) {
        return 0;
    }
    char line[DB_GRADIENT_DIVERGENCE_LINE_CAPACITY];
    char expected_rgba8[DB_GRADIENT_DIVERGENCE_HEX_CAPACITY];
    char observed_rgba8[DB_GRADIENT_DIVERGENCE_HEX_CAPACITY];
    if ((db_hex_encode_lower(divergence->expected_rgba8,
                             sizeof(divergence->expected_rgba8), expected_rgba8,
                             sizeof(expected_rgba8)) == 0) ||
        (db_hex_encode_lower(divergence->observed_rgba8,
                             sizeof(divergence->observed_rgba8), observed_rgba8,
                             sizeof(observed_rgba8)) == 0)) {
        (void)fclose(output);
        return 0;
    }
    const db_log_field_t fields[] = {
        DB_LOG_TOKEN("stage",
                     db_gradient_divergence_stage_name(divergence->stage)),
        DB_LOG_U64("command", divergence->command_index),
        DB_LOG_U64("row", divergence->row),
        DB_LOG_U64("pixel_x", divergence->pixel_x),
        DB_LOG_U64("pixel_y", divergence->pixel_y),
        DB_LOG_U64("component", divergence->component),
        DB_LOG_TOKEN("expected_rgba8", expected_rgba8),
        DB_LOG_TOKEN("observed_rgba8", observed_rgba8),
        DB_LOG_I64("axis_start", divergence->context.axis_start),
        DB_LOG_I64("axis_end", divergence->context.axis_end),
        DB_LOG_BOOL("reverse", divergence->context.reverse_stops),
        DB_LOG_BOOL("expected_sentinel", divergence->context.expected_sentinel),
        DB_LOG_BOOL("observed_sentinel", divergence->context.observed_sentinel),
    };
    const int length = db_log_format_line(
        line, sizeof(line), DB_LOG_LEVEL_INFO,
        &(const db_log_event_t){.component = "conformance_probe",
                                .event = "gradient_divergence",
                                .fields = fields,
                                .field_count = DB_LOG_FIELD_COUNT(fields)});
    const int written =
        ((length > 0) && ((size_t)length < sizeof(line)) &&
         (fwrite(line, 1U, (size_t)length, output) == (size_t)length));
    const int closed = (fclose(output) == 0);
    return written && closed;
}

static db_gradient_vector_t
gradient_vector_at(const db_render_ir_linear_gradient_command_t *gradient,
                   db_render_ir_rect_t clip, int32_t row) {
    const db_render_ir_color_t canonical =
        db_render_ir_linear_gradient_color_at(gradient, row);
    double amount = 0.0;
    if (gradient->axis_end > gradient->axis_start) {
        const int32_t clamped =
            DB_CLAMP(row, gradient->axis_start, gradient->axis_end);
        amount = DB_TO_F64((int64_t)clamped - gradient->axis_start) /
                 DB_TO_F64((int64_t)gradient->axis_end - gradient->axis_start);
    }
    if (gradient->reverse_stops != 0U) {
        amount = 1.0 - amount;
    }
    db_gradient_vector_t vector = {
        .clip = clip,
        .logical_row = row,
        .interpolation_parameter = amount,
    };
    const float amount_f32 = db_double_to_f32(amount);
    for (size_t channel = 0U; channel < 4U; channel++) {
        const float start =
            db_double_to_f32(gradient->start_color.rgba[channel]);
        const float end = db_double_to_f32(gradient->end_color.rgba[channel]);
        vector.canonical_rgba[channel] = canonical.rgba[channel];
        vector.lowered_rgba[channel] = start + ((end - start) * amount_f32);
        vector.rgba16f[channel] =
            db_f64_to_f16_via_f32(canonical.rgba[channel]);
    }
    db_rgba01_to_u8_rgba4(canonical.rgba, vector.rgba8);
    return vector;
}

db_gradient_vector_status_t db_gradient_vector_evaluate(
    const db_render_ir_linear_gradient_command_t *gradient,
    db_render_ir_rect_t clip, int32_t logical_row,
    db_gradient_vector_t *vector) {
    db_render_ir_rect_t clipped = {0};
    if ((gradient == NULL) || (vector == NULL) ||
        (db_render_ir_rect_intersect(gradient->bounds, clip, &clipped) == 0) ||
        (logical_row < clipped.y) ||
        ((int64_t)logical_row >= (int64_t)clipped.y + clipped.height)) {
        return DB_GRADIENT_VECTOR_INVALID;
    }
    *vector = gradient_vector_at(gradient, clipped, logical_row);
    return DB_GRADIENT_VECTOR_OK;
}

db_gradient_vector_status_t db_gradient_vectors_generate(
    const db_render_ir_linear_gradient_command_t *gradient,
    const db_render_ir_rect_t *clips, size_t clip_count,
    db_gradient_vector_t *vectors, size_t vector_capacity,
    size_t *vector_count) {
    if ((gradient == NULL) || (vectors == NULL) || (vector_count == NULL) ||
        ((clip_count > 0U) && (clips == NULL)) ||
        db_render_ir_rect_is_empty(gradient->bounds)) {
        return DB_GRADIENT_VECTOR_INVALID;
    }
    *vector_count = 0U;
    const size_t effective_clip_count = (clip_count == 0U) ? 1U : clip_count;
    for (size_t clip_index = 0U; clip_index < effective_clip_count;
         clip_index++) {
        const db_render_ir_rect_t requested =
            (clip_count == 0U) ? gradient->bounds : clips[clip_index];
        db_render_ir_rect_t clipped = {0};
        if (db_render_ir_rect_intersect(gradient->bounds, requested,
                                        &clipped) == 0) {
            continue;
        }
        const int64_t row_end = (int64_t)clipped.y + clipped.height;
        for (int32_t row = clipped.y; (int64_t)row < row_end; row++) {
            if (*vector_count >= vector_capacity) {
                *vector_count = 0U;
                return DB_GRADIENT_VECTOR_CAPACITY;
            }
            vectors[*vector_count] = gradient_vector_at(gradient, clipped, row);
            (*vector_count)++;
        }
    }
    return DB_GRADIENT_VECTOR_OK;
}
