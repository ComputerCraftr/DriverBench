#include "../core/db_buffer_convert.h"
#include "../core/db_core.h"
#include "renderer_gl_api.h"
#include "renderer_gl_common.h"
#include "renderer_gl_proc_runtime_internal.h"
#include "renderer_gl_upload_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

static GLenum db_gl_upload_target_gl_enum(db_gl_upload_target_t target) {
    return (target == DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER)
               ? GL_PIXEL_UNPACK_BUFFER
               : GL_ARRAY_BUFFER;
}

static int db_gl_upload_stream_bind(const db_gl_upload_stream_t *stream) {
    if (stream == NULL) {
        return 0;
    }
    if (stream->target == DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER) {
        return db_gl_bind_unpack_buffer_cached(stream->buffer, NULL);
    }
    return db_gl_bind_array_buffer_cached(stream->buffer, NULL);
}

static int
db_gl_upload_stream_alloc_buffer_storage(db_gl_upload_stream_t *stream,
                                         size_t required_bytes) {
    db_gl_internal_require_upload_ready(
        "db_gl_upload_stream_alloc_buffer_storage");
    const db_gl_upload_ops_t ops = db_gl_internal_get_upload_ops();
    if ((stream == NULL) || (stream->buffer == 0U) || (required_bytes == 0U) ||
        (required_bytes > PTRDIFF_MAX) || (ops.buffer_data == NULL)) {
        return 0;
    }
    if (db_gl_upload_stream_bind(stream) == 0) {
        return 0;
    }
    const GLenum gl_target = db_gl_upload_target_gl_enum(stream->target);
    if ((db_gl_stream_upload_uses_persistent(&stream->capability) != 0) &&
        (stream->target == DB_GL_UPLOAD_TARGET_VBO_ARRAY_BUFFER) &&
        (ops.buffer_storage != NULL) && (ops.map_buffer_range != NULL)) {
        const GLbitfield flags =
            GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
        ops.buffer_storage(gl_target, (GLsizeiptr)required_bytes, NULL, flags);
        if (db_gl_internal_get_error_value() != GL_NO_ERROR) {
            db_gl_stream_upload_disable_persistent_for_target(
                &stream->capability, stream->target);
        } else {
            stream->persistent_mapping = ops.map_buffer_range(
                gl_target, 0, (GLsizeiptr)required_bytes, flags);
            if ((stream->persistent_mapping == NULL) ||
                (db_gl_internal_get_error_value() != GL_NO_ERROR)) {
                stream->persistent_mapping = NULL;
                db_gl_stream_upload_disable_persistent_for_target(
                    &stream->capability, stream->target);
            } else {
                stream->reserved_bytes = required_bytes;
                return 1;
            }
        }
    }
    ops.buffer_data(gl_target, (GLsizeiptr)required_bytes, NULL,
                    GL_STREAM_DRAW);
    if (db_gl_internal_get_error_value() != GL_NO_ERROR) {
        return 0;
    }
    stream->reserved_bytes = required_bytes;
    return 1;
}

void db_gl_upload_stream_init(db_gl_upload_stream_t *stream,
                              db_gl_upload_target_t target,
                              db_gl_stream_upload_capability_t capability,
                              unsigned int buffer, int owns_storage) {
    if (stream == NULL) {
        return;
    }
    *stream = (db_gl_upload_stream_t){
        .target = target,
        .capability = capability,
        .buffer = buffer,
        .owns_storage = (owns_storage != 0) ? 1 : 0,
    };
    db_gl_stream_upload_disable_persistent_for_target(&stream->capability,
                                                      target);
}

