#include "core/db_geometry.h"
#include "core/db_numeric.h"
#include "gl_common.h"

#include <stddef.h>
#include <stdint.h>

static void db_gl_shadow_present_draw_with_origin(
    db_gl_shadow_present_state_t *state, uint32_t pixel_width,
    uint32_t pixel_height, int framebuffer_texture) {
    if ((state == NULL) || (state->texture == 0U) || (pixel_width == 0U) ||
        (pixel_height == 0U)) {
        return;
    }
    if ((state->present_damage_configured != 0) &&
        (state->present_damage_full == 0) &&
        (state->present_damage_block_count == 0U)) {
        return;
    }
    const float tex_u =
        (state->texture_width == 0U)
            ? 1.0F
            : db_u32_ratio_to_f32(pixel_width, state->texture_width);
    const float tex_v =
        (state->texture_height == 0U)
            ? 1.0F
            : db_u32_ratio_to_f32(pixel_height, state->texture_height);
    state->texcoords[DB_GL_QUAD_V0_X] = 0.0F;
    state->texcoords[DB_GL_QUAD_V0_Y] =
        (framebuffer_texture != 0) ? 0.0F : tex_v;
    state->texcoords[DB_GL_QUAD_V1_X] = tex_u;
    state->texcoords[DB_GL_QUAD_V1_Y] =
        (framebuffer_texture != 0) ? 0.0F : tex_v;
    state->texcoords[DB_GL_QUAD_V2_X] = 0.0F;
    state->texcoords[DB_GL_QUAD_V2_Y] =
        (framebuffer_texture != 0) ? tex_v : 0.0F;
    state->texcoords[DB_GL_QUAD_V3_X] = tex_u;
    state->texcoords[DB_GL_QUAD_V3_Y] =
        (framebuffer_texture != 0) ? tex_v : 0.0F;

    db_gl_prepare_textured_present_state();
    if (state->presentation_quad_uses_vbo != 0) {
        (void)db_gl_upload_stream_bind(&state->presentation_quad_stream);
        db_gl_set_vertex_pointer_2f(0, db_gl_vbo_offset_ptr(0U));
        (void)db_gl_upload_stream_unbind_target(
            DB_GL_UPLOAD_TARGET_VBO_ARRAY_BUFFER);
    } else {
        db_gl_set_vertex_pointer_2f(0, state->vertices);
    }
    db_gl_set_color_pointer_f(4, 0, state->colors);
    db_gl_set_texcoord_pointer_2f(0, state->texcoords);
    db_gl_texture_bind_2d(state->texture);
    const int use_damage = DB_BOOL((state->present_damage_configured != 0) &&
                                   (state->present_damage_full == 0) &&
                                   (state->present_damage_blocks != NULL) &&
                                   (state->present_damage_block_count > 0U));
    if (use_damage != 0) {
        db_gl_set_scissor_enabled(1);
        for (size_t index = 0U; index < state->present_damage_block_count;
             index++) {
            const db_damage_block_t block = state->present_damage_blocks[index];
            if ((block.row_count == 0U) || (block.col_count == 0U) ||
                (block.row_start >= pixel_height) ||
                (block.col_start >= pixel_width)) {
                continue;
            }
            const uint32_t row_count =
                DB_MIN(block.row_count, pixel_height - block.row_start);
            const uint32_t col_count =
                DB_MIN(block.col_count, pixel_width - block.col_start);
            const uint32_t scissor_y =
                pixel_height - (block.row_start + row_count);
            db_gl_set_scissor(block.col_start, scissor_y, col_count, row_count);
            db_gl_draw_arrays_triangle_strip(0, 4);
        }
        db_gl_set_scissor_enabled(0);
    } else {
        db_gl_draw_arrays_triangle_strip(0, 4);
    }
    db_gl_finish_textured_present_state();
}

void db_gl_shadow_present_draw(db_gl_shadow_present_state_t *state,
                               uint32_t pixel_width, uint32_t pixel_height) {
    db_gl_shadow_present_draw_with_origin(state, pixel_width, pixel_height, 0);
}

void db_gl_shadow_present_draw_framebuffer_texture(
    db_gl_shadow_present_state_t *state, uint32_t pixel_width,
    uint32_t pixel_height) {
    db_gl_shadow_present_draw_with_origin(state, pixel_width, pixel_height, 1);
}

void db_gl_shadow_present_set_draw_damage(db_gl_shadow_present_state_t *state,
                                          db_pixel_block_view_t damage,
                                          int force_full) {
    if (state == NULL) {
        return;
    }
    state->present_damage_blocks = damage.blocks;
    state->present_damage_block_count = damage.count;
    state->present_damage_full = DB_BOOL(force_full);
    state->present_damage_configured = 1;
}
