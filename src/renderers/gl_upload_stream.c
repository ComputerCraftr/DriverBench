#include "../core/db_core.h"
#include "../core/db_numeric.h"
#include "../core/db_poll_policy.h"
#include "core/db_log.h"
#include "gl_api.h"
#include "gl_common.h"
#include "gl_probe_internal.h"
#include "gl_proc_runtime.h"
#include "gl_upload_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    DB_GL_UPLOAD_STREAM_PREPARE_RETRY_LIMIT = 3U,
};

static const char *db_gl_client_wait_result_name(GLenum result) {
    switch (result) {
    case GL_ALREADY_SIGNALED:
        return "already_signaled";
    case GL_CONDITION_SATISFIED:
        return "condition_satisfied";
    case GL_TIMEOUT_EXPIRED:
        return "timeout_expired";
    case GL_WAIT_FAILED:
        return "wait_failed";
    default:
        return "unknown";
    }
}

typedef struct {
    GLsync sync;
    uint32_t attempts;
} db_gl_sync_wait_context_t;

static db_sync_wait_result_t db_gl_sync_wait_attempt(void *user_data,
                                                     uint64_t timeout_ns) {
    db_gl_sync_wait_context_t *const context =
        (db_gl_sync_wait_context_t *)user_data;
    const GLbitfield flags =
        (context->attempts++ == 0U) ? GL_SYNC_FLUSH_COMMANDS_BIT : 0U;
    const GLenum result =
        g_upload_proc_table.client_wait_sync(context->sync, flags, timeout_ns);
    if ((result == GL_ALREADY_SIGNALED) || (result == GL_CONDITION_SATISFIED)) {
        return db_sync_wait_result_make(DB_SYNC_WAIT_COMPLETED, 0U, 0U,
                                        (uint32_t)result,
                                        db_gl_client_wait_result_name(result));
    }
    if (result == GL_WAIT_FAILED) {
        return db_sync_wait_result_make(DB_SYNC_WAIT_FAILED, 0U, 0U,
                                        (uint32_t)result, "gl_wait_failed");
    }
    return db_sync_wait_result_make(DB_SYNC_WAIT_TIMEOUT, 0U, 0U,
                                    (uint32_t)result,
                                    db_gl_client_wait_result_name(result));
}

static db_sync_wait_result_t
db_gl_upload_stream_wait_sync_with_policy(GLsync sync,
                                          db_progress_policy_id_t policy_id) {
    if (sync == NULL) {
        return db_sync_wait_result_make(DB_SYNC_WAIT_INVALID, 0U, 0U, 0U,
                                        "missing_sync");
    }
    db_gl_require_upload_proc_table_loaded("db_gl_upload_stream_wait");
    if (g_upload_proc_table.client_wait_sync == NULL) {
        return db_sync_wait_result_make(DB_SYNC_WAIT_UNSUPPORTED, 0U, 0U, 0U,
                                        "client_wait_sync_unavailable");
    }

    db_gl_sync_wait_context_t context = {.sync = sync, .attempts = 0U};
    return db_progress_execute(policy_id, db_gl_sync_wait_attempt, &context);
}

db_sync_wait_result_t
db_gl_upload_stream_probe_sync(void *sync, db_progress_policy_id_t policy_id) {
    return db_gl_upload_stream_wait_sync_with_policy((GLsync)sync, policy_id);
}

int db_gl_upload_stream_bind(const db_gl_upload_stream_t *stream) {
    if (stream == NULL) {
        return 0;
    }
    db_gl_require_upload_proc_table_loaded("db_gl_upload_stream_bind");
    if (g_upload_proc_table.bind_buffer == NULL) {
        return 0;
    }
    g_upload_proc_table.bind_buffer(db_gl_upload_target_gl_enum(stream->target),
                                    (GLuint)stream->buffer);
    return 1;
}

