#include "../core/db_numeric.h"
#include "core/db_render_types.h"
#include "gl_api.h"
#include "gl_common.h"
#include "gl_probe_internal.h"
#include "gl_proc_runtime.h"
#include <stddef.h>
#include <stdint.h>

int db_gl_context_supports_unpack_row_length_upload(void) {
    const db_gl_runtime_metadata_t runtime = db_gl_runtime_metadata_load();
    if (g_upload_proc_table.pixel_storei == NULL) {
        return 0;
    }
    if (db_gl_runtime_has_usable_version(&runtime) == 0) {
        return 0;
    }
    if (db_gl_runtime_is_es_context(&runtime) != 0) {
        return db_gl_runtime_supports_es_core_or_extension(
            &runtime, 3, 0, "GL_EXT_unpack_subimage");
    }
    // Desktop GL exposes GL_UNPACK_ROW_LENGTH in the core pixel-store API from
    // GL 1.2 onward.
    return db_gl_runtime_version_at_least(&runtime, 1, 2);
}

static int
db_gl_probe_partial_upload_draw_and_check(unsigned int probe_texture) {
    uint8_t probe_readback[4U * 4U * 4U] = {0U};
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
    db_gl_set_viewport_px(4, 4);
    db_gl_clear_color_rgba(0.0F, 0.0F, 0.0F, 1.0F);
    db_gl_clear_color_buffer();
    db_gl_prepare_textured_present_state();
    db_gl_set_vertex_pointer_2f(0, probe_vertices);
    db_gl_set_color_pointer_f(4, 0, probe_colors);
    db_gl_set_texcoord_pointer_2f(0, probe_texcoords);
    db_gl_texture_bind_2d(probe_texture);
    db_gl_draw_arrays_triangle_strip(0, 4);
    db_gl_read_pixels_rgba8(0, 0, 4, 4, probe_readback);
    const int draw_ok = db_gl_probe_step_error_free();

    db_gl_finish_textured_present_state();
    db_gl_set_client_state_color_array_enabled(0);
    db_gl_set_client_state_vertex_array_enabled(0);
    db_gl_set_viewport_px(viewport[2], viewport[3]);
    db_gl_texture_delete_if_valid(&probe_texture);
    if (draw_ok == 0) {
        return db_gl_probe_finish(0);
    }

    const size_t center0 = db_rgba4_channel_offset(4U, 1U, 1U);
    const size_t center1 = db_rgba4_channel_offset(4U, 2U, 1U);
    const size_t center2 = db_rgba4_channel_offset(4U, 1U, 2U);
    const size_t center3 = db_rgba4_channel_offset(4U, 2U, 2U);
    return db_gl_probe_finish(
        db_gl_probe_rgb_matches(probe_readback, center0, 1U, 0U, 0U) &&
        db_gl_probe_rgb_matches(probe_readback, center1, 0U, 1U, 0U) &&
        db_gl_probe_rgb_matches(probe_readback, center2, 1U, 1U, 0U) &&
        db_gl_probe_rgb_matches(probe_readback, center3, 1U, 0U, 1U));
}

static int db_gl_probe_shadow_present_partial_upload_support_rgba8(void) {
    if (db_gl_context_supports_unpack_row_length_upload() == 0) {
        return 0;
    }

    db_gl_probe_drain_errors();
    unsigned int probe_texture = 0U;
    if (db_gl_texture_create_rgba8(&probe_texture, 4, 4, NULL) == 0) {
        return db_gl_probe_finish(0);
    }

    static const uint8_t k_base_rgba8[4U * 4U * 4U] = {
        0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255,
        0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255,
        0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255,
        0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255,
    };
    static const uint8_t k_patch_rgba8[4U * 4U * 4U] = {
        0, 0, 255, 255, 0,   0,   255, 255, 0,   0,   255, 255, 0, 0, 255, 255,
        0, 0, 255, 255, 255, 0,   0,   255, 0,   255, 0,   255, 0, 0, 255, 255,
        0, 0, 255, 255, 255, 255, 0,   255, 255, 0,   255, 255, 0, 0, 255, 255,
        0, 0, 255, 255, 0,   0,   255, 255, 0,   0,   255, 255, 0, 0, 255, 255,
    };
    db_gl_texture_bind_2d(probe_texture);
    db_gl_texture_sub_image_2d_rgba(0, 0, 4, 4, k_base_rgba8);
    if (db_gl_probe_step_error_free() == 0) {
        db_gl_texture_delete_if_valid(&probe_texture);
        db_gl_texture_bind_2d(0U);
        return db_gl_probe_finish(0);
    }
    db_gl_set_unpack_alignment_1();
    db_gl_set_unpack_row_length_pixels(4);
    db_gl_texture_sub_image_2d_rgba(
        1, 1, 2, 2, &k_patch_rgba8[db_rgba4_channel_offset(4U, 1U, 1U)]);
    db_gl_set_unpack_row_length_pixels(0);
    if (db_gl_probe_step_error_free() == 0) {
        db_gl_texture_delete_if_valid(&probe_texture);
        db_gl_texture_bind_2d(0U);
        return db_gl_probe_finish(0);
    }

    return db_gl_probe_partial_upload_draw_and_check(probe_texture);
}