int db_gl_geometry_stream_init(db_gl_upload_stream_t *stream,
                               db_gl_geometry_stream_init_result_t *result,
                               const char *backend, size_t storage_bytes,
                               const float *probe_seed_vertices,
                               const void *initial_seed_data,
                               size_t initial_seed_bytes,
                               int allow_client_array_fallback) {
    if ((stream == NULL) || (result == NULL) || (backend == NULL) ||
        (storage_bytes == 0U)) {
        return 0;
    }

    *result = (db_gl_geometry_stream_init_result_t){0};
    db_gl_context_probe_upload_capabilities(storage_bytes, probe_seed_vertices,
                                            &result->probe);
    result->capability = db_gl_stream_upload_capability_from_probe(
        DB_GL_UPLOAD_TARGET_VBO_ARRAY_BUFFER, &result->probe, 0);

    unsigned int vbo_u32 = 0U;
    if (db_gl_vbo_create_or_zero(&vbo_u32) != 0) {
        result->buffer = vbo_u32;
    }
    if (result->buffer != 0U) {
        db_gl_upload_stream_init(stream, DB_GL_UPLOAD_TARGET_VBO_ARRAY_BUFFER,
                                 result->capability, result->buffer, 0);
        if (db_gl_upload_stream_prepare_storage(stream, backend,
                                                storage_bytes) == 0) {
            db_gl_upload_stream_shutdown(stream);
            db_gl_vbo_delete_if_valid(result->buffer);
            result->buffer = 0U;
        }
    }

    if (result->buffer != 0U) {
        if ((initial_seed_data != NULL) && (initial_seed_bytes > 0U) &&
            (db_gl_upload_stream_write(stream, backend, initial_seed_data,
                                       storage_bytes, 0U,
                                       initial_seed_bytes) == 0)) {
            db_gl_upload_stream_shutdown(stream);
            db_gl_vbo_delete_if_valid(result->buffer);
            result->buffer = 0U;
            return 0;
        }
        return 1;
    }

    if (allow_client_array_fallback == 0) {
        return 0;
    }

    db_gl_stream_upload_force_client_fallback(&result->capability, 1);
    result->uses_client_arrays = 1;
    db_gl_upload_stream_init(stream, DB_GL_UPLOAD_TARGET_VBO_ARRAY_BUFFER,
                             result->capability, 0U, 0);
    return 1;
}

int db_gl_upload_stream_prepare_storage(db_gl_upload_stream_t *stream,
                                        const char *backend,
                                        size_t required_bytes) {
    if ((stream == NULL) || (backend == NULL)) {
        return 0;
    }
    if (stream->active_bytes != required_bytes) {
        stream->active_bytes = required_bytes;
    }
    while (1) {
        if (db_gl_stream_upload_uses_buffer_object(&stream->capability) == 0) {
            if ((stream->client_storage == NULL) ||
                (stream->reserved_bytes < required_bytes)) {
                void *resized = realloc(stream->client_storage, required_bytes);
                if ((resized == NULL) && (required_bytes > 0U)) {
                    db_failf(backend,
                             "failed to resize upload_stream_client=%zu",
                             required_bytes);
                }
                stream->client_storage = resized;
                stream->reserved_bytes = required_bytes;
            }
            return 1;
        }
        if ((stream->buffer == 0U) && (stream->owns_storage != 0)) {
            if (stream->target == DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER) {
                stream->buffer = db_gl_pbo_create_or_zero();
            } else {
                (void)db_gl_vbo_create_or_zero(&stream->buffer);
            }
        }
        if (stream->buffer == 0U) {
            db_gl_stream_upload_force_client_fallback(&stream->capability, 1);
            continue;
        }
        if (stream->reserved_bytes >= required_bytes) {
            return 1;
        }
        if (db_gl_upload_stream_alloc_buffer_storage(stream, required_bytes) !=
            0) {
            return 1;
        }
        db_gl_stream_upload_force_client_fallback(&stream->capability, 1);
        if ((stream->owns_storage != 0) && (stream->buffer != 0U)) {
            if (stream->target == DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER) {
                db_gl_pbo_delete_if_valid(stream->buffer);
            } else {
                db_gl_vbo_delete_if_valid(stream->buffer);
            }
            stream->buffer = 0U;
        }
    }
}

