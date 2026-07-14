#include "../core/db_core.h"
#include "../core/db_numeric.h"
#include "gl_api.h"
#include "gl_common.h"
#include "gl_probe_internal.h"
#include "gl_proc_runtime.h"
#include <stddef.h>
#include <stdint.h>

static void gl_texture_set_nearest_clamp_2d(void) {
    if (g_upload_proc_table.tex_parameteri == NULL) {
        return;
    }
    g_upload_proc_table.tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                                       GL_NEAREST);
    g_upload_proc_table.tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                                       GL_NEAREST);
    g_upload_proc_table.tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                                       GL_CLAMP_TO_EDGE);
    g_upload_proc_table.tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                                       GL_CLAMP_TO_EDGE);
}

static int gl_texture_allocate_typed(unsigned int texture, uint32_t width,
                                     uint32_t height,
                                     unsigned int internal_format,
                                     unsigned int upload_format,
                                     unsigned int pixel_type,
                                     const void *pixels) {
    if ((texture == 0U) || (width == 0U) || (height == 0U)) {
        return 0;
    }
    db_gl_load_upload_proc_table();
    if ((g_upload_proc_table.bind_texture == NULL) ||
        (g_upload_proc_table.tex_image_2d == NULL)) {
        return 0;
    }
    db_gl_probe_drain_errors();
    g_upload_proc_table.bind_texture(GL_TEXTURE_2D, (GLuint)texture);
    gl_texture_set_nearest_clamp_2d();
    g_upload_proc_table.tex_image_2d(
        GL_TEXTURE_2D, 0, (GLint)internal_format,
        db_checked_u32_to_int("gl_texture_allocate_rgba_typed", "width", width),
        db_checked_u32_to_int("gl_texture_allocate_rgba_typed", "height",
                              height),
        0, (GLenum)upload_format, (GLenum)pixel_type, pixels);
    return db_gl_probe_finish(db_gl_probe_step_error_free());
}

int db_gl_texture_allocate_rgba(unsigned int texture, uint32_t width,
                                uint32_t height, unsigned int internal_format,
                                const void *pixels) {
    return gl_texture_allocate_typed(texture, width, height, internal_format,
                                     GL_RGBA, GL_UNSIGNED_BYTE, pixels);
}

static int gl_texture_create_typed(unsigned int *out_texture, uint32_t width,
                                   uint32_t height,
                                   unsigned int internal_format,
                                   unsigned int upload_format,
                                   unsigned int pixel_type,
                                   const void *pixels) {
    if (out_texture == NULL) {
        return 0;
    }
    *out_texture = 0U;
    if ((width == 0U) || (height == 0U)) {
        return 0;
    }
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.gen_textures == NULL) {
        return 0;
    }
    GLuint texture = 0U;
    g_upload_proc_table.gen_textures(1, &texture);
    if (texture == 0U) {
        return 0;
    }
    if (gl_texture_allocate_typed((unsigned int)texture, width, height,
                                  internal_format, upload_format, pixel_type,
                                  pixels) == 0) {
        if (g_upload_proc_table.delete_textures != NULL) {
            g_upload_proc_table.delete_textures(1, &texture);
        }
        return 0;
    }
    *out_texture = (unsigned int)texture;
    return 1;
}

int db_gl_texture_create_rgba(unsigned int *out_texture, uint32_t width,
                              uint32_t height, unsigned int internal_format,
                              const void *pixels) {
    return gl_texture_create_typed(out_texture, width, height, internal_format,
                                   GL_RGBA, GL_UNSIGNED_BYTE, pixels);
}

int db_gl_texture_create_rgba8(unsigned int *out_texture, uint32_t width,
                               uint32_t height, const uint8_t *pixels) {
    return db_gl_texture_create_rgba(out_texture, width, height, GL_RGBA,
                                     pixels);
}

int db_gl_texture_create_rgba16f(unsigned int *out_texture, uint32_t width,
                                 uint32_t height, const uint16_t *pixels) {
    return gl_texture_create_typed(out_texture, width, height, GL_RGBA16F,
                                   GL_RGBA, GL_HALF_FLOAT, pixels);
}

int db_gl_texture_create_rgb10a2_bt2020_pq(unsigned int *out_texture,
                                           uint32_t width, uint32_t height,
                                           const uint32_t *pixels) {
    return gl_texture_create_typed(out_texture, width, height, GL_RGB10_A2,
                                   GL_BGRA, GL_UNSIGNED_INT_2_10_10_10_REV,
                                   pixels);
}