int db_gl_upload_stream_unbind_target(db_gl_upload_target_t target) {
    db_gl_require_upload_proc_table_loaded("db_gl_upload_stream_unbind_target");
    if (g_upload_proc_table.bind_buffer == NULL) {
        return 0;
    }
    g_upload_proc_table.bind_buffer(db_gl_upload_target_gl_enum(target), 0U);
    return 1;
}

static int gl_upload_stream_log_and_demote(db_gl_upload_stream_t *stream,
                                           const char *backend,
                                           db_gl_upload_failure_reason_t reason,
                                           const char *detail,
                                           GLenum gl_error) {
    if ((stream == NULL) || (backend == NULL)) {
        return 0;
    }
    stream->last_failure_gl_error = (uint32_t)gl_error;
    const char *before = db_gl_stream_upload_name(&stream->capability, 0, 1);
    if (db_gl_stream_upload_demote(&stream->capability, reason, 1) == 0) {
        return 0;
    }
    if ((stream->last_logged_failure_reason == reason) &&
        (stream->last_logged_effective_storage ==
         stream->capability.effective_storage) &&
        (stream->last_logged_effective_mode ==
         stream->capability.effective_mode)) {
        return 1;
    }
    if (gl_error != GL_NO_ERROR) {
        DB_RUNTIME_ERROR(
            backend,
            "GL upload demoted target=%s from=%s reason=%s detail=%s "
            "gl_error=0x%04X",
            db_gl_upload_target_name(stream->target), before,
            db_gl_upload_failure_reason_name(reason),
            (detail != NULL) ? detail : "unspecified", (unsigned int)gl_error);
    } else {
        DB_RUNTIME_ERROR(
            backend, "GL upload demoted target=%s from=%s reason=%s detail=%s",
            db_gl_upload_target_name(stream->target), before,
            db_gl_upload_failure_reason_name(reason),
            (detail != NULL) ? detail : "unspecified");
    }
    DB_RUNTIME_STATUS(backend, "GL upload effective target=%s mode=%s",
                      db_gl_upload_target_name(stream->target),
                      db_gl_stream_upload_name(&stream->capability, 0, 1));
    stream->last_logged_failure_reason = reason;
    stream->last_logged_effective_storage =
        stream->capability.effective_storage;
    stream->last_logged_effective_mode = stream->capability.effective_mode;
    return 1;
}

