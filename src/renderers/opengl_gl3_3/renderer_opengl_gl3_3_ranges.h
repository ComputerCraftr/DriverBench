#ifndef RENDERER_OPENGL_GL3_3_RANGES_H
#define RENDERER_OPENGL_GL3_3_RANGES_H

#include <stddef.h>
#include <stdint.h>

#include "../renderer_benchmark_common.h"
#include "../renderer_snake_common.h"

int db_gl3_step_span_row_range(const char *backend_name, uint32_t region_width,
                               uint32_t region_height, uint32_t span_start,
                               uint32_t span_count,
                               db_dirty_row_range_t *out_range);

size_t db_gl3_collect_snake_dirty_rows(const char *backend_name,
                                       const db_snake_plan_t *plan,
                                       const db_snake_region_t *region,
                                       db_dirty_row_range_t out[4]);

#endif