void db_gl_texture_delete_if_valid(unsigned int *texture) {
    if ((texture == NULL) || (*texture == 0U)) {
        return;
    }
    const GLuint gl_texture = (GLuint)(*texture);
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.delete_textures != NULL) {
        g_upload_proc_table.delete_textures(1, &gl_texture);
    }
    *texture = 0U;
}

void db_gl_texture_bind_2d(unsigned int texture) {
    db_gl_load_upload_proc_table();
    if ((g_active_texture_unit_valid != 0) &&
        (g_active_texture_unit >= GL_TEXTURE0) &&
        (g_active_texture_unit <
         (GL_TEXTURE0 + DB_GL_TRACKED_TEXTURE_UNIT_COUNT))) {
        const size_t unit_index = (size_t)(g_active_texture_unit - GL_TEXTURE0);
        if ((g_texture2d_binding_valid_by_unit[unit_index] != 0) &&
            (g_texture2d_binding_by_unit[unit_index] == texture)) {
            return;
        }
        if (g_upload_proc_table.bind_texture != NULL) {
            g_upload_proc_table.bind_texture(GL_TEXTURE_2D, (GLuint)texture);
            g_texture2d_binding_by_unit[unit_index] = texture;
            g_texture2d_binding_valid_by_unit[unit_index] = 1;
        }
        return;
    }
    if (g_upload_proc_table.bind_texture != NULL) {
        g_upload_proc_table.bind_texture(GL_TEXTURE_2D, (GLuint)texture);
    }
}

void db_gl_texture_sub_image_2d_rgba(uint32_t x_px, uint32_t y_px,
                                     uint32_t width, uint32_t height,
                                     const uint8_t *pixels) {
    if ((width == 0U) || (height == 0U)) {
        return;
    }
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.tex_sub_image_2d != NULL) {
        g_upload_proc_table.tex_sub_image_2d(
            GL_TEXTURE_2D, 0,
            db_checked_u32_to_int("db_gl_texture_sub_image_2d_rgba", "x_px",
                                  x_px),
            db_checked_u32_to_int("db_gl_texture_sub_image_2d_rgba", "y_px",
                                  y_px),
            db_checked_u32_to_int("db_gl_texture_sub_image_2d_rgba", "width",
                                  width),
            db_checked_u32_to_int("db_gl_texture_sub_image_2d_rgba", "height",
                                  height),
            GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    }
}

void db_gl_texture_sub_image_2d_rgba16f(uint32_t x_px, uint32_t y_px,
                                        uint32_t width, uint32_t height,
                                        const uint16_t *pixels) {
    if ((width == 0U) || (height == 0U)) {
        return;
    }
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.tex_sub_image_2d != NULL) {
        g_upload_proc_table.tex_sub_image_2d(
            GL_TEXTURE_2D, 0,
            db_checked_u32_to_int("db_gl_texture_sub_image_2d_rgba16f", "x_px",
                                  x_px),
            db_checked_u32_to_int("db_gl_texture_sub_image_2d_rgba16f", "y_px",
                                  y_px),
            db_checked_u32_to_int("db_gl_texture_sub_image_2d_rgba16f", "width",
                                  width),
            db_checked_u32_to_int("db_gl_texture_sub_image_2d_rgba16f",
                                  "height", height),
            GL_RGBA, GL_HALF_FLOAT, pixels);
    }
}

void db_gl_texture_sub_image_2d_rgb10a2_bt2020_pq(uint32_t x_px, uint32_t y_px,
                                                  uint32_t width,
                                                  uint32_t height,
                                                  const uint32_t *pixels) {
    if ((width == 0U) || (height == 0U)) {
        return;
    }
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.tex_sub_image_2d != NULL) {
        g_upload_proc_table.tex_sub_image_2d(
            GL_TEXTURE_2D, 0,
            db_checked_u32_to_int(
                "db_gl_texture_sub_image_2d_rgb10a2_bt2020_pq", "x_px", x_px),
            db_checked_u32_to_int(
                "db_gl_texture_sub_image_2d_rgb10a2_bt2020_pq", "y_px", y_px),
            db_checked_u32_to_int(
                "db_gl_texture_sub_image_2d_rgb10a2_bt2020_pq", "width", width),
            db_checked_u32_to_int(
                "db_gl_texture_sub_image_2d_rgb10a2_bt2020_pq", "height",
                height),
            GL_BGRA, GL_UNSIGNED_INT_2_10_10_10_REV, pixels);
    }
}