static int gl_upload_stream_alloc_buffer_storage(db_gl_upload_stream_t *stream,
                                                 const char *backend,
                                                 size_t required_bytes) {
    db_gl_require_upload_proc_table_loaded(
        "gl_upload_stream_alloc_buffer_storage");

    if ((stream == NULL) || (backend == NULL) || (stream->buffer == 0U) ||
        (required_bytes == 0U) || (required_bytes > PTRDIFF_MAX) ||
        (g_upload_proc_table.buffer_data == NULL)) {
        return 0;
    }
    if (db_gl_upload_stream_bind(stream) == 0) {
        return 0;
    }
    const GLenum gl_target = db_gl_upload_target_gl_enum(stream->target);
    db_gl_probe_drain_errors();

    if ((db_gl_stream_upload_uses_persistent(&stream->capability) != 0) &&
        (stream->target == DB_GL_UPLOAD_TARGET_VBO_ARRAY_BUFFER) &&
        (g_upload_proc_table.buffer_storage != NULL) &&
        (g_upload_proc_table.map_buffer_range != NULL)) {
        const GLbitfield flags =
            GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
        g_upload_proc_table.buffer_storage(
            gl_target, (GLsizeiptr)required_bytes, NULL, flags);
        GLenum err = db_gl_get_error_value();
        if (err != GL_NO_ERROR) {
            (void)gl_upload_stream_log_and_demote(
                stream, backend, DB_GL_UPLOAD_FAILURE_STORAGE_ALLOC,
                "buffer_storage", err);
        } else {
            stream->persistent_mapping = g_upload_proc_table.map_buffer_range(
                gl_target, 0, (GLsizeiptr)required_bytes, flags);
            err = db_gl_get_error_value();
            if ((stream->persistent_mapping == NULL) || (err != GL_NO_ERROR)) {
                stream->persistent_mapping = NULL;
                (void)gl_upload_stream_log_and_demote(
                    stream, backend, DB_GL_UPLOAD_FAILURE_MAP_NULL,
                    "persistent_map_range", err);
            } else {
                stream->buffer_reserved_bytes = required_bytes;
                stream->initialized_storage_bytes = required_bytes;
                (void)db_gl_upload_stream_unbind_target(stream->target);
                return 1;
            }
        }
    }
    const GLenum gl_usage =
        (stream->target == DB_GL_UPLOAD_TARGET_PBO_PACK_BUFFER)
            ? GL_STREAM_READ
            : GL_STREAM_DRAW;
    g_upload_proc_table.buffer_data(gl_target, (GLsizeiptr)required_bytes, NULL,
                                    gl_usage);
    const GLenum data_err = db_gl_get_error_value();
    if (data_err != GL_NO_ERROR) {
        (void)gl_upload_stream_log_and_demote(
            stream, backend, DB_GL_UPLOAD_FAILURE_STORAGE_ALLOC, "buffer_data",
            data_err);
        (void)db_gl_upload_stream_unbind_target(stream->target);
        return 0;
    }
    stream->buffer_reserved_bytes = required_bytes;
    stream->initialized_storage_bytes = required_bytes;
    (void)db_gl_upload_stream_unbind_target(stream->target);
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
        .owns_storage = DB_BOOL(owns_storage),
        .last_logged_failure_reason = DB_GL_UPLOAD_FAILURE_NONE,
    };
}

void db_gl_upload_stream_log_selection(const db_gl_upload_stream_t *stream,
                                       const char *backend, const char *role) {
    if ((stream == NULL) || (backend == NULL) || (role == NULL)) {
        return;
    }
    const db_gl_stream_upload_capability_t *const capability =
        &stream->capability;
    const db_log_field_t fields[] = {
        DB_LOG_TOKEN("role", role),
        DB_LOG_TOKEN("target", db_gl_upload_target_name(stream->target)),
        DB_LOG_TOKEN("requested_storage", db_gl_stream_upload_storage_name(
                                              capability->requested_storage)),
        DB_LOG_TOKEN("supported_storage", db_gl_stream_upload_storage_name(
                                              capability->supported_storage)),
        DB_LOG_TOKEN("effective_storage", db_gl_stream_upload_storage_name(
                                              capability->effective_storage)),
        DB_LOG_TOKEN("requested_mode",
                     db_gl_stream_upload_mode_name(capability->requested_mode)),
        DB_LOG_TOKEN("supported_mode",
                     db_gl_stream_upload_mode_name(capability->supported_mode)),
        DB_LOG_TOKEN("effective_mode",
                     db_gl_stream_upload_mode_name(capability->effective_mode)),
        DB_LOG_BOOL("buffer_created", stream->buffer != 0U),
        DB_LOG_BOOL("mapping_probe_attempted",
                    capability->mapping_probe_attempted),
        DB_LOG_BOOL("mapping_validated", capability->mapping_validated),
        DB_LOG_BOOL("canary_validated", capability->canary_validated),
        DB_LOG_BOOL("sync_enabled", capability->sync_enabled),
        DB_LOG_TOKEN("demotion_reason", db_gl_upload_failure_reason_name(
                                            capability->demotion_reason)),
    };
    db_log_info(backend, "gl_buffer_selection", fields,
                DB_LOG_FIELD_COUNT(fields));
}

