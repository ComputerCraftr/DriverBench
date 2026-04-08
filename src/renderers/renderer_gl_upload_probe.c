#include "../core/db_buffer_convert.h"
#include "../core/db_core.h"
#include "renderer_benchmark_types.h"
#include "renderer_gl_api.h"
#include "renderer_gl_common.h"
#include "renderer_gl_probe_internal.h"
#include "renderer_gl_proc_runtime_internal.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Context probe checks (active runtime verification).
int db_gl_verify_buffer_prefix(const uint8_t *expected, size_t expected_size) {
    if (expected_size == 0U) {
        return 0;
    }

    db_gl_require_upload_proc_table_loaded("db_gl_verify_buffer_prefix");
    if (g_upload_proc_table.get_buffer_sub_data == NULL) {
        return 1;
    }

    uint8_t actual[DB_GL_PROBE_PREFIX_BYTES] = {0};
    db_gl_probe_drain_errors();
    g_upload_proc_table.get_buffer_sub_data(GL_ARRAY_BUFFER, 0,
                                            (GLsizeiptr)expected_size, actual);

    return db_gl_probe_finish(db_gl_probe_step_error_free() &&
                              (memcmp(expected, actual, expected_size) == 0));
}

int db_gl_probe_texture_create_rgba16f(unsigned int *out_texture, int width,
                                       int height) {
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
    g_upload_proc_table.tex_image_2d(GL_TEXTURE_2D, 0, (GLint)GL_RGBA16F,
                                     (GLsizei)width, (GLsizei)height, 0,
                                     GL_RGBA, GL_HALF_FLOAT, NULL);
    if (db_gl_probe_step_error_free() == 0) {
        if (g_upload_proc_table.delete_textures != NULL) {
            g_upload_proc_table.delete_textures(1, &texture);
        }
        return db_gl_probe_finish(0);
    }

    *out_texture = (unsigned int)texture;
    return db_gl_probe_finish(1);
}

int db_gl_context_probe_texture_float_support(void) {
    static int cached_result = -1;
    if (cached_result >= 0) {
        return cached_result;
    }
    db_gl_load_upload_proc_table();
    const db_gl_runtime_metadata_t runtime = db_gl_runtime_metadata_load();
    if (db_gl_extensions_advertise_texture_float(&runtime) == 0) {
        cached_result = 0;
        return 0;
    }

    // Probe: create/upload/delete a tiny RGBA16F texture and require clean
    // error state throughout.
    db_gl_probe_drain_errors();

    unsigned int probe_texture = 0U;
    if (db_gl_probe_texture_create_rgba16f(&probe_texture, 2, 2) == 0) {
        db_gl_probe_drain_errors();
        cached_result = 0;
        return 0;
    }

    static const uint16_t k_probe_rgba16f[4U * 4U] = {
        DB_F16_ONE, 0x0000U,    0x0000U,    DB_F16_ONE, 0x0000U,    DB_F16_ONE,
        0x0000U,    DB_F16_ONE, 0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE,
        DB_F16_ONE, DB_F16_ONE, DB_F16_ONE, DB_F16_ONE,
    };
    db_gl_texture_bind_2d(probe_texture);
    db_gl_texture_sub_image_2d_rgba16f(0, 0, 2, 2, k_probe_rgba16f);
    const int upload_ok = db_gl_probe_step_error_free();
    db_gl_texture_delete_if_valid(&probe_texture);
    db_gl_texture_bind_2d(0U);
    cached_result = db_gl_probe_finish(upload_ok);
    return cached_result;
}