// 3) Wrapper APIs: fixed-function state, client arrays, and readback.
void db_gl_clear_color_rgba(float red, float green, float blue, float alpha) {
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.clear_color != NULL) {
        g_upload_proc_table.clear_color(red, green, blue, alpha);
    }
}

void db_gl_clear_color_buffer(void) {
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.clear != NULL) {
        g_upload_proc_table.clear(GL_COLOR_BUFFER_BIT);
    }
}

static void gl_set_capability_enabled_cached(unsigned int capability,
                                             int enabled, int *cached_state) {
    const int normalized_enabled = DB_BOOL(enabled);
    if ((cached_state == NULL) || (*cached_state == normalized_enabled)) {
        return;
    }
    if ((normalized_enabled != 0) && (g_upload_proc_table.enable != NULL)) {
        g_upload_proc_table.enable(capability);
    }
    if ((normalized_enabled == 0) && (g_upload_proc_table.disable != NULL)) {
        g_upload_proc_table.disable(capability);
    }
    *cached_state = normalized_enabled;
}

static void
db_gl_set_capability_enabled_cached_valid(unsigned int capability, int enabled,
                                          unsigned int *cached_state,
                                          int *state_valid) {
    const int normalized_enabled = DB_BOOL(enabled);
    if ((cached_state == NULL) || (state_valid == NULL)) {
        return;
    }
    if ((*state_valid != 0) && ((int)(*cached_state) == normalized_enabled)) {
        return;
    }
    if ((normalized_enabled != 0) && (g_upload_proc_table.enable != NULL)) {
        g_upload_proc_table.enable(capability);
    }
    if ((normalized_enabled == 0) && (g_upload_proc_table.disable != NULL)) {
        g_upload_proc_table.disable(capability);
    }
    *cached_state = (unsigned int)normalized_enabled;
    *state_valid = 1;
}

static void gl_set_client_state_enabled_cached(unsigned int capability,
                                               int enabled, int *cached_state) {
    const int normalized_enabled = DB_BOOL(enabled);
    if ((cached_state == NULL) || (*cached_state == normalized_enabled)) {
        return;
    }
    if ((normalized_enabled != 0) &&
        (g_upload_proc_table.enable_client_state != NULL)) {
        g_upload_proc_table.enable_client_state(capability);
    }
    if ((normalized_enabled == 0) &&
        (g_upload_proc_table.disable_client_state != NULL)) {
        g_upload_proc_table.disable_client_state(capability);
    }
    *cached_state = normalized_enabled;
}

void db_gl_set_blend_enabled(int enabled) {
    db_gl_load_upload_proc_table();
    gl_set_capability_enabled_cached(GL_BLEND, enabled, &g_blend_enabled_state);
}

void db_gl_set_cull_face_enabled(int enabled) {
    db_gl_load_upload_proc_table();
    gl_set_capability_enabled_cached(GL_CULL_FACE, enabled,
                                     &g_cull_face_enabled_state);
}

void db_gl_set_depth_test_enabled(int enabled) {
    db_gl_load_upload_proc_table();
    gl_set_capability_enabled_cached(GL_DEPTH_TEST, enabled,
                                     &g_depth_test_enabled_state);
}

void db_gl_set_dither_enabled(int enabled) {
    db_gl_load_upload_proc_table();
    gl_set_capability_enabled_cached(GL_DITHER, enabled,
                                     &g_dither_enabled_state);
}

void db_gl_set_pack_alignment_1(void) {
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.pixel_storei != NULL) {
        g_upload_proc_table.pixel_storei(GL_PACK_ALIGNMENT, 1);
    }
}

void db_gl_set_unpack_alignment_1(void) {
    db_gl_load_upload_proc_table();
    if ((g_unpack_alignment_state_valid != 0) &&
        (g_unpack_alignment_state == 1)) {
        return;
    }
    if (g_upload_proc_table.pixel_storei != NULL) {
        g_upload_proc_table.pixel_storei(GL_UNPACK_ALIGNMENT, 1);
        g_unpack_alignment_state = 1;
        g_unpack_alignment_state_valid = 1;
    }
}