int db_gl_upload_stream_create_owned_buffer(db_gl_upload_stream_t *stream,
                                            const char *backend) {
    if ((stream == NULL) || (backend == NULL)) {
        return 0;
    }
    if ((stream->owns_storage == 0) ||
        (db_gl_stream_upload_uses_buffer_object(&stream->capability) == 0)) {
        return 1;
    }
    if (stream->buffer != 0U) {
        return 1;
    }
    if (db_gl_vbo_create_or_zero(&stream->buffer) != 0) {
        return 1;
    }
    (void)gl_upload_stream_log_and_demote(stream, backend,
                                          DB_GL_UPLOAD_FAILURE_TARGET_ACQUIRE,
                                          "buffer_create", GL_NO_ERROR);
    return 0;
}

int db_gl_geometry_stream_init(db_gl_upload_stream_t *stream,
                               db_gl_geometry_stream_init_result_t *result,
                               const char *backend, size_t storage_bytes,
                               const float *probe_seed_vertices,
                               const float *initial_vertices,
                               size_t initial_seed_bytes, int enable_sync,
                               int allow_client_array_fallback) {
    if ((stream == NULL) || (result == NULL) || (backend == NULL) ||
        (storage_bytes == 0U)) {
        return 0;
    }

    db_gl_probe_drain_errors();
    *result = (db_gl_geometry_stream_init_result_t){0};
    static const float
        k_fallback_seed[DB_GL_PROBE_PREFIX_BYTES / sizeof(float)] = {0};
    const float *seed_ptr =
        (probe_seed_vertices != NULL) ? probe_seed_vertices : k_fallback_seed;
    result->capability = db_gl_stream_upload_capability_probe(
        DB_GL_UPLOAD_TARGET_VBO_ARRAY_BUFFER, storage_bytes, seed_ptr,
        enable_sync);

    unsigned int geometry_buffer = 0U;
    if (db_gl_vbo_create_or_zero(&geometry_buffer) != 0) {
        db_gl_upload_stream_init(stream, DB_GL_UPLOAD_TARGET_VBO_ARRAY_BUFFER,
                                 result->capability, geometry_buffer, 1);
        stream->hot_path_fixed_capacity_bytes = storage_bytes;
        if (db_gl_upload_stream_prepare_storage(stream, backend,
                                                storage_bytes) == 0) {
            db_gl_upload_stream_shutdown(stream);
            // Update capability in result after potential demotion during
            // preparation
            result->capability = stream->capability;
        }
    }

    if (stream->buffer != 0U) {
        if ((initial_vertices != NULL) && (initial_seed_bytes > 0U) &&
            (db_gl_upload_stream_write(stream, backend, initial_vertices,
                                       storage_bytes, 0U,
                                       initial_seed_bytes) == 0)) {
            db_gl_upload_stream_shutdown(stream);
            return 0;
        }
        return 1;
    }

    if (allow_client_array_fallback == 0) {
        return 0;
    }

    db_gl_stream_upload_force_client_fallback(&result->capability, 1);
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
    stream->active_bytes = required_bytes;
    stream->capability.staging_storage_bytes = required_bytes;
    for (uint32_t retry = 0U; retry < DB_GL_UPLOAD_STREAM_PREPARE_RETRY_LIMIT;
         retry++) {
        db_gl_probe_drain_errors();
        const int needs_client_staging =
            (db_gl_stream_upload_uses_buffer_object(&stream->capability) ==
             0) ||
            (stream->capability.effective_mode ==
             DB_GL_STREAM_UPLOAD_MODE_SUB_DATA);
        if (needs_client_staging != 0) {
            if ((stream->client_storage == NULL) ||
                (stream->client_reserved_bytes < required_bytes)) {
                void *resized = realloc(stream->client_storage, required_bytes);
                if ((resized == NULL) && (required_bytes > 0U)) {
                    (void)gl_upload_stream_log_and_demote(
                        stream, backend, DB_GL_UPLOAD_FAILURE_STORAGE_ALLOC,
                        "client_staging_realloc", GL_NO_ERROR);
                    return 0;
                }
                stream->client_storage = resized;
                stream->client_reserved_bytes = required_bytes;
            }
            if (db_gl_stream_upload_uses_buffer_object(&stream->capability) ==
                0) {
                return 1;
            }
        }
        if ((stream->hot_path_fixed_capacity_bytes != 0U) &&
            (required_bytes > stream->hot_path_fixed_capacity_bytes)) {
            (void)gl_upload_stream_log_and_demote(
                stream, backend, DB_GL_UPLOAD_FAILURE_STORAGE_ALLOC,
                "hot_path_capacity_exceeded", GL_NO_ERROR);
            return 0;
        }
        if ((stream->buffer == 0U) && (db_gl_stream_upload_uses_buffer_object(
                                           &stream->capability) != 0)) {
            (void)gl_upload_stream_log_and_demote(
                stream, backend, DB_GL_UPLOAD_FAILURE_TARGET_ACQUIRE,
                "buffer_create", GL_NO_ERROR);
            if (db_gl_stream_upload_uses_buffer_object(&stream->capability) ==
                0) {
                continue;
            }
            return 0;
        }
        if (stream->buffer_reserved_bytes >= required_bytes) {
            return 1;
        }
        if (gl_upload_stream_alloc_buffer_storage(stream, backend,
                                                  required_bytes) != 0) {
            return 1;
        }
        // If allocation failed, it might have demoted to client storage.
        // Continue loop to allow client storage allocation if needed.
        if (db_gl_stream_upload_uses_buffer_object(&stream->capability) == 0) {
            continue;
        }
        return 0;
    }
    (void)gl_upload_stream_log_and_demote(
        stream, backend, DB_GL_UPLOAD_FAILURE_STORAGE_ALLOC,
        "prepare_storage_retry_exhausted", GL_NO_ERROR);
    return 0;
}