int db_gl_context_probe_texture_float_present_support(void) {
    static int cached_result = -1;
    if (cached_result >= 0) {
        return cached_result;
    }
    if (db_gl_context_probe_texture_float_support() == 0) {
        cached_result = 0;
        return 0;
    }

    db_gl_probe_drain_errors();

    unsigned int probe_texture = 0U;
    if (db_gl_probe_texture_create_rgba16f(&probe_texture, 2, 2) == 0) {
        cached_result = db_gl_probe_finish(0);
        return cached_result;
    }

    static const uint16_t k_probe_rgba16f[4U * 4U] = {
        DB_F16_ONE, 0x0000U,    0x0000U,    DB_F16_ONE, 0x0000U,    DB_F16_ONE,
        0x0000U,    DB_F16_ONE, 0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE,
        DB_F16_ONE, DB_F16_ONE, DB_F16_ONE, DB_F16_ONE,
    };
    db_gl_texture_bind_2d(probe_texture);
    db_gl_texture_sub_image_2d_rgba16f(0, 0, 2, 2, k_probe_rgba16f);
    if (db_gl_probe_step_error_free() == 0) {
        db_gl_texture_delete_if_valid(&probe_texture);
        db_gl_texture_bind_2d(0U);
        cached_result = db_gl_probe_finish(0);
        return cached_result;
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
    db_gl_set_depth_test_enabled(0);
    db_gl_set_cull_face_enabled(0);
    db_gl_set_blend_enabled(0);
    db_gl_set_texture_2d_enabled(1);
    db_gl_set_client_state_vertex_array_enabled(1);
    db_gl_set_client_state_color_array_enabled(1);
    db_gl_set_client_state_texcoord_array_enabled(1);
    db_gl_set_vertex_pointer_2f(0, probe_vertices);
    db_gl_set_color_pointer_f(4, 0, probe_colors);
    db_gl_set_texcoord_pointer_2f(0, probe_texcoords);
    db_gl_texture_bind_2d(probe_texture);
    db_gl_draw_arrays_triangle_strip(0, 4);
    db_gl_read_pixels_rgba8(0, 0, 2, 2, probe_readback);
    const int draw_ok = db_gl_probe_step_error_free();

    db_gl_texture_bind_2d(0U);
    db_gl_set_texture_2d_enabled(0);
    db_gl_set_client_state_texcoord_array_enabled(0);
    db_gl_set_client_state_color_array_enabled(0);
    db_gl_set_client_state_vertex_array_enabled(0);
    db_gl_set_viewport_px(viewport[2], viewport[3]);
    db_gl_texture_delete_if_valid(&probe_texture);

    if (draw_ok == 0) {
        cached_result = db_gl_probe_finish(0);
        return 0;
    }

    const uint8_t *const p0 =
        &probe_readback[db_gl_probe_rgba_pixel_offset(2U, 0U, 0U)];
    const uint8_t *const p1 =
        &probe_readback[db_gl_probe_rgba_pixel_offset(2U, 1U, 0U)];
    const uint8_t *const p2 =
        &probe_readback[db_gl_probe_rgba_pixel_offset(2U, 0U, 1U)];
    const uint8_t *const p3 =
        &probe_readback[db_gl_probe_rgba_pixel_offset(2U, 1U, 1U)];
    cached_result =
        db_gl_probe_finish((db_gl_probe_rgb_matches(p0, 0U, 0U, 0U, 1U) &&
                            db_gl_probe_rgb_matches(p1, 0U, 1U, 1U, 1U) &&
                            db_gl_probe_rgb_matches(p2, 0U, 1U, 0U, 0U) &&
                            db_gl_probe_rgb_matches(p3, 0U, 0U, 1U, 0U))
                               ? 1
                               : 0);
    return cached_result;
}

int db_gl_context_probe_persistent_upload(size_t bytes,
                                          const float *initial_vertices,
                                          void **mapped_out) {
    if ((g_upload_proc_table.buffer_storage == NULL) ||
        (g_upload_proc_table.map_buffer_range == NULL) ||
        (g_upload_proc_table.unmap_buffer == NULL)) {
        return 0;
    }
    const size_t probe_size = db_gl_upload_probe_size_bytes(bytes);
    if ((probe_size == 0U) || (initial_vertices == NULL)) {
        return 0;
    }
    const GLbitfield storage_flags =
        GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;

    db_gl_probe_drain_errors();
    g_upload_proc_table.buffer_storage(GL_ARRAY_BUFFER, (GLsizeiptr)bytes, NULL,
                                       storage_flags);
    if (db_gl_probe_step_error_free() == 0) {
        return db_gl_probe_finish(0);
    }

    void *mapped = g_upload_proc_table.map_buffer_range(
        GL_ARRAY_BUFFER, 0, (GLsizeiptr)bytes, storage_flags);
    if ((mapped == NULL) || (db_gl_probe_step_error_free() == 0)) {
        if (mapped != NULL) {
            (void)g_upload_proc_table.unmap_buffer(GL_ARRAY_BUFFER);
        }
        return db_gl_probe_finish(0);
    }

    db_copy_bytes(mapped, initial_vertices, probe_size);
    if (!db_gl_verify_buffer_prefix((const uint8_t *)initial_vertices,
                                    probe_size)) {
        (void)g_upload_proc_table.unmap_buffer(GL_ARRAY_BUFFER);
        return db_gl_probe_finish(0);
    }

    *mapped_out = mapped;
    return db_gl_probe_finish(1);
}

int db_gl_context_probe_map_range_upload(size_t bytes,
                                         const float *initial_vertices) {
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

    db_gl_probe_drain_errors();
    void *dst = g_upload_proc_table.map_buffer_range(
        GL_ARRAY_BUFFER, 0, (GLsizeiptr)probe_size,
        GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT |
            GL_MAP_UNSYNCHRONIZED_BIT);
    if ((dst == NULL) || (db_gl_probe_step_error_free() == 0)) {
        return db_gl_probe_finish(0);
    }

    db_copy_bytes(dst, pattern, probe_size);
    if ((g_upload_proc_table.unmap_buffer(GL_ARRAY_BUFFER) != GL_TRUE) ||
        (db_gl_probe_step_error_free() == 0)) {
        return db_gl_probe_finish(0);
    }

    if (!db_gl_verify_buffer_prefix(pattern, probe_size)) {
        return db_gl_probe_finish(0);
    }

    g_upload_proc_table.buffer_sub_data(
        GL_ARRAY_BUFFER, 0, (GLsizeiptr)probe_size, initial_vertices);
    return db_gl_probe_finish(db_gl_probe_step_error_free());
}

static int
db_gl_context_probe_map_buffer_upload(size_t bytes,
                                      const float *initial_vertices) {
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

    db_gl_probe_drain_errors();
    void *dst = g_upload_proc_table.map_buffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
    if ((dst == NULL) || (db_gl_probe_step_error_free() == 0)) {
        return db_gl_probe_finish(0);
    }

    db_copy_bytes(dst, pattern, probe_size);
    if ((g_upload_proc_table.unmap_buffer(GL_ARRAY_BUFFER) != GL_TRUE) ||
        (db_gl_probe_step_error_free() == 0)) {
        return db_gl_probe_finish(0);
    }

    if (!db_gl_verify_buffer_prefix(pattern, probe_size)) {
        return db_gl_probe_finish(0);
    }

    g_upload_proc_table.buffer_sub_data(
        GL_ARRAY_BUFFER, 0, (GLsizeiptr)probe_size, initial_vertices);
    return db_gl_probe_finish(db_gl_probe_step_error_free());
}

void db_gl_context_probe_upload_capabilities(size_t bytes,
                                             const float *initial_vertices,
                                             db_gl_upload_probe_result_t *out) {
    if (out == NULL) {
        db_failf("renderer_gl_common",
                 "db_gl_context_probe_upload_capabilities: output is null");
    }

    *out = (db_gl_upload_probe_result_t){0};
    if (db_gl_context_advertises_vbo() == 0) {
        return;
    }

    db_gl_require_upload_proc_table_loaded(
        "db_gl_context_probe_upload_capabilities");

    const db_gl_runtime_metadata_t runtime = db_gl_runtime_metadata_load();

    if (db_gl_extensions_advertise_buffer_storage(&runtime) &&
        db_gl_extensions_advertise_map_buffer_range(&runtime) &&
        db_gl_context_probe_persistent_upload(bytes, initial_vertices,
                                              &out->persistent_mapped_ptr)) {
        out->use_persistent_upload = 1;
        return;
    }

    if (g_upload_proc_table.buffer_data == NULL) {
        return;
    }
    // Intentionally pass NULL: this is a storage/orphan allocation step for
    // the probe buffer, not the data-path validation itself.
    g_upload_proc_table.buffer_data(GL_ARRAY_BUFFER, (GLsizeiptr)bytes, NULL,
                                    GL_DYNAMIC_DRAW);
    if (db_gl_probe_step_error_free() == 0) {
        (void)db_gl_probe_finish(0);
        return;
    }

    if (db_gl_extensions_advertise_map_buffer_range(&runtime) &&
        db_gl_context_probe_map_range_upload(bytes, initial_vertices)) {
        out->use_map_range_upload = 1;
        return;
    }

    if (db_gl_extensions_advertise_map_buffer(&runtime) &&
        db_gl_context_probe_map_buffer_upload(bytes, initial_vertices)) {
        out->use_map_buffer_upload = 1;
    }
}

// 5) Upload path helpers and range upload execution.
// Implemented in renderer_gl_upload.c.