void *db_gl_upload_stream_begin_write(db_gl_upload_stream_t *stream,
                                      size_t offset_bytes, size_t size_bytes) {
    if ((stream == NULL) || (size_bytes == 0U) ||
        (offset_bytes > (SIZE_MAX - size_bytes)) ||
        ((stream->active_bytes != 0U) &&
         ((offset_bytes + size_bytes) > stream->active_bytes))) {
        return NULL;
    }
    if (db_gl_stream_upload_uses_buffer_object(&stream->capability) == 0) {
        return ((uint8_t *)stream->client_storage) + offset_bytes;
    }
    if ((db_gl_stream_upload_uses_persistent(&stream->capability) != 0) &&
        (stream->persistent_mapping != NULL)) {
        return ((uint8_t *)stream->persistent_mapping) + offset_bytes;
    }

    db_gl_internal_require_upload_ready("db_gl_upload_stream_begin_write");
    const db_gl_upload_ops_t ops = db_gl_internal_get_upload_ops();
    if (db_gl_upload_stream_bind(stream) == 0) {
        return NULL;
    }
    const GLenum gl_target = db_gl_upload_target_gl_enum(stream->target);
    if ((db_gl_stream_upload_uses_map_range(&stream->capability) != 0) &&
        (ops.map_buffer_range != NULL)) {
        stream->mapping_active = 1;
        return ops.map_buffer_range(
            gl_target, (GLintptr)offset_bytes, (GLsizeiptr)size_bytes,
            GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
    }
    if ((db_gl_stream_upload_uses_map_buffer(&stream->capability) != 0) &&
        (ops.map_buffer != NULL)) {
        void *mapped = ops.map_buffer(gl_target, GL_WRITE_ONLY);
        if (mapped != NULL) {
            stream->mapping_active = 1;
            return ((uint8_t *)mapped) + offset_bytes;
        }
    }
    return NULL;
}

void db_gl_upload_stream_end_write(db_gl_upload_stream_t *stream) {
    if ((stream == NULL) || (stream->mapping_active == 0) ||
        (db_gl_stream_upload_uses_buffer_object(&stream->capability) == 0) ||
        (db_gl_stream_upload_uses_persistent(&stream->capability) != 0)) {
        return;
    }
    db_gl_internal_require_upload_ready("db_gl_upload_stream_end_write");
    const db_gl_upload_ops_t ops = db_gl_internal_get_upload_ops();
    if (ops.unmap_buffer != NULL) {
        (void)ops.unmap_buffer(db_gl_upload_target_gl_enum(stream->target));
    }
    stream->mapping_active = 0;
}

int db_gl_upload_stream_write(db_gl_upload_stream_t *stream,
                              const char *backend, const void *source,
                              size_t total_bytes, size_t dst_offset_bytes,
                              size_t size_bytes) {
    if ((stream == NULL) || (backend == NULL) || (source == NULL) ||
        (size_bytes == 0U)) {
        return 0;
    }
    if (db_gl_upload_stream_prepare_storage(stream, backend, total_bytes) ==
        0) {
        return 0;
    }
    void *mapped =
        db_gl_upload_stream_begin_write(stream, dst_offset_bytes, size_bytes);
    if (mapped != NULL) {
        db_copy_bytes(mapped, source, size_bytes);
        db_gl_upload_stream_end_write(stream);
        return 1;
    }
    if (db_gl_stream_upload_uses_buffer_object(&stream->capability) == 0) {
        return 0;
    }
    db_gl_internal_require_upload_ready("db_gl_upload_stream_write");
    const db_gl_upload_ops_t ops = db_gl_internal_get_upload_ops();
    if ((ops.buffer_sub_data == NULL) ||
        (db_gl_upload_stream_bind(stream) == 0)) {
        return 0;
    }
    ops.buffer_sub_data(db_gl_upload_target_gl_enum(stream->target),
                        (GLintptr)dst_offset_bytes, (GLsizeiptr)size_bytes,
                        source);
    return 1;
}

const void *db_gl_upload_stream_pointer(const db_gl_upload_stream_t *stream,
                                        size_t byte_offset) {
    if (stream == NULL) {
        return NULL;
    }
    if (db_gl_stream_upload_uses_buffer_object(&stream->capability) != 0) {
        return db_gl_vbo_offset_ptr(byte_offset);
    }
    return ((const uint8_t *)stream->client_storage) + byte_offset;
}

void db_gl_upload_stream_wait(db_gl_upload_stream_t *stream) {
    if ((stream == NULL) || (stream->in_flight_sync == NULL) ||
        (db_gl_stream_upload_sync_enabled(&stream->capability) == 0)) {
        return;
    }
    db_gl_internal_require_upload_ready("db_gl_upload_stream_wait");
    const db_gl_upload_ops_t ops = db_gl_internal_get_upload_ops();
    if (ops.client_wait_sync == NULL) {
        return;
    }
    while (1) {
        const GLenum result =
            ops.client_wait_sync((GLsync)stream->in_flight_sync, 0U, 0U);
        if ((result == GL_ALREADY_SIGNALED) ||
            (result == GL_CONDITION_SATISFIED) || (result == GL_WAIT_FAILED)) {
            break;
        }
    }
    if (ops.delete_sync != NULL) {
        ops.delete_sync((GLsync)stream->in_flight_sync);
    }
    stream->in_flight_sync = NULL;
}

void db_gl_upload_stream_record_sync(db_gl_upload_stream_t *stream) {
    if ((stream == NULL) ||
        (db_gl_stream_upload_sync_enabled(&stream->capability) == 0)) {
        return;
    }
    db_gl_internal_require_upload_ready("db_gl_upload_stream_record_sync");
    const db_gl_upload_ops_t ops = db_gl_internal_get_upload_ops();
    if (ops.fence_sync == NULL) {
        return;
    }
    if (stream->in_flight_sync != NULL) {
        if ((ops.client_wait_sync != NULL) && (ops.delete_sync != NULL)) {
            const GLenum result =
                ops.client_wait_sync((GLsync)stream->in_flight_sync, 0U, 0U);
            if ((result == GL_ALREADY_SIGNALED) ||
                (result == GL_CONDITION_SATISFIED) ||
                (result == GL_WAIT_FAILED)) {
                ops.delete_sync((GLsync)stream->in_flight_sync);
                stream->in_flight_sync = NULL;
            } else {
                db_failf("renderer_gl_upload_stream",
                         "attempted to overwrite pending stream sync");
            }
        } else {
            db_failf("renderer_gl_upload_stream",
                     "attempted to overwrite pending stream sync without poll "
                     "support");
        }
    }
    stream->in_flight_sync =
        (void *)ops.fence_sync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0U);
}