static int db_gl_probe_shadow_present_partial_upload_support_rgba16f(void) {
    if ((db_gl_context_supports_unpack_row_length_upload() == 0) ||
        (db_gl_context_probe_texture_float_present_support() == 0)) {
        return 0;
    }

    db_gl_probe_drain_errors();
    unsigned int probe_texture = 0U;
    if (db_gl_probe_texture_create_rgba16f(&probe_texture, 4, 4) == 0) {
        return db_gl_probe_finish(0);
    }

    static const uint16_t k_base_rgba16f[4U * 4U * 4U] = {
        0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE, 0x0000U,    0x0000U,
        DB_F16_ONE, DB_F16_ONE, 0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE,
        0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE, 0x0000U,    0x0000U,
        DB_F16_ONE, DB_F16_ONE, 0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE,
        0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE, 0x0000U,    0x0000U,
        DB_F16_ONE, DB_F16_ONE, 0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE,
        0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE, 0x0000U,    0x0000U,
        DB_F16_ONE, DB_F16_ONE, 0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE,
        0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE, 0x0000U,    0x0000U,
        DB_F16_ONE, DB_F16_ONE, 0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE,
        0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE,
    };
    static const uint16_t k_patch_rgba16f[4U * 4U * 4U] = {
        0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE, 0x0000U,    0x0000U,
        DB_F16_ONE, DB_F16_ONE, 0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE,
        0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE, 0x0000U,    0x0000U,
        DB_F16_ONE, DB_F16_ONE, DB_F16_ONE, 0x0000U,    0x0000U,    DB_F16_ONE,
        0x0000U,    DB_F16_ONE, 0x0000U,    DB_F16_ONE, 0x0000U,    0x0000U,
        DB_F16_ONE, DB_F16_ONE, 0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE,
        DB_F16_ONE, DB_F16_ONE, 0x0000U,    DB_F16_ONE, DB_F16_ONE, 0x0000U,
        DB_F16_ONE, DB_F16_ONE, 0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE,
        0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE, 0x0000U,    0x0000U,
        DB_F16_ONE, DB_F16_ONE, 0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE,
        0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE,
    };
    db_gl_texture_bind_2d(probe_texture);
    db_gl_texture_sub_image_2d_rgba16f(0, 0, 4, 4, k_base_rgba16f);
    if (db_gl_probe_step_error_free() == 0) {
        db_gl_texture_delete_if_valid(&probe_texture);
        db_gl_texture_bind_2d(0U);
        return db_gl_probe_finish(0);
    }
    db_gl_set_unpack_alignment_1();
    db_gl_set_unpack_row_length_pixels(4);
    db_gl_texture_sub_image_2d_rgba16f(
        1, 1, 2, 2, &k_patch_rgba16f[db_rgba4_channel_offset(4U, 1U, 1U)]);
    db_gl_set_unpack_row_length_pixels(0);
    if (db_gl_probe_step_error_free() == 0) {
        db_gl_texture_delete_if_valid(&probe_texture);
        db_gl_texture_bind_2d(0U);
        return db_gl_probe_finish(0);
    }

    return db_gl_probe_partial_upload_draw_and_check(probe_texture);
}

int db_gl_probe_shadow_present_partial_upload_support(
    db_pixel_format_t format) {
    switch (format) {
    case DB_PIXEL_FORMAT_RGBA8:
        return db_gl_probe_shadow_present_partial_upload_support_rgba8();
    case DB_PIXEL_FORMAT_RGBA16F:
        return db_gl_probe_shadow_present_partial_upload_support_rgba16f();
    }
    return 0;
}

int db_gl_context_supports_full_npot_texture_2d(void) {
    const db_gl_runtime_metadata_t runtime = db_gl_runtime_metadata_load();
    if (db_gl_runtime_has_usable_version(&runtime) == 0) {
        return 0;
    }
    if (db_gl_runtime_is_es_context(&runtime) != 0) {
        return db_gl_runtime_has_extension(&runtime, "GL_OES_texture_npot");
    }
    return db_gl_runtime_supports_desktop_core_or_extension(
        &runtime, 2, 0, "GL_ARB_texture_non_power_of_two");
}

int db_gl_context_supports_shadow_present_exact_size_texture_2d(void) {
    const db_gl_runtime_metadata_t runtime = db_gl_runtime_metadata_load();
    if (db_gl_runtime_has_usable_version(&runtime) == 0) {
        return 0;
    }
    if (db_gl_context_supports_full_npot_texture_2d() != 0) {
        return 1;
    }
    if (db_gl_runtime_is_es_context(&runtime) != 0) {
        // ES 2.0 permits NPOT 2D textures for non-mipmapped CLAMP_TO_EDGE
        // usage. The shadow-present path only uses GL_NEAREST +
        // GL_CLAMP_TO_EDGE and never enables mipmaps.
        return db_gl_runtime_version_at_least(&runtime, 2, 0);
    }
    return 0;
}
