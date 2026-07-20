#ifndef DRIVERBENCH_GL3_EXACT_LOOKUP_H
#define DRIVERBENCH_GL3_EXACT_LOOKUP_H

#include "core/db_render_ir.h"
#include "core/db_render_types.h"
#include "renderers/gl_common.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    db_gl_upload_stream_t stream;
    uint32_t *words;
    size_t row_capacity;
    size_t row_count;
    size_t words_per_row;
    unsigned int texture;
    unsigned int program;
    int sampler_location;
    int max_texture_rows;
    db_pixel_format_t format;
    int available;
    const char *unavailable_reason;
} gl3_exact_lookup_t;

int db_gl3_exact_lookup_init(gl3_exact_lookup_t *lookup,
                             db_pixel_format_t format, size_t row_capacity);
int db_gl3_exact_lookup_capacity_supported(size_t required_rows,
                                           int maximum_texels);
void db_gl3_exact_lookup_reset(gl3_exact_lookup_t *lookup);
int db_gl3_exact_lookup_append(
    gl3_exact_lookup_t *lookup,
    const db_render_ir_linear_gradient_command_t *gradient,
    uint32_t *lookup_base);
int db_gl3_exact_lookup_upload(gl3_exact_lookup_t *lookup,
                               size_t *uploaded_bytes);
void db_gl3_exact_lookup_bind(const gl3_exact_lookup_t *lookup);
void db_gl3_exact_lookup_shutdown(gl3_exact_lookup_t *lookup);
uint64_t
db_gl3_exact_lookup_implementation_hash(const gl3_exact_lookup_t *lookup);
uint32_t db_gl3_exact_pack_rgba8(const double *rgba);
void db_gl3_exact_pack_rgba16f(const double *rgba, uint32_t *words);

#endif
