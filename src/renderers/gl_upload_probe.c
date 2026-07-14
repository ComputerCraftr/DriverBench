#include "../core/db_core.h"
#include "../core/db_numeric.h"
#include "core/db_render_types.h"
#include "gl_api.h"
#include "gl_common.h"
#include "gl_probe_internal.h"
#include "gl_proc_runtime.h"
#include "gl_upload_internal.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

size_t db_gl_upload_probe_size_bytes(size_t bytes) {
    if (bytes == 0U) {
        return 0U;
    }
    return DB_MIN(bytes, DB_GL_PROBE_PREFIX_BYTES);
}

static int gl_verify_buffer_prefix_for_target(db_gl_upload_target_t target,
                                              const uint8_t *expected,
                                              size_t expected_size) {
    if ((expected == NULL) || (expected_size == 0U) ||
        (expected_size > DB_GL_PROBE_PREFIX_BYTES)) {
        return 0;
    }

    db_gl_require_upload_proc_table_loaded("db_gl_verify_buffer_prefix");
    if (g_upload_proc_table.get_buffer_sub_data == NULL) {
        return 1;
    }

    uint8_t actual[DB_GL_PROBE_PREFIX_BYTES] = {0};
    db_gl_probe_drain_errors();
    g_upload_proc_table.get_buffer_sub_data(db_gl_upload_target_gl_enum(target),
                                            0, (GLsizeiptr)expected_size,
                                            actual);

    return db_gl_probe_finish(db_gl_probe_step_error_free() &&
                              (memcmp(expected, actual, expected_size) == 0));
}

int db_gl_verify_buffer_prefix(const uint8_t *expected, size_t expected_size) {
    return gl_verify_buffer_prefix_for_target(
        DB_GL_UPLOAD_TARGET_VBO_ARRAY_BUFFER, expected, expected_size);
}

int db_gl_probe_texture_create_rgba16f(unsigned int *out_texture,
                                       uint32_t width, uint32_t height) {
    if (out_texture == NULL) {
        return 0;
    }
    *out_texture = 0U;
    db_gl_load_upload_proc_table();
    if ((g_upload_proc_table.gen_textures == NULL) ||
        (g_upload_proc_table.bind_texture == NULL) ||
        (g_upload_proc_table.tex_parameteri == NULL) ||
        (g_upload_proc_table.tex_image_2d == NULL)) {
        return 0;
    }

    db_gl_probe_drain_errors();

    GLuint texture = 0U;
    g_upload_proc_table.gen_textures(1, &texture);
    if (texture == 0U) {
        return db_gl_probe_finish(0);
    }

    g_upload_proc_table.bind_texture(GL_TEXTURE_2D, texture);
    g_upload_proc_table.tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                                       GL_NEAREST);
    g_upload_proc_table.tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                                       GL_NEAREST);
    g_upload_proc_table.tex_image_2d(
        GL_TEXTURE_2D, 0, (GLint)GL_RGBA16F,
        db_checked_u32_to_int("db_gl_probe_texture_create_rgba16f", "width",
                              width),
        db_checked_u32_to_int("db_gl_probe_texture_create_rgba16f", "height",
                              height),
        0, GL_RGBA, GL_HALF_FLOAT, NULL);
    if (db_gl_probe_step_error_free() == 0) {
        if (g_upload_proc_table.delete_textures != NULL) {
            g_upload_proc_table.delete_textures(1, &texture);
        }
        return db_gl_probe_finish(0);
    }

    *out_texture = (unsigned int)texture;
    return db_gl_probe_finish(1);
}

