#include "core/db_core.h"
#include "core/db_log.h"
#include "gl_common.h"

#include <stddef.h>
#include <stdint.h>

int db_gl_upload_compact_stream_write(db_gl_upload_stream_t *stream,
                                      const char *backend,
                                      const db_gl_compact_vbo_state_t *compact,
                                      size_t compact_bytes) {
    if ((stream == NULL) || (backend == NULL) || (compact == NULL) ||
        (compact_bytes == 0U) || (compact->scratch_vertices == NULL) ||
        (compact->vbo_capacity_bytes == 0U) ||
        (db_size_range_fits(compact->vbo_capacity_bytes,
                            compact->vbo_offset_bytes, compact_bytes) == 0)) {
        return 0;
    }
    size_t total_bytes = 0U;
    if (db_try_add_size(compact->vbo_offset_bytes, compact_bytes,
                        &total_bytes) == 0) {
        return 0;
    }
    if (db_gl_stream_upload_uses_buffer_object(&stream->capability) != 0) {
        if ((stream->buffer == 0U) ||
            (stream->buffer_reserved_bytes < total_bytes)) {
            const char *before =
                db_gl_stream_upload_name(&stream->capability, 0, 1);
            db_gl_stream_upload_force_client_fallback(&stream->capability, 1);
            DB_RUNTIME_ERROR(backend,
                             "GL compact upload demoted target=%s from=%s "
                             "reason=preallocated_stream_unavailable "
                             "detail=compact_hot_path",
                             db_gl_upload_target_name(stream->target), before);
            DB_RUNTIME_STATUS(
                backend, "GL upload effective target=%s mode=%s",
                db_gl_upload_target_name(stream->target),
                db_gl_stream_upload_name(&stream->capability, 0, 1));
            return 0;
        }
    }
    return db_gl_upload_stream_write(stream, backend, compact->scratch_vertices,
                                     total_bytes, compact->vbo_offset_bytes,
                                     compact_bytes);
}