uint8_t *db_gl_upload_stream_begin_write(db_gl_upload_stream_t *stream,
                                         const char *backend,
                                         size_t offset_bytes,
                                         size_t size_bytes) {
    if ((stream == NULL) || (size_bytes == 0U) ||
        ((stream->active_bytes != 0U) &&
         (db_size_range_fits(stream->active_bytes, offset_bytes, size_bytes) ==
          0))) {
        return NULL;
    }
    if (db_gl_stream_upload_uses_buffer_object(&stream->capability) == 0) {
        if (stream->client_storage == NULL) {
            return NULL;
        }
        return ((uint8_t *)stream->client_storage) + offset_bytes;
    }
    if ((db_gl_stream_upload_uses_persistent(&stream->capability) != 0) &&
        (stream->persistent_mapping != NULL)) {
        return ((uint8_t *)stream->persistent_mapping) + offset_bytes;
    }

    db_gl_require_upload_proc_table_loaded("db_gl_upload_stream_begin_write");

    if (db_gl_upload_stream_bind(stream) == 0) {
        (void)gl_upload_stream_log_and_demote(
            stream, backend, DB_GL_UPLOAD_FAILURE_API_UNAVAILABLE,
            "bind_target", GL_NO_ERROR);
        return NULL;
    }
    const GLenum gl_target = db_gl_upload_target_gl_enum(stream->target);
    if ((db_gl_stream_upload_uses_map_range(&stream->capability) != 0) &&
        (g_upload_proc_table.map_buffer_range != NULL)) {
        db_gl_probe_drain_errors();
        GLbitfield map_flags = GL_MAP_WRITE_BIT;
        if (stream->target == DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER) {
            map_flags |= GL_MAP_INVALIDATE_BUFFER_BIT;
        }
        void *mapped = g_upload_proc_table.map_buffer_range(
            gl_target, (GLintptr)offset_bytes, (GLsizeiptr)size_bytes,
            map_flags);
        GLenum err = db_gl_get_error_value();
        if ((mapped == NULL) || (err != GL_NO_ERROR)) {
            if (mapped != NULL) {
                (void)g_upload_proc_table.unmap_buffer(gl_target);
            }
            (void)gl_upload_stream_log_and_demote(stream, backend,
                                                  DB_GL_UPLOAD_FAILURE_MAP_NULL,
                                                  "map_buffer_range", err);
            return NULL;
        }
        stream->mapping_active = 1;
        return (uint8_t *)mapped;
    }
    if ((db_gl_stream_upload_uses_map_buffer(&stream->capability) != 0) &&
        (g_upload_proc_table.map_buffer != NULL)) {
        db_gl_probe_drain_errors();
        void *mapped = g_upload_proc_table.map_buffer(gl_target, GL_WRITE_ONLY);
        GLenum err = db_gl_get_error_value();
        if ((mapped == NULL) || (err != GL_NO_ERROR)) {
            if (mapped != NULL) {
                (void)g_upload_proc_table.unmap_buffer(gl_target);
            }
            (void)gl_upload_stream_log_and_demote(stream, backend,
                                                  DB_GL_UPLOAD_FAILURE_MAP_NULL,
                                                  "map_buffer", err);
            return NULL;
        }
        stream->mapping_active = 1;
        return ((uint8_t *)mapped) + offset_bytes;
    }
    return NULL;
}
int db_gl_upload_stream_end_write(db_gl_upload_stream_t *stream,
                                  const char *backend) {
    if ((stream == NULL) || (stream->mapping_active == 0) ||
        (db_gl_stream_upload_uses_buffer_object(&stream->capability) == 0) ||
        (db_gl_stream_upload_uses_persistent(&stream->capability) != 0)) {
        return 1;
    }
    db_gl_require_upload_proc_table_loaded("db_gl_upload_stream_end_write");

    if (g_upload_proc_table.unmap_buffer != NULL) {
        if (db_gl_upload_stream_bind(stream) == 0) {
            return 0;
        }
        if (g_upload_proc_table.unmap_buffer(
                db_gl_upload_target_gl_enum(stream->target)) == GL_FALSE) {
            stream->mapping_active = 0;
            (void)gl_upload_stream_log_and_demote(
                stream, backend, DB_GL_UPLOAD_FAILURE_UNMAP_FAILED,
                "unmap_buffer", db_gl_get_error_value());
            return 0;
        }
    }
    (void)db_gl_upload_stream_unbind_target(stream->target);
    stream->mapping_active = 0;
    return 1;
}