void db_gl_upload_stream_shutdown(db_gl_upload_stream_t *stream) {
    if (stream == NULL) {
        return;
    }
    if (stream->in_flight_sync != NULL) {
        db_gl_upload_stream_wait(stream);
    }
    if ((stream->persistent_mapping != NULL) &&
        (db_gl_stream_upload_uses_buffer_object(&stream->capability) != 0) &&
        (db_gl_stream_upload_uses_persistent(&stream->capability) != 0)) {
        db_gl_internal_require_upload_ready("db_gl_upload_stream_shutdown");
        const db_gl_upload_ops_t ops = db_gl_internal_get_upload_ops();
        if ((ops.unmap_buffer != NULL) &&
            (db_gl_upload_stream_bind(stream) != 0)) {
            (void)ops.unmap_buffer(db_gl_upload_target_gl_enum(stream->target));
        }
        stream->persistent_mapping = NULL;
    }
    if (stream->owns_storage != 0) {
        if (stream->buffer != 0U) {
            if (stream->target == DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER) {
                db_gl_pbo_delete_if_valid(stream->buffer);
            } else {
                db_gl_vbo_delete_if_valid(stream->buffer);
            }
        }
        free(stream->client_storage);
    }
    *stream = (db_gl_upload_stream_t){0};
}
