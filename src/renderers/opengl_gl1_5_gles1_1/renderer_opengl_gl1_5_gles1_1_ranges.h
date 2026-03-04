#ifndef DRIVERBENCH_RENDERER_OPENGL_GL1_5_GLES1_1_RANGES_H
#define DRIVERBENCH_RENDERER_OPENGL_GL1_5_GLES1_1_RANGES_H

#include <stddef.h>

#include "../renderer_gl_common.h"

size_t db_gl1_collapse_upload_ranges_covering_span(db_gl_upload_range_t *ranges,
                                                   size_t range_count);

size_t db_gl1_optimize_upload_ranges(db_gl_upload_range_t *ranges,
                                     size_t range_count, int allow_overdraw,
                                     size_t collapse_threshold);

#endif