void db_gl_prepare_textured_present_state(void) {
    db_gl_set_depth_test_enabled(0);
    db_gl_set_cull_face_enabled(0);
    db_gl_set_blend_enabled(0);
    db_gl_set_dither_enabled(0);
    db_gl_set_texture_2d_enabled(1);
    db_gl_set_unpack_alignment_1();
    db_gl_set_unpack_row_length_pixels(0);
    (void)db_gl_upload_stream_unbind_target(
        DB_GL_UPLOAD_TARGET_VBO_ARRAY_BUFFER);
    db_gl_set_client_state_vertex_array_enabled(1);
    db_gl_set_client_state_color_array_enabled(1);
    db_gl_set_client_state_texcoord_array_enabled(1);
    if (g_upload_proc_table.tex_envi != NULL) {
        g_upload_proc_table.tex_envi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE,
                                     GL_REPLACE);
    }
}

void db_gl_finish_textured_present_state(void) {
    db_gl_texture_bind_2d(0U);
    db_gl_set_unpack_row_length_pixels(0);
    db_gl_set_texture_2d_enabled(0);
    db_gl_set_client_state_texcoord_array_enabled(0);
}

void db_gl_upload_state_reset_unpack(void) {
    db_gl_set_unpack_alignment_1();
    db_gl_set_unpack_row_length_pixels(0);
    (void)db_gl_upload_stream_unbind_target(
        DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER);
}

void db_gl_upload_state_reset_pack(void) {
    db_gl_set_pack_alignment_1();
    (void)db_gl_upload_stream_unbind_target(
        DB_GL_UPLOAD_TARGET_PBO_PACK_BUFFER);
}

void db_gl_upload_state_reset_all(void) {
    db_gl_upload_state_reset_unpack();
    db_gl_upload_state_reset_pack();
    db_gl_upload_state_reset_present_arrays();
}

void db_gl_upload_state_reset_present_arrays(void) {
    (void)db_gl_upload_stream_unbind_target(
        DB_GL_UPLOAD_TARGET_VBO_ARRAY_BUFFER);
}

void db_gl_read_pixels_rgba8(uint32_t x_px, uint32_t y_px, uint32_t width,
                             uint32_t height, uint8_t *pixels) {
    if ((width == 0U) || (height == 0U)) {
        return;
    }
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.read_pixels != NULL) {
        g_upload_proc_table.read_pixels(
            db_checked_u32_to_int("db_gl_read_pixels_rgba8", "x_px", x_px),
            db_checked_u32_to_int("db_gl_read_pixels_rgba8", "y_px", y_px),
            db_checked_u32_to_int("db_gl_read_pixels_rgba8", "width", width),
            db_checked_u32_to_int("db_gl_read_pixels_rgba8", "height", height),
            GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    }
}

void db_gl_read_pixels_rgba16f(uint32_t x_px, uint32_t y_px, uint32_t width,
                               uint32_t height, uint16_t *pixels) {
    if ((width == 0U) || (height == 0U)) {
        return;
    }
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.read_pixels != NULL) {
        g_upload_proc_table.read_pixels(
            db_checked_u32_to_int("db_gl_read_pixels_rgba16f", "x_px", x_px),
            db_checked_u32_to_int("db_gl_read_pixels_rgba16f", "y_px", y_px),
            db_checked_u32_to_int("db_gl_read_pixels_rgba16f", "width", width),
            db_checked_u32_to_int("db_gl_read_pixels_rgba16f", "height",
                                  height),
            GL_RGBA, GL_HALF_FLOAT, pixels);
    }
}

void db_gl_set_texture_2d_enabled(int enabled) {
    db_gl_load_upload_proc_table();
    db_gl_set_capability_enabled_cached_valid(GL_TEXTURE_2D, enabled,
                                              &g_texture2d_enabled_state,
                                              &g_texture2d_enabled_state_valid);
}

void db_gl_set_client_state_vertex_array_enabled(int enabled) {
    db_gl_load_upload_proc_table();
    gl_set_client_state_enabled_cached(GL_VERTEX_ARRAY, enabled,
                                       &g_client_state_vertex_array_enabled);
}

void db_gl_set_client_state_color_array_enabled(int enabled) {
    db_gl_load_upload_proc_table();
    gl_set_client_state_enabled_cached(GL_COLOR_ARRAY, enabled,
                                       &g_client_state_color_array_enabled);
}

