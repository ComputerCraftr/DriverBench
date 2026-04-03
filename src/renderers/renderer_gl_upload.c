#include "../core/db_buffer_convert.h"
#include "renderer_gl_api.h"
#include "renderer_gl_common.h"
#include "renderer_gl_upload_internal.h"

#include <stddef.h>
#include <stdint.h>

static void db_gl_upload_buffer_subdata_target(const db_gl_upload_ops_t *ops,
                                               GLenum target,
                                               const void *source,
                                               size_t dst_offset_bytes,
                                               size_t size_bytes) {
    if ((ops == NULL) || (ops->buffer_sub_data == NULL) || (source == NULL) ||
        (size_bytes == 0U)) {
        return;
    }
    ops->buffer_sub_data(target, (GLintptr)dst_offset_bytes,
                         (GLsizeiptr)size_bytes, source);
}

static int db_gl_upload_buffer_map_range_target(const db_gl_upload_ops_t *ops,
                                                GLenum target,
                                                const void *source,
                                                size_t dst_offset_bytes,
                                                size_t size_bytes) {
    if ((ops == NULL) || (ops->map_buffer_range == NULL) ||
        (ops->unmap_buffer == NULL) || (source == NULL) || (size_bytes == 0U)) {
        return 0;
    }
    void *mapped = ops->map_buffer_range(
        target, (GLintptr)dst_offset_bytes, (GLsizeiptr)size_bytes,
        GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT |
            GL_MAP_UNSYNCHRONIZED_BIT);
    if ((mapped == NULL) || (db_gl_internal_get_error_value() != GL_NO_ERROR)) {
        return 0;
    }
    db_copy_bytes(mapped, source, size_bytes);
    if ((ops->unmap_buffer(target) == GL_FALSE) ||
        (db_gl_internal_get_error_value() != GL_NO_ERROR)) {
        return 0;
    }
    return 1;
}

static void db_gl_upload_buffer_span_target(
    const void *source, size_t total_bytes, size_t dst_offset_bytes,
    size_t size_bytes, db_gl_upload_target_t target, unsigned int target_buffer,
    int use_persistent_upload, void *persistent_mapped_ptr,
    int use_map_range_upload, int use_map_buffer_upload) {
    if ((source == NULL) || (size_bytes == 0U) ||
        (dst_offset_bytes > (SIZE_MAX - size_bytes)) ||
        ((total_bytes != 0U) &&
         ((dst_offset_bytes + size_bytes) > total_bytes))) {
        return;
    }

    const int is_vbo = (target == DB_GL_UPLOAD_TARGET_VBO_ARRAY_BUFFER) ? 1 : 0;
    const GLenum gl_target = (target == DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER)
                                 ? GL_PIXEL_UNPACK_BUFFER
                                 : GL_ARRAY_BUFFER;

    if ((is_vbo != 0) && (use_persistent_upload != 0) &&
        (persistent_mapped_ptr != NULL)) {
        db_copy_bytes(((uint8_t *)persistent_mapped_ptr) + dst_offset_bytes,
                      source, size_bytes);
        return;
    }

    db_gl_internal_require_upload_ready("db_gl_upload_buffer_span_target");
    const db_gl_upload_ops_t ops = db_gl_internal_get_upload_ops();
    if (ops.buffer_sub_data == NULL) {
        return;
    }

    if (is_vbo == 0) {
        const size_t max_gl_buffer_bytes = PTRDIFF_MAX;
        if ((target_buffer == 0U) || (ops.bind_buffer == NULL) ||
            (ops.buffer_data == NULL) || (total_bytes > max_gl_buffer_bytes)) {
            return;
        }
        ops.bind_buffer(gl_target, (GLuint)target_buffer);
        ops.buffer_data(gl_target, (GLsizeiptr)total_bytes, NULL,
                        GL_STREAM_DRAW);
    }

    if ((use_map_range_upload != 0) &&
        db_gl_upload_buffer_map_range_target(&ops, gl_target, source,
                                             dst_offset_bytes, size_bytes)) {
        return;
    }

    const int can_try_whole_buffer_map =
        (use_map_buffer_upload != 0) && (ops.map_buffer != NULL) &&
        (ops.unmap_buffer != NULL) && (dst_offset_bytes == 0U) &&
        (size_bytes == total_bytes);
    if (can_try_whole_buffer_map != 0) {
        void *mapped = ops.map_buffer(gl_target, GL_WRITE_ONLY);
        if (mapped != NULL) {
            db_copy_bytes(mapped, source, size_bytes);
            if (ops.unmap_buffer(gl_target) == GL_FALSE) {
                db_gl_upload_buffer_subdata_target(
                    &ops, gl_target, source, dst_offset_bytes, size_bytes);
            }
            return;
        }
    }

    if ((use_map_buffer_upload != 0) && (ops.map_buffer != NULL) &&
        (ops.unmap_buffer != NULL) && (total_bytes > 0U)) {
        void *mapped = ops.map_buffer(gl_target, GL_WRITE_ONLY);
        if (mapped != NULL) {
            db_copy_bytes(((uint8_t *)mapped) + dst_offset_bytes, source,
                          size_bytes);
            if (ops.unmap_buffer(gl_target) == GL_FALSE) {
                db_gl_upload_buffer_subdata_target(
                    &ops, gl_target, source, dst_offset_bytes, size_bytes);
            }
            return;
        }
    }

    db_gl_upload_buffer_subdata_target(&ops, gl_target, source,
                                       dst_offset_bytes, size_bytes);
}