static int gl_probe_create_uploaded_rgba16f(unsigned int *out_texture) {
    static const uint16_t probe_rgba16f[4U * 4U] = {
        DB_F16_ONE, 0x0000U,    0x0000U,    DB_F16_ONE, 0x0000U,    DB_F16_ONE,
        0x0000U,    DB_F16_ONE, 0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE,
        DB_F16_ONE, DB_F16_ONE, DB_F16_ONE, DB_F16_ONE,
    };
    if (db_gl_probe_texture_create_rgba16f(out_texture, 2U, 2U) == 0) {
        return 0;
    }
    db_gl_texture_bind_2d(*out_texture);
    db_gl_texture_sub_image_2d_rgba16f(0U, 0U, 2U, 2U, probe_rgba16f);
    if (db_gl_probe_step_error_free() != 0) {
        return 1;
    }
    db_gl_texture_delete_if_valid(out_texture);
    db_gl_texture_bind_2d(0U);
    return db_gl_probe_finish(0);
}

int db_gl_context_probe_texture_float_support(void) {
    db_gl_load_upload_proc_table();
    const db_gl_runtime_metadata_t runtime = db_gl_runtime_metadata_load();
    if (db_gl_extensions_advertise_texture_float(&runtime) == 0) {
        return 0;
    }

    // Probe: create/upload/delete a tiny RGBA16F texture and require clean
    // error state throughout.
    db_gl_probe_drain_errors();

    unsigned int probe_texture = 0U;
    if (gl_probe_create_uploaded_rgba16f(&probe_texture) == 0) {
        db_gl_probe_drain_errors();
        return 0;
    }
    db_gl_texture_delete_if_valid(&probe_texture);
    db_gl_texture_bind_2d(0U);
    return db_gl_probe_finish(1);
}

int db_gl_context_probe_texture_float_present_support(void) {
    if ((db_gl_context_probe_texture_float_support() == 0) ||
        (db_runtime_is_linux_x11() != 0)) {
        return 0;
    }

    db_gl_probe_drain_errors();

    unsigned int probe_texture = 0U;
    if (gl_probe_create_uploaded_rgba16f(&probe_texture) == 0) {
        return db_gl_probe_finish(0);
    }

    int viewport[4] = {0, 0, 0, 0};
    db_gl_get_integerv(GL_VIEWPORT, viewport);
    float probe_vertices[8] = {0.0F};
    db_gl_quad_init(probe_vertices);
    const float probe_texcoords[8] = {0.0F, 1.0F, 1.0F, 1.0F,
                                      0.0F, 0.0F, 1.0F, 0.0F};
    const float probe_colors[DB_RECT_VERTEX_COUNT * 4U] = {
        1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F,
        1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F,
    };
    uint8_t probe_readback[2U * 2U * 4U] = {0U};

    db_gl_set_viewport_px(2, 2);
    db_gl_clear_color_rgba(0.0F, 0.0F, 0.0F, 1.0F);
    db_gl_clear_color_buffer();
    db_gl_prepare_textured_present_state();
    db_gl_set_vertex_pointer_2f(0, probe_vertices);
    db_gl_set_color_pointer_f(4, 0, probe_colors);
    db_gl_set_texcoord_pointer_2f(0, probe_texcoords);
    db_gl_texture_bind_2d(probe_texture);
    db_gl_draw_arrays_triangle_strip(0, 4);
    db_gl_read_pixels_rgba8(0, 0, 2, 2, probe_readback);
    const int draw_ok = db_gl_probe_step_error_free();

    db_gl_finish_textured_present_state();
    db_gl_set_client_state_color_array_enabled(0);
    db_gl_set_client_state_vertex_array_enabled(0);
    db_gl_set_viewport_px(viewport[2], viewport[3]);
    db_gl_texture_delete_if_valid(&probe_texture);

    if (draw_ok == 0) {
        return db_gl_probe_finish(0);
    }

    const uint8_t *const p0 =
        &probe_readback[db_rgba4_channel_offset(2U, 0U, 0U)];
    const uint8_t *const p1 =
        &probe_readback[db_rgba4_channel_offset(2U, 1U, 0U)];
    const uint8_t *const p2 =
        &probe_readback[db_rgba4_channel_offset(2U, 0U, 1U)];
    const uint8_t *const p3 =
        &probe_readback[db_rgba4_channel_offset(2U, 1U, 1U)];
    return db_gl_probe_finish(
        DB_BOOL(db_gl_probe_rgb_matches(p0, 0U, 0U, 0U, 1U) &&
                db_gl_probe_rgb_matches(p1, 0U, 1U, 1U, 1U) &&
                db_gl_probe_rgb_matches(p2, 0U, 1U, 0U, 0U) &&
                db_gl_probe_rgb_matches(p3, 0U, 0U, 1U, 0U)));
}