uint8_t *db_gl_upload_stream_begin_read(db_gl_upload_stream_t *stream,
                                        const char *backend,
                                        size_t offset_bytes,
                                        size_t size_bytes) {
    if ((stream == NULL) || (size_bytes == 0U) ||
        ((stream->active_bytes != 0U) &&
         (db_size_range_fits(stream->active_bytes, offset_bytes, size_bytes) ==
          0))) {
        return NULL;
    }
    if (db_gl_stream_upload_uses_buffer_object(&stream->capability) == 0) {
        if (stream->client_storage == NULL) {
            return NULL;
        }
        return ((uint8_t *)stream->client_storage) + offset_bytes;
    }
    db_gl_require_upload_proc_table_loaded("db_gl_upload_stream_begin_read");

    if (db_gl_upload_stream_bind(stream) == 0) {
        return NULL;
    }
    const GLenum gl_target = db_gl_upload_target_gl_enum(stream->target);
    if ((db_gl_stream_upload_uses_map_range(&stream->capability) != 0) &&
        (g_upload_proc_table.map_buffer_range != NULL)) {
        db_gl_probe_drain_errors();
        void *mapped = g_upload_proc_table.map_buffer_range(
            gl_target, (GLintptr)offset_bytes, (GLsizeiptr)size_bytes,
            GL_MAP_READ_BIT);
        GLenum err = db_gl_get_error_value();
        if ((mapped != NULL) && (err == GL_NO_ERROR)) {
            stream->mapping_active = 1;
            return (uint8_t *)mapped;
        }
        if (mapped != NULL) {
            (void)g_upload_proc_table.unmap_buffer(gl_target);
        }
        (void)gl_upload_stream_log_and_demote(stream, backend,
                                              DB_GL_UPLOAD_FAILURE_MAP_NULL,
                                              "map_buffer_read", err);
    }
    if ((db_gl_stream_upload_uses_map_buffer(&stream->capability) != 0) &&
        (g_upload_proc_table.map_buffer != NULL)) {
        db_gl_probe_drain_errors();
        void *mapped = g_upload_proc_table.map_buffer(gl_target, GL_READ_ONLY);
        GLenum err = db_gl_get_error_value();
        if ((mapped != NULL) && (err == GL_NO_ERROR)) {
            stream->mapping_active = 1;
            return ((uint8_t *)mapped) + offset_bytes;
        }
        if (mapped != NULL) {
            (void)g_upload_proc_table.unmap_buffer(gl_target);
        }
        (void)gl_upload_stream_log_and_demote(stream, backend,
                                              DB_GL_UPLOAD_FAILURE_MAP_NULL,
                                              "map_buffer_read_fallback", err);
    }
    return NULL;
}

