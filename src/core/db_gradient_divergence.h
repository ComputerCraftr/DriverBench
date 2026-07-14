#ifndef DRIVERBENCH_CORE_DB_GRADIENT_DIVERGENCE_H
#define DRIVERBENCH_CORE_DB_GRADIENT_DIVERGENCE_H

#include "core/db_render_ir.h"

#include <stddef.h>
#include <stdint.h>

typedef enum {
    DB_GRADIENT_DIVERGENCE_NONE = 0,
    DB_GRADIENT_DIVERGENCE_IR_SEMANTICS,
    DB_GRADIENT_DIVERGENCE_LOWERING,
    DB_GRADIENT_DIVERGENCE_COORDINATE,
    DB_GRADIENT_DIVERGENCE_COVERAGE,
    DB_GRADIENT_DIVERGENCE_CONVERSION,
    DB_GRADIENT_DIVERGENCE_READBACK,
} db_gradient_divergence_stage_t;

typedef struct {
    db_render_ir_rect_t extent;
    db_render_ir_rect_t clip;
    int32_t axis_start;
    int32_t axis_end;
    uint32_t command_index;
    int reverse_stops;
    int expected_sentinel;
    int observed_sentinel;
} db_gradient_compare_context_t;

typedef struct {
    db_gradient_divergence_stage_t stage;
    uint32_t command_index;
    uint32_t row;
    uint32_t pixel_x;
    uint32_t pixel_y;
    uint32_t component;
    uint8_t expected_rgba8[4];
    uint8_t observed_rgba8[4];
    uint16_t expected_rgba16f[4];
    uint16_t observed_rgba16f[4];
    double canonical_rgba[4];
    float lowered_rgba[4];
    double interpolation_parameter;
    db_gradient_compare_context_t context;
    int divergent;
} db_gradient_divergence_t;

typedef struct {
    db_render_ir_rect_t clip;
    int32_t logical_row;
    double canonical_rgba[4];
    float lowered_rgba[4];
    uint16_t rgba16f[4];
    uint8_t rgba8[4];
    double interpolation_parameter;
} db_gradient_vector_t;

typedef enum {
    DB_GRADIENT_VECTOR_OK = 0,
    DB_GRADIENT_VECTOR_INVALID,
    DB_GRADIENT_VECTOR_CAPACITY,
} db_gradient_vector_status_t;

const char *
db_gradient_divergence_stage_name(db_gradient_divergence_stage_t stage);
db_gradient_divergence_t
db_gradient_compare_rgba8(const uint8_t *expected, const uint8_t *observed,
                          uint32_t width, uint32_t height,
                          const db_gradient_compare_context_t *context);
int db_gradient_divergence_write(const char *path,
                                 const db_gradient_divergence_t *divergence);
db_gradient_vector_status_t db_gradient_vectors_generate(
    const db_render_ir_linear_gradient_command_t *gradient,
    const db_render_ir_rect_t *clips, size_t clip_count,
    db_gradient_vector_t *vectors, size_t vector_capacity,
    size_t *vector_count);
db_gradient_vector_status_t db_gradient_vector_evaluate(
    const db_render_ir_linear_gradient_command_t *gradient,
    db_render_ir_rect_t clip, int32_t logical_row,
    db_gradient_vector_t *vector);

#endif