static int gl_context_probe_target_persistent_upload(
    db_gl_upload_target_t target, size_t bytes, const void *initial_bytes,
    void **mapped_out) {
    if ((g_upload_proc_table.buffer_storage == NULL) ||
        (g_upload_proc_table.map_buffer_range == NULL) ||
        (g_upload_proc_table.unmap_buffer == NULL)) {
        return 0;
    }
    const size_t probe_size = db_gl_upload_probe_size_bytes(bytes);
    if ((probe_size == 0U) || (initial_bytes == NULL)) {
        return 0;
    }
    const GLenum gl_target = db_gl_upload_target_gl_enum(target);
    const GLbitfield storage_flags =
        GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;

    db_gl_probe_drain_errors();
    g_upload_proc_table.buffer_storage(gl_target, (GLsizeiptr)bytes, NULL,
                                       storage_flags);
    if (db_gl_probe_step_error_free() == 0) {
        return db_gl_probe_finish(0);
    }

    void *mapped = g_upload_proc_table.map_buffer_range(
        gl_target, 0, (GLsizeiptr)bytes, storage_flags);
    if ((mapped == NULL) || (db_gl_probe_step_error_free() == 0)) {
        if (mapped != NULL) {
            (void)g_upload_proc_table.unmap_buffer(gl_target);
        }
        return db_gl_probe_finish(0);
    }

    memcpy(mapped, initial_bytes, probe_size);
    if (!gl_verify_buffer_prefix_for_target(
            target, (const uint8_t *)initial_bytes, probe_size)) {
        (void)g_upload_proc_table.unmap_buffer(gl_target);
        return db_gl_probe_finish(0);
    }

    *mapped_out = mapped;
    return db_gl_probe_finish(1);
}

int db_gl_context_probe_persistent_upload(size_t bytes,
                                          const float *initial_vertices,
                                          void **mapped_out) {
    return gl_context_probe_target_persistent_upload(
        DB_GL_UPLOAD_TARGET_VBO_ARRAY_BUFFER, bytes,
        (const void *)initial_vertices, mapped_out);
}

static int gl_context_probe_target_map_range_upload(
    db_gl_upload_target_t target, size_t bytes, const void *initial_bytes,
    db_gl_stream_upload_capability_t *capability) {
    if ((g_upload_proc_table.map_buffer_range == NULL) ||
        (g_upload_proc_table.unmap_buffer == NULL) ||
        (g_upload_proc_table.buffer_sub_data == NULL)) {
        return 0;
    }

    const size_t probe_size = db_gl_upload_probe_size_bytes(bytes);
    if (probe_size == 0U) {
        return 0;
    }

    uint8_t pattern[DB_GL_PROBE_PREFIX_BYTES] = {0};
    db_gl_upload_probe_fill_pattern(pattern, probe_size);
    const GLenum gl_target = db_gl_upload_target_gl_enum(target);

    capability->mapping_probe_attempted = 1;
    db_gl_probe_drain_errors();
    void *dst = g_upload_proc_table.map_buffer_range(
        gl_target, 0, (GLsizeiptr)probe_size,
        GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT |
            GL_MAP_UNSYNCHRONIZED_BIT);
    const GLenum map_error = db_gl_get_error_value();
    if ((dst == NULL) || (map_error != GL_NO_ERROR)) {
        capability->mapping_probe_failure_step =
            DB_GL_UPLOAD_PROBE_STEP_MAP_RANGE;
        capability->mapping_probe_gl_error = map_error;
        return db_gl_probe_finish(0);
    }

    memcpy(dst, pattern, probe_size);
    const GLboolean unmapped = g_upload_proc_table.unmap_buffer(gl_target);
    const GLenum unmap_error = db_gl_get_error_value();
    if ((unmapped != GL_TRUE) || (unmap_error != GL_NO_ERROR)) {
        capability->mapping_probe_failure_step =
            DB_GL_UPLOAD_PROBE_STEP_MAP_RANGE_UNMAP;
        capability->mapping_probe_gl_error = unmap_error;
        return db_gl_probe_finish(0);
    }

    if (!gl_verify_buffer_prefix_for_target(target, pattern, probe_size)) {
        capability->mapping_probe_failure_step =
            DB_GL_UPLOAD_PROBE_STEP_CANARY_VERIFY;
        return db_gl_probe_finish(0);
    }

    g_upload_proc_table.buffer_sub_data(gl_target, 0, (GLsizeiptr)probe_size,
                                        initial_bytes);
    const GLenum restore_error = db_gl_get_error_value();
    if (restore_error != GL_NO_ERROR) {
        capability->mapping_probe_failure_step =
            DB_GL_UPLOAD_PROBE_STEP_RESTORE;
        capability->mapping_probe_gl_error = restore_error;
        return db_gl_probe_finish(0);
    }
    capability->mapping_probe_failure_step = DB_GL_UPLOAD_PROBE_STEP_NONE;
    capability->mapping_probe_gl_error = GL_NO_ERROR;
    return db_gl_probe_finish(1);
}

