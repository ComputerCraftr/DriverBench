#ifndef DRIVERBENCH_GL3_EXECUTE_H
#define DRIVERBENCH_GL3_EXECUTE_H

#include "core/db_render_ir.h"
#include "core/db_render_result.h"
#include "core/db_renderer_support.h"
#include "gl3_exact_lookup.h"
#include "gl3_target.h"
#include "renderers/gl_common.h"

#include <stddef.h>
#include <stdint.h>

enum {
    DB_GL3_INSTANCE_FLOAT_COUNT = 16U,
    DB_GL3_UNIT_QUAD_FLOAT_COUNT = 12U,
};

typedef struct {
    db_gl_vertex_init_t vertex;
    size_t instance_capacity;
    unsigned int vao;
    db_gl_buffer_cache_t buffers;
    db_gl_upload_stream_t stream;
} db_gl3_geometry_stream_t;

typedef struct {
    const db_renderer_execution_config_t *runtime;
    const gl3_persistent_target_t *target;
    db_gl3_geometry_stream_t *geometry;
    gl3_exact_lookup_t *exact_lookup;
    unsigned int draw_program;
} db_gl3_execute_context_t;

int db_gl3_geometry_storage_layout(size_t instance_capacity,
                                   size_t *float_count, size_t *byte_count);

void db_gl3_execute_bind_layout(const db_gl3_execute_context_t *context);
uint32_t db_gl3_execute_ir(const db_gl3_execute_context_t *context,
                           const db_render_ir_view_t *first_ir,
                           const db_render_ir_view_t *second_ir,
                           db_render_operation_path_t gradient_path,
                           uint32_t *semantic_gradients,
                           uint32_t *exact_gradients,
                           size_t *lookup_upload_bytes,
                           uint32_t *fallback_instances);

#endif
