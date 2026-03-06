#include "../core/db_buffer_convert.h"
#include "renderer_gl_api.h"
#include "renderer_gl_common.h"
#include "renderer_gl_upload_internal.h"

#include <stddef.h>
#include <stdint.h>

static void *db_gl_upload_map_target_buffer_if_supported(
    const db_gl_upload_ops_t *ops, GLenum target, size_t bytes,
    int try_map_range, int try_map_buffer) {
    if (ops == NULL) {
        return NULL;
    }
    if ((try_map_range != 0) && (ops->map_buffer_range != NULL)) {
        return ops->map_buffer_range(target, 0, (GLsizeiptr)bytes,
                                     GL_MAP_WRITE_BIT |
                                         GL_MAP_INVALIDATE_BUFFER_BIT |
                                         GL_MAP_UNSYNCHRONIZED_BIT);
    }

    if ((try_map_buffer != 0) && (ops->map_buffer != NULL)) {
        return ops->map_buffer(target, GL_WRITE_ONLY);
    }

    return NULL;
}

static void db_gl_upload_ranges_subdata_target(
    const db_gl_upload_ops_t *ops, GLenum target, const void *source_base,
    const db_gl_upload_range_t *ranges, size_t range_count) {
    if ((ops == NULL) || (ops->buffer_sub_data == NULL)) {
        return;
    }
    const uint8_t *src_base = (const uint8_t *)source_base;
    for (size_t i = 0U; i < range_count; i++) {
        const db_gl_upload_range_t *range = &ranges[i];
        ops->buffer_sub_data(target, (GLintptr)range->dst_offset_bytes,
                             (GLsizeiptr)range->size_bytes,
                             src_base + range->src_offset_bytes);
    }
}

static int db_gl_upload_ranges_map_range_target(
    const db_gl_upload_ops_t *ops, GLenum target, const void *source_base,
    const db_gl_upload_range_t *ranges, size_t range_count) {
    if ((ops == NULL) || (ops->map_buffer_range == NULL) ||
        (ops->unmap_buffer == NULL)) {
        return 0;
    }

    const uint8_t *src_base = (const uint8_t *)source_base;
    for (size_t i = 0U; i < range_count; i++) {
        const db_gl_upload_range_t *range = &ranges[i];
        if (range->size_bytes == 0U) {
            continue;
        }
        void *mapped = ops->map_buffer_range(
            target, (GLintptr)range->dst_offset_bytes,
            (GLsizeiptr)range->size_bytes,
            GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT |
                GL_MAP_UNSYNCHRONIZED_BIT);
        if ((mapped == NULL) ||
            (db_gl_internal_get_error_value() != GL_NO_ERROR)) {
            return 0;
        }
        db_copy_bytes(mapped, src_base + range->src_offset_bytes,
                      range->size_bytes);
        if ((ops->unmap_buffer(target) == GL_FALSE) ||
            (db_gl_internal_get_error_value() != GL_NO_ERROR)) {
            return 0;
        }
    }
    return 1;
}