void db_gl_upload_buffer_target(const void *source, size_t bytes,
                                db_gl_upload_target_t target,
                                unsigned int target_buffer,
                                int use_persistent_upload,
                                void *persistent_mapped_ptr,
                                int use_map_range_upload,
                                int use_map_buffer_upload) {
    db_gl_upload_buffer_span_target(
        source, bytes, 0U, bytes, target, target_buffer, use_persistent_upload,
        persistent_mapped_ptr, use_map_range_upload, use_map_buffer_upload);
}

void db_gl_upload_buffer(const void *source, size_t bytes,
                         int use_persistent_upload, void *persistent_mapped_ptr,
                         int use_map_range_upload, int use_map_buffer_upload) {
    db_gl_upload_buffer_target(source, bytes,
                               DB_GL_UPLOAD_TARGET_VBO_ARRAY_BUFFER, 0U,
                               use_persistent_upload, persistent_mapped_ptr,
                               use_map_range_upload, use_map_buffer_upload);
}

int db_gl_upload_compact_prepared(const db_gl_compact_vbo_state_t *compact,
                                  const db_gl_upload_probe_result_t *upload,
                                  size_t compact_bytes) {
    if ((compact == NULL) || (upload == NULL) || (compact_bytes == 0U) ||
        (compact->scratch_vertices == NULL) ||
        (compact->vbo_capacity_bytes == 0U) ||
        (compact_bytes > compact->vbo_capacity_bytes) ||
        (compact->vbo_offset_bytes > (SIZE_MAX - compact_bytes))) {
        return 0;
    }
    db_gl_upload_buffer_span_target(
        compact->scratch_vertices, compact->vbo_offset_bytes + compact_bytes,
        compact->vbo_offset_bytes, compact_bytes,
        DB_GL_UPLOAD_TARGET_VBO_ARRAY_BUFFER, 0U, upload->use_persistent_upload,
        upload->persistent_mapped_ptr, upload->use_map_range_upload,
        upload->use_map_buffer_upload);
    return 1;
}

void db_gl_unmap_current_array_buffer(void) {
    db_gl_internal_require_upload_ready("db_gl_unmap_current_array_buffer");
    const db_gl_upload_ops_t ops = db_gl_internal_get_upload_ops();
    if (ops.unmap_buffer != NULL) {
        (void)ops.unmap_buffer(GL_ARRAY_BUFFER);
    }
}