static int gl_context_probe_target_map_buffer_upload(
    db_gl_upload_target_t target, size_t bytes, const void *initial_bytes,
    db_gl_stream_upload_capability_t *capability) {
    if ((g_upload_proc_table.map_buffer == NULL) ||
        (g_upload_proc_table.unmap_buffer == NULL) ||
        (g_upload_proc_table.buffer_sub_data == NULL)) {
        return 0;
    }

    const size_t probe_size = db_gl_upload_probe_size_bytes(bytes);
    if (probe_size == 0U) {
        return 0;
    }

    uint8_t pattern[DB_GL_PROBE_PREFIX_BYTES] = {0};
    db_gl_upload_probe_fill_pattern(pattern, probe_size);
    const GLenum gl_target = db_gl_upload_target_gl_enum(target);

    capability->mapping_probe_attempted = 1;
    db_gl_probe_drain_errors();
    void *dst = g_upload_proc_table.map_buffer(gl_target, GL_WRITE_ONLY);
    const GLenum map_error = db_gl_get_error_value();
    if ((dst == NULL) || (map_error != GL_NO_ERROR)) {
        capability->mapping_probe_failure_step =
            DB_GL_UPLOAD_PROBE_STEP_MAP_BUFFER;
        capability->mapping_probe_gl_error = map_error;
        return db_gl_probe_finish(0);
    }

    memcpy(dst, pattern, probe_size);
    const GLboolean unmapped = g_upload_proc_table.unmap_buffer(gl_target);
    const GLenum unmap_error = db_gl_get_error_value();
    if ((unmapped != GL_TRUE) || (unmap_error != GL_NO_ERROR)) {
        capability->mapping_probe_failure_step =
            DB_GL_UPLOAD_PROBE_STEP_MAP_BUFFER_UNMAP;
        capability->mapping_probe_gl_error = unmap_error;
        return db_gl_probe_finish(0);
    }

    if (!gl_verify_buffer_prefix_for_target(target, pattern, probe_size)) {
        capability->mapping_probe_failure_step =
            DB_GL_UPLOAD_PROBE_STEP_CANARY_VERIFY;
        return db_gl_probe_finish(0);
    }

    g_upload_proc_table.buffer_sub_data(gl_target, 0, (GLsizeiptr)probe_size,
                                        initial_bytes);
    const GLenum restore_error = db_gl_get_error_value();
    if (restore_error != GL_NO_ERROR) {
        capability->mapping_probe_failure_step =
            DB_GL_UPLOAD_PROBE_STEP_RESTORE;
        capability->mapping_probe_gl_error = restore_error;
        return db_gl_probe_finish(0);
    }
    capability->mapping_probe_failure_step = DB_GL_UPLOAD_PROBE_STEP_NONE;
    capability->mapping_probe_gl_error = GL_NO_ERROR;
    return db_gl_probe_finish(1);
}