void db_gl_set_client_state_texcoord_array_enabled(int enabled) {
    db_gl_load_upload_proc_table();
    gl_set_client_state_enabled_cached(GL_TEXTURE_COORD_ARRAY, enabled,
                                       &g_client_state_texcoord_array_enabled);
}

void db_gl_set_vertex_pointer_2f(size_t stride_bytes, const void *pointer) {
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.vertex_pointer != NULL) {
        g_upload_proc_table.vertex_pointer(
            2, GL_FLOAT,
            db_checked_size_to_i32("db_gl_set_vertex_pointer_2f",
                                   "stride_bytes", stride_bytes),
            pointer);
    }
}

void db_gl_set_color_pointer_f(uint32_t component_count, size_t stride_bytes,
                               const void *pointer) {
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.color_pointer != NULL) {
        g_upload_proc_table.color_pointer(
            db_checked_u32_to_int("db_gl_set_color_pointer_f",
                                  "component_count", component_count),
            GL_FLOAT,
            db_checked_size_to_i32("db_gl_set_color_pointer_f", "stride_bytes",
                                   stride_bytes),
            pointer);
    }
}

void db_gl_set_texcoord_pointer_2f(size_t stride_bytes, const void *pointer) {
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.tex_coord_pointer != NULL) {
        g_upload_proc_table.tex_coord_pointer(
            2, GL_FLOAT,
            db_checked_size_to_i32("db_gl_set_texcoord_pointer_2f",
                                   "stride_bytes", stride_bytes),
            pointer);
    }
}

void db_gl_draw_arrays_triangles(uint32_t first, uint32_t count) {
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.draw_arrays != NULL) {
        g_upload_proc_table.draw_arrays(
            GL_TRIANGLES,
            db_checked_u32_to_int("db_gl_draw_arrays_triangles", "first",
                                  first),
            db_checked_u32_to_int("db_gl_draw_arrays_triangles", "count",
                                  count));
    }
}

int db_gl_draw_arrays_triangles_instanced(uint32_t first, uint32_t count,
                                          uint32_t instance_count) {
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.draw_arrays_instanced == NULL) {
        return 0;
    }
    g_upload_proc_table.draw_arrays_instanced(
        GL_TRIANGLES,
        db_checked_u32_to_int("db_gl_draw_arrays_triangles_instanced", "first",
                              first),
        db_checked_u32_to_int("db_gl_draw_arrays_triangles_instanced", "count",
                              count),
        db_checked_u32_to_int("db_gl_draw_arrays_triangles_instanced",
                              "instance_count", instance_count));
    return 1;
}

void db_gl_draw_arrays_triangle_strip(uint32_t first, uint32_t count) {
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.draw_arrays != NULL) {
        g_upload_proc_table.draw_arrays(
            GL_TRIANGLE_STRIP,
            db_checked_u32_to_int("db_gl_draw_arrays_triangle_strip", "first",
                                  first),
            db_checked_u32_to_int("db_gl_draw_arrays_triangle_strip", "count",
                                  count));
    }
}

void db_gl_set_scissor_enabled(int enabled) {
    db_gl_load_upload_proc_table();
    if (enabled != 0) {
        g_upload_proc_table.enable(GL_SCISSOR_TEST);
    } else {
        g_upload_proc_table.disable(GL_SCISSOR_TEST);
    }
}

void db_gl_set_scissor(uint32_t x_px, uint32_t y_px, uint32_t width,
                       uint32_t height) {
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.scissor != NULL) {
        g_upload_proc_table.scissor(
            db_checked_u32_to_int("db_gl_set_scissor", "x_px", x_px),
            db_checked_u32_to_int("db_gl_set_scissor", "y_px", y_px),
            db_checked_u32_to_int("db_gl_set_scissor", "width", width),
            db_checked_u32_to_int("db_gl_set_scissor", "height", height));
    }
}

// 3) Wrapper APIs: shader/program and modern pipeline operations.
void db_gl_active_texture(unsigned int texture_unit) {
    db_gl_load_upload_proc_table();
    if ((g_active_texture_unit_valid != 0) &&
        (g_active_texture_unit == texture_unit)) {
        return;
    }
    if (g_upload_proc_table.active_texture != NULL) {
        g_upload_proc_table.active_texture((GLenum)texture_unit);
        g_active_texture_unit = texture_unit;
        g_active_texture_unit_valid = 1;
    }
}