void db_gl_upload_ranges_target(
    const void *source_base, size_t total_bytes,
    const db_gl_upload_range_t *ranges, size_t range_count,
    db_gl_upload_target_t target, unsigned int target_buffer,
    int use_persistent_upload, void *persistent_mapped_ptr,
    int use_map_range_upload, int use_map_buffer_upload) {
    if ((source_base == NULL) || (ranges == NULL) || (range_count == 0U)) {
        return;
    }

    const int is_vbo = (target == DB_GL_UPLOAD_TARGET_VBO_ARRAY_BUFFER) ? 1 : 0;
    const GLenum gl_target = (target == DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER)
                                 ? GL_PIXEL_UNPACK_BUFFER
                                 : GL_ARRAY_BUFFER;

    if ((is_vbo != 0) && (use_persistent_upload != 0) &&
        (persistent_mapped_ptr != NULL)) {
        uint8_t *dst_base = (uint8_t *)persistent_mapped_ptr;
        const uint8_t *src_base = (const uint8_t *)source_base;
        for (size_t i = 0; i < range_count; i++) {
            const db_gl_upload_range_t *range = &ranges[i];
            db_copy_bytes(dst_base + range->dst_offset_bytes,
                          src_base + range->src_offset_bytes,
                          range->size_bytes);
        }
        return;
    }

    db_gl_internal_require_upload_ready("db_gl_upload_ranges_target");
    const db_gl_upload_ops_t ops = db_gl_internal_get_upload_ops();
    if (ops.buffer_sub_data == NULL) {
        return;
    }

    if (is_vbo == 0) {
        if ((target_buffer == 0U) || (ops.bind_buffer == NULL) ||
            (ops.buffer_data == NULL) || (total_bytes > (size_t)PTRDIFF_MAX)) {
            return;
        }
        ops.bind_buffer(gl_target, (GLuint)target_buffer);
        // PBO path: orphan storage each upload pass to avoid GPU/CPU stalls.
        ops.buffer_data(gl_target, (GLsizeiptr)total_bytes, NULL,
                        GL_STREAM_DRAW);
    }

    if ((use_map_range_upload != 0) &&
        db_gl_upload_ranges_map_range_target(&ops, gl_target, source_base,
                                             ranges, range_count)) {
        return;
    }

    const int can_try_whole_buffer_map =
        (use_map_buffer_upload != 0) && (ops.map_buffer != NULL) &&
        (ops.unmap_buffer != NULL) && (range_count == 1U) &&
        (ranges[0].dst_offset_bytes == 0U) &&
        (ranges[0].size_bytes == total_bytes);
    if (can_try_whole_buffer_map != 0) {
        void *mapped_ptr = db_gl_upload_map_target_buffer_if_supported(
            &ops, gl_target, total_bytes, 0, 1);
        if (mapped_ptr != NULL) {
            db_copy_bytes(mapped_ptr, source_base, total_bytes);
            if (ops.unmap_buffer(gl_target) == GL_FALSE) {
                db_gl_upload_ranges_subdata_target(&ops, gl_target, source_base,
                                                   ranges, range_count);
            }
            return;
        }
    }

    if (use_map_buffer_upload != 0) {
        void *mapped_ptr = db_gl_upload_map_target_buffer_if_supported(
            &ops, gl_target, total_bytes, 0, 1);
        if ((mapped_ptr != NULL) && (ops.unmap_buffer != NULL)) {
            uint8_t *dst_base = (uint8_t *)mapped_ptr;
            const uint8_t *src_base = (const uint8_t *)source_base;
            for (size_t i = 0; i < range_count; i++) {
                const db_gl_upload_range_t *range = &ranges[i];
                db_copy_bytes(dst_base + range->dst_offset_bytes,
                              src_base + range->src_offset_bytes,
                              range->size_bytes);
            }
            if (ops.unmap_buffer(gl_target) == GL_FALSE) {
                db_gl_upload_ranges_subdata_target(&ops, gl_target, source_base,
                                                   ranges, range_count);
            }
            return;
        }
    }

    db_gl_upload_ranges_subdata_target(&ops, gl_target, source_base, ranges,
                                       range_count);
}

void db_gl_upload_buffer(const void *source, size_t bytes,
                         int use_persistent_upload, void *persistent_mapped_ptr,
                         int use_map_range_upload, int use_map_buffer_upload) {
    const db_gl_upload_range_t full_range = {0U, 0U, bytes};
    db_gl_upload_ranges_target(source, bytes, &full_range, 1U,
                               DB_GL_UPLOAD_TARGET_VBO_ARRAY_BUFFER, 0U,
                               use_persistent_upload, persistent_mapped_ptr,
                               use_map_range_upload, use_map_buffer_upload);
}

void db_gl_upload_vbo_damage_ranges(const float *vertices, size_t upload_bytes,
                                    const db_gl_upload_probe_result_t *upload,
                                    const db_gl_upload_range_t *range_storage,
                                    size_t upload_range_count) {
    if ((upload == NULL) || (upload_range_count == 0U)) {
        return;
    }
    db_gl_upload_ranges_target(
        vertices, upload_bytes, range_storage, upload_range_count,
        DB_GL_UPLOAD_TARGET_VBO_ARRAY_BUFFER, 0U, upload->use_persistent_upload,
        upload->persistent_mapped_ptr, upload->use_map_range_upload,
        upload->use_map_buffer_upload);
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
    const db_gl_upload_range_t compact_range = {
        .src_offset_bytes = 0U,
        .dst_offset_bytes = compact->vbo_offset_bytes,
        .size_bytes = compact_bytes,
    };
    db_gl_upload_vbo_damage_ranges(compact->scratch_vertices,
                                   compact->vbo_offset_bytes + compact_bytes,
                                   upload, &compact_range, 1U);
    return 1;
}

void db_gl_unmap_current_array_buffer(void) {
    db_gl_internal_require_upload_ready("db_gl_unmap_current_array_buffer");
    const db_gl_upload_ops_t ops = db_gl_internal_get_upload_ops();
    if (ops.unmap_buffer != NULL) {
        (void)ops.unmap_buffer(GL_ARRAY_BUFFER);
    }
}