int db_gl_context_probe_map_range_upload(size_t bytes,
                                         const float *initial_vertices) {
    db_gl_stream_upload_capability_t capability = {0};
    return gl_context_probe_target_map_range_upload(
        DB_GL_UPLOAD_TARGET_VBO_ARRAY_BUFFER, bytes,
        (const void *)initial_vertices, &capability);
}

db_gl_stream_upload_capability_t
db_gl_stream_upload_capability_probe(db_gl_upload_target_t target, size_t bytes,
                                     const void *initial_bytes,
                                     int enable_sync) {
    db_gl_stream_upload_capability_t capability = {
        .target = target,
        .requested_storage = DB_GL_STREAM_UPLOAD_STORAGE_BUFFER_OBJECT,
        .supported_storage = DB_GL_STREAM_UPLOAD_STORAGE_CLIENT,
        .effective_storage = DB_GL_STREAM_UPLOAD_STORAGE_CLIENT,
        .requested_mode = DB_GL_STREAM_UPLOAD_MODE_PERSISTENT,
        .supported_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA,
        .effective_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA,
        .sync_supported = DB_BOOL(enable_sync),
        .sync_enabled = DB_BOOL(enable_sync),
        .staging_storage_bytes = bytes,
        .alignment_bytes = DB_CACHELINE_ALIGNMENT_BYTES,
        .partial_updates_supported = 1,
        .mapping_validated = 0,
        .canary_validated = 0,
        .mapping_probe_attempted = 0,
        .mapping_probe_failure_step = DB_GL_UPLOAD_PROBE_STEP_NONE,
        .mapping_probe_gl_error = GL_NO_ERROR,
        .demotion_reason = DB_GL_UPLOAD_FAILURE_NONE,
    };

    db_gl_require_upload_proc_table_loaded(
        "db_gl_stream_upload_capability_probe");
    const db_gl_runtime_metadata_t runtime = db_gl_runtime_metadata_load();
    if ((target == DB_GL_UPLOAD_TARGET_VBO_ARRAY_BUFFER) &&
        (db_gl_context_advertises_vbo() == 0)) {
        capability.demotion_reason = DB_GL_UPLOAD_FAILURE_PROBE_REJECTED;
        return capability;
    }
    if (((target == DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER) ||
         (target == DB_GL_UPLOAD_TARGET_PBO_PACK_BUFFER)) &&
        (db_gl_extensions_advertise_pbo(&runtime) == 0)) {
        capability.demotion_reason = DB_GL_UPLOAD_FAILURE_PROBE_REJECTED;
        return capability;
    }
    if ((g_upload_proc_table.gen_buffers == NULL) ||
        (g_upload_proc_table.bind_buffer == NULL) ||
        (g_upload_proc_table.delete_buffers == NULL) ||
        (g_upload_proc_table.buffer_data == NULL)) {
        capability.demotion_reason = DB_GL_UPLOAD_FAILURE_API_UNAVAILABLE;
        return capability;
    }

    static const uint8_t k_probe_bytes[DB_GL_PROBE_PREFIX_BYTES] = {
        0xA5U, 0xA4U, 0xA7U, 0xA6U, 0xA1U, 0xA0U, 0xA3U, 0xA2U,
        0xADU, 0xACU, 0xAFU, 0xAEU, 0xA9U, 0xA8U, 0xABU, 0xAAU,
    };
    const void *probe_bytes =
        (initial_bytes != NULL) ? initial_bytes : (const void *)k_probe_bytes;
    const GLenum gl_target = db_gl_upload_target_gl_enum(target);

    GLuint probe_buffer = 0U;
    g_upload_proc_table.gen_buffers(1, &probe_buffer);
    if (probe_buffer == 0U) {
        capability.demotion_reason = DB_GL_UPLOAD_FAILURE_TARGET_ACQUIRE;
        return capability;
    }
    g_upload_proc_table.bind_buffer(gl_target, probe_buffer);

    g_upload_proc_table.buffer_data(gl_target, (GLsizeiptr)bytes, NULL,
                                    GL_DYNAMIC_DRAW);
    if (db_gl_probe_step_error_free() == 0) {
        g_upload_proc_table.bind_buffer(gl_target, 0U);
        g_upload_proc_table.delete_buffers(1, &probe_buffer);
        capability.demotion_reason = DB_GL_UPLOAD_FAILURE_STORAGE_ALLOC;
        (void)db_gl_probe_finish(0);
        return capability;
    }

    capability.supported_storage = DB_GL_STREAM_UPLOAD_STORAGE_BUFFER_OBJECT;
    capability.effective_storage = DB_GL_STREAM_UPLOAD_STORAGE_BUFFER_OBJECT;
    capability.supported_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA;
    capability.effective_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA;
    capability.canary_validated = 1;

    void *persistent_mapping = NULL;
    if ((target == DB_GL_UPLOAD_TARGET_VBO_ARRAY_BUFFER) &&
        db_gl_extensions_advertise_buffer_storage(&runtime) &&
        db_gl_extensions_advertise_map_buffer_range(&runtime) &&
        gl_context_probe_target_persistent_upload(target, bytes, probe_bytes,
                                                  &persistent_mapping)) {
        capability.supported_mode = DB_GL_STREAM_UPLOAD_MODE_PERSISTENT;
        capability.effective_mode = DB_GL_STREAM_UPLOAD_MODE_PERSISTENT;
        capability.mapping_validated = 1;
    } else if (db_gl_extensions_advertise_map_buffer_range(&runtime) &&
               gl_context_probe_target_map_range_upload(
                   target, bytes, probe_bytes, &capability)) {
        int read_ok = 1;
        if (target == DB_GL_UPLOAD_TARGET_PBO_PACK_BUFFER) {
            db_gl_probe_drain_errors();
            void *read_dst = g_upload_proc_table.map_buffer_range(
                gl_target, 0, (GLsizeiptr)db_gl_upload_probe_size_bytes(bytes),
                GL_MAP_READ_BIT);
            if ((read_dst == NULL) || (db_gl_probe_step_error_free() == 0)) {
                read_ok = 0;
            }
            if (read_dst != NULL) {
                (void)g_upload_proc_table.unmap_buffer(gl_target);
            }
            (void)db_gl_probe_finish(read_ok);
        }
        if (read_ok != 0) {
            capability.supported_mode = DB_GL_STREAM_UPLOAD_MODE_MAP_RANGE;
            capability.effective_mode = DB_GL_STREAM_UPLOAD_MODE_MAP_RANGE;
            capability.mapping_validated = 1;
        }
    } else if (db_gl_extensions_advertise_map_buffer(&runtime) &&
               gl_context_probe_target_map_buffer_upload(
                   target, bytes, probe_bytes, &capability)) {
        capability.supported_mode = DB_GL_STREAM_UPLOAD_MODE_MAP_BUFFER;
        capability.effective_mode = DB_GL_STREAM_UPLOAD_MODE_MAP_BUFFER;
        capability.mapping_validated = 1;
    }

    if ((capability.mapping_probe_attempted != 0) &&
        (capability.mapping_validated == 0)) {
        capability.demotion_reason = DB_GL_UPLOAD_FAILURE_PROBE_REJECTED;
    }

    if ((persistent_mapping != NULL) &&
        (g_upload_proc_table.unmap_buffer != NULL)) {
        (void)g_upload_proc_table.unmap_buffer(gl_target);
    }
    g_upload_proc_table.bind_buffer(gl_target, 0U);
    g_upload_proc_table.delete_buffers(1, &probe_buffer);
    return capability;
}