int db_gl_upload_stream_end_read(db_gl_upload_stream_t *stream,
                                 const char *backend) {
    return db_gl_upload_stream_end_write(stream, backend);
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
    void *mapped = db_gl_upload_stream_begin_write(
        stream, backend, dst_offset_bytes, size_bytes);
    if (mapped != NULL) {
        memcpy(mapped, source, size_bytes);
        if (db_gl_upload_stream_end_write(stream, backend) == 0) {
            return 0;
        }
        return 1;
    }
    if (db_gl_stream_upload_uses_buffer_object(&stream->capability) == 0) {
        return 0;
    }
    db_gl_require_upload_proc_table_loaded("db_gl_upload_stream_write");

    if ((g_upload_proc_table.buffer_sub_data == NULL) ||
        (db_gl_upload_stream_bind(stream) == 0)) {
        (void)gl_upload_stream_log_and_demote(
            stream, backend, DB_GL_UPLOAD_FAILURE_API_UNAVAILABLE,
            "buffer_sub_data", GL_NO_ERROR);
        return 0;
    }
    g_upload_proc_table.buffer_sub_data(
        db_gl_upload_target_gl_enum(stream->target), (GLintptr)dst_offset_bytes,
        (GLsizeiptr)size_bytes, source);
    GLenum err = db_gl_get_error_value();
    if (err != GL_NO_ERROR) {
        (void)gl_upload_stream_log_and_demote(
            stream, backend, DB_GL_UPLOAD_FAILURE_API_UNAVAILABLE,
            "buffer_sub_data_error", err);
        return 0;
    }
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

int db_gl_upload_stream_wait(db_gl_upload_stream_t *stream) {
    if ((stream == NULL) || (stream->in_flight_sync == NULL) ||
        (db_gl_stream_upload_sync_enabled(&stream->capability) == 0)) {
        return 1;
    }
    const db_sync_wait_result_t result =
        db_gl_upload_stream_wait_sync_with_policy(
            (GLsync)stream->in_flight_sync, DB_PROGRESS_GL_UPLOAD_REUSE);
    db_progress_log_outcome("renderer_gl_upload_stream", "upload_stream_wait",
                            DB_PROGRESS_GL_UPLOAD_REUSE, &result);
    if (result.status == DB_SYNC_WAIT_UNSUPPORTED) {
        DB_RUNTIME_STATUS(
            "renderer_gl_upload_stream",
            "GL sync wait unsupported target=%s operation=%s reason=%s",
            db_gl_upload_target_name(stream->target), "upload_stream_wait",
            result.reason);
        if (g_upload_proc_table.delete_sync != NULL) {
            g_upload_proc_table.delete_sync((GLsync)stream->in_flight_sync);
        }
        stream->in_flight_sync = NULL;
        return 1;
    }
    if (result.status == DB_SYNC_WAIT_INVALID) {
        DB_RUNTIME_FAIL(
            "renderer_gl_upload_stream",
            "invalid GL upload stream sync wait target=%s operation=%s "
            "reason=%s",
            db_gl_upload_target_name(stream->target), "upload_stream_wait",
            result.reason);
    }
    if ((result.status == DB_SYNC_WAIT_TIMEOUT) ||
        (result.status == DB_SYNC_WAIT_FAILED)) {
        return 0;
    }
    if (g_upload_proc_table.delete_sync != NULL) {
        g_upload_proc_table.delete_sync((GLsync)stream->in_flight_sync);
    }
    stream->in_flight_sync = NULL;
    return 1;
}

void db_gl_upload_stream_record_sync(db_gl_upload_stream_t *stream) {
    if ((stream == NULL) ||
        (db_gl_stream_upload_sync_enabled(&stream->capability) == 0)) {
        return;
    }
    db_gl_require_upload_proc_table_loaded("db_gl_upload_stream_record_sync");

    if (g_upload_proc_table.fence_sync == NULL) {
        return;
    }
    if (stream->in_flight_sync != NULL) {
        if ((g_upload_proc_table.client_wait_sync != NULL) &&
            (g_upload_proc_table.delete_sync != NULL)) {
            const db_sync_wait_result_t result =
                db_gl_upload_stream_wait_sync_with_policy(
                    (GLsync)stream->in_flight_sync,
                    DB_PROGRESS_GL_PENDING_SYNC_PROBE);
            if ((result.status == DB_SYNC_WAIT_COMPLETED) ||
                (result.status == DB_SYNC_WAIT_FAILED)) {
                g_upload_proc_table.delete_sync((GLsync)stream->in_flight_sync);
                stream->in_flight_sync = NULL;
            } else {
                DB_RUNTIME_FAIL("renderer_gl_upload_stream",
                                "attempted to overwrite pending stream sync "
                                "operation=%s status=%s polls=%u reason=%s",
                                "upload_stream_pending_probe",
                                db_sync_wait_status_name(result.status),
                                result.attempts, result.reason);
            }
        } else {
            DB_RUNTIME_FAIL(
                "renderer_gl_upload_stream",
                "attempted to overwrite pending stream sync without poll "
                "support");
        }
    }
    stream->in_flight_sync = (void *)g_upload_proc_table.fence_sync(
        GL_SYNC_GPU_COMMANDS_COMPLETE, 0U);
}

void db_gl_upload_stream_shutdown(db_gl_upload_stream_t *stream) {
    if (stream == NULL) {
        return;
    }
    if (stream->in_flight_sync != NULL) {
        if (db_gl_upload_stream_wait(stream) == 0) {
            DB_RUNTIME_FAIL("renderer_gl_upload_stream",
                            "pending upload did not retire before shutdown");
        }
    }
    if ((stream->persistent_mapping != NULL) &&
        (db_gl_stream_upload_uses_buffer_object(&stream->capability) != 0) &&
        (db_gl_stream_upload_uses_persistent(&stream->capability) != 0)) {
        db_gl_require_upload_proc_table_loaded("db_gl_upload_stream_shutdown");

        if ((g_upload_proc_table.unmap_buffer != NULL) &&
            (db_gl_upload_stream_bind(stream) != 0)) {
            (void)g_upload_proc_table.unmap_buffer(
                db_gl_upload_target_gl_enum(stream->target));
        }
        stream->persistent_mapping = NULL;
    }
    if (stream->owns_storage != 0) {
        if (stream->buffer != 0U) {
            db_gl_vbo_delete_if_valid(stream->buffer);
        }
        free(stream->client_storage);
    }
    *stream = (db_gl_upload_stream_t){0};
}
