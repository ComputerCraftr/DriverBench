#include "../core/db_core.h"
#include "../core/db_numeric.h"
#include "renderer_benchmark_types.h"
#include "renderer_gl_api.h"
#include "renderer_gl_common.h"
#include "renderer_gl_probe_internal.h"
#include "renderer_gl_proc_runtime_internal.h"
#include <stddef.h>
#include <stdint.h>

void db_gl_shadow_present_init_runtime(db_gl_shadow_present_state_t *state,
                                       int prefer_unpack_pbo,
                                       int selected_content_uses_rgba16f) {
    if ((state == NULL) || (state->initialized != 0)) {
        return;
    }
    const int runtime_supports_hdr_present =
        (db_gl_context_probe_texture_float_present_support() != 0) ? 1 : 0;
    const db_gl_shadow_present_texture_format_t selected_texture_format =
        ((selected_content_uses_rgba16f != 0) &&
         (runtime_supports_hdr_present != 0))
            ? DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F
            : DB_GL_SHADOW_PRESENT_TEXTURE_RGBA8;
    const int supports_unpack_row_length_upload =
        (selected_texture_format == DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F)
            ? db_gl_probe_shadow_present_partial_upload_support_rgba16f()
            : db_gl_probe_shadow_present_partial_upload_support_rgba8();
    *state = (db_gl_shadow_present_state_t){
        .initialized = 1,
        .backing_valid = 0,
        .texture_valid = 0,
        .texture_needs_full_upload = 1,
        .runtime_supports_unpack_row_length_upload =
            (supports_unpack_row_length_upload != 0) ? 1 : 0,
        .runtime_supports_hdr_present = runtime_supports_hdr_present,
        .uses_exact_size_texture =
            (db_gl_context_supports_shadow_present_exact_size_texture_2d() != 0)
                ? 1
                : 0,
        .selected_texture_format = selected_texture_format,
        .unpack_pbo = db_gl_pbo_create_if_usable(prefer_unpack_pbo),
    };
    db_gl_quad_init(state->vertices);
    state->texcoords[DB_GL_QUAD_V0_X] = 0.0F;
    state->texcoords[DB_GL_QUAD_V0_Y] = 1.0F;
    state->texcoords[DB_GL_QUAD_V1_X] = 1.0F;
    state->texcoords[DB_GL_QUAD_V1_Y] = 1.0F;
    state->texcoords[DB_GL_QUAD_V2_X] = 0.0F;
    state->texcoords[DB_GL_QUAD_V2_Y] = 0.0F;
    state->texcoords[DB_GL_QUAD_V3_X] = 1.0F;
    state->texcoords[DB_GL_QUAD_V3_Y] = 0.0F;
    for (size_t i = 0U; i < (size_t)DB_RECT_VERTEX_COUNT; i++) {
        const size_t base = i * 4U;
        state->colors[base + 0U] = 1.0F;
        state->colors[base + 1U] = 1.0F;
        state->colors[base + 2U] = 1.0F;
        state->colors[base + 3U] = 1.0F;
    }
}

void db_gl_shadow_present_log_decision(
    const char *backend, const char *present_name, int content_uses_rgba16f,
    int hdr_explicit_requested, const db_gl_shadow_present_state_t *state) {
    if ((backend == NULL) || (present_name == NULL) || (state == NULL)) {
        return;
    }
    const int use_pbo = (state->unpack_pbo != 0U) ? 1 : 0;
    const char *const texture_size_mode =
        (state->uses_exact_size_texture != 0) ? "exact_size" : "pow2_fallback";
    const char *const partial_upload_mode =
        (state->runtime_supports_unpack_row_length_upload != 0)
            ? "row_length"
            : "rowwise_fallback";
    if (state->selected_texture_format ==
        DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F) {
        db_infof(backend,
                 "%s hdr=enabled reason=float texture present probe passed, "
                 "pbo=%s, hdr_explicit=%s, texture_sizing=%s, "
                 "partial_upload=%s",
                 present_name, (use_pbo != 0) ? "yes" : "no",
                 (hdr_explicit_requested != 0) ? "yes" : "no",
                 texture_size_mode, partial_upload_mode);
        return;
    }
    if (content_uses_rgba16f == 0) {
        db_infof(backend,
                 "%s hdr=disabled reason=content prefers sdr backbuffer, "
                 "pbo=%s, texture_sizing=%s, partial_upload=%s",
                 present_name, (use_pbo != 0) ? "yes" : "no", texture_size_mode,
                 partial_upload_mode);
        return;
    }
    if (state->runtime_supports_hdr_present == 0) {
        db_infof(backend,
                 "%s hdr=disabled reason=float texture present probe failed, "
                 "falling back to rgba8, pbo=%s, texture_sizing=%s, "
                 "partial_upload=%s",
                 present_name, (use_pbo != 0) ? "yes" : "no", texture_size_mode,
                 partial_upload_mode);
        return;
    }
    db_infof(backend,
             "%s hdr=disabled reason=present texture did not select rgba16f "
             "despite supported float present probe, pbo=%s, "
             "texture_sizing=%s, partial_upload=%s",
             present_name, (use_pbo != 0) ? "yes" : "no", texture_size_mode,
             partial_upload_mode);
}

void db_gl_shadow_present_shutdown(db_gl_shadow_present_state_t *state) {
    if (state == NULL) {
        return;
    }
    if (state->unpack_pbo != 0U) {
        db_gl_pbo_delete_if_valid(state->unpack_pbo);
        state->unpack_pbo = 0U;
    }
    db_gl_texture_delete_if_valid(&state->texture);
    *state = (db_gl_shadow_present_state_t){0};
}

void db_gl_shadow_present_prepare_texture(db_gl_shadow_present_state_t *state,
                                          const char *backend,
                                          uint32_t pixel_width,
                                          uint32_t pixel_height) {
    if ((state == NULL) || (backend == NULL) || (pixel_width == 0U) ||
        (pixel_height == 0U)) {
        return;
    }
    const uint32_t target_width = (state->uses_exact_size_texture != 0)
                                      ? pixel_width
                                      : db_u32_next_pow2(pixel_width);
    const uint32_t target_height = (state->uses_exact_size_texture != 0)
                                       ? pixel_height
                                       : db_u32_next_pow2(pixel_height);
    const int content_size_changed = (state->content_width != pixel_width) ||
                                     (state->content_height != pixel_height);
    const int needs_recreate = (state->texture == 0U) ||
                               (state->texture_width < target_width) ||
                               (state->texture_height < target_height);
    if (needs_recreate == 0) {
        if (content_size_changed != 0) {
            state->content_width = pixel_width;
            state->content_height = pixel_height;
            state->texture_valid = 0;
            state->texture_needs_full_upload = 1;
        }
        return;
    }

    // Texture allocation with NULL pixels must not inherit a bound unpack PBO.
    if (state->unpack_pbo != 0U) {
        db_gl_pbo_unbind_unpack();
    }

    state->texture_width = target_width;
    state->texture_height = target_height;
    state->content_width = pixel_width;
    state->content_height = pixel_height;
    if (state->texture != 0U) {
        db_gl_texture_delete_if_valid(&state->texture);
    }
    const int created =
        (state->selected_texture_format == DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F)
            ? db_gl_texture_create_rgba16f(
                  &state->texture,
                  db_checked_u32_to_i32(backend, "shadow_tex_width",
                                        state->texture_width),
                  db_checked_u32_to_i32(backend, "shadow_tex_height",
                                        state->texture_height),
                  NULL)
            : db_gl_texture_create_rgba8(
                  &state->texture,
                  db_checked_u32_to_i32(backend, "shadow_tex_width",
                                        state->texture_width),
                  db_checked_u32_to_i32(backend, "shadow_tex_height",
                                        state->texture_height),
                  NULL);
    if (created == 0) {
        db_failf(backend, "failed to create shared shadow texture");
    }
    state->texture_valid = 0;
    state->texture_needs_full_upload = 1;
}

void db_gl_set_unpack_row_length_pixels(int pixel_count) {
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.pixel_storei != NULL) {
        g_upload_proc_table.pixel_storei(GL_UNPACK_ROW_LENGTH, pixel_count);
    }
}

void db_gl_shadow_present_upload_damage_blocks(
    const db_gl_shadow_present_state_t *state, const char *backend,
    const void *selected_pixels, uint32_t pixel_width, uint32_t pixel_height,
    const db_damage_block_t *blocks, size_t block_count) {
    if ((state == NULL) || (backend == NULL) || (state->texture == 0U) ||
        (pixel_width == 0U) || (pixel_height == 0U) || (blocks == NULL) ||
        (block_count == 0U)) {
        return;
    }
    if (selected_pixels == NULL) {
        return;
    }

    const uint32_t pixel_bytes =
        (state->selected_texture_format == DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F)
            ? (uint32_t)(sizeof(uint16_t) * 4U)
            : 4U;
    const size_t row_bytes_size = db_checked_mul_size(
        backend, "shadow_row_bytes", (size_t)pixel_width, (size_t)pixel_bytes);
    const size_t total_bytes =
        db_checked_mul_size(backend, "shadow_upload_total_bytes",
                            row_bytes_size, (size_t)pixel_height);
    const size_t max_gl_shadow_bytes = PTRDIFF_MAX;
    if (total_bytes > max_gl_shadow_bytes) {
        db_failf(backend, "shadow_upload_total_bytes too large: %zu",
                 total_bytes);
    }
    const int use_pbo = (state->unpack_pbo != 0U) ? 1 : 0;
    const int use_unpack_row_length =
        (state->runtime_supports_unpack_row_length_upload != 0) ? 1 : 0;
    const uint32_t row_bytes = db_checked_mul_u32(backend, "shadow_row_bytes",
                                                  pixel_width, pixel_bytes);
    db_gl_texture_bind_2d(state->texture);
    db_gl_set_unpack_alignment_1();
    for (size_t i = 0U; i < block_count; i++) {
        const db_damage_block_t block = blocks[i];
        if ((block.row_count == 0U) || (block.col_count == 0U)) {
            continue;
        }
        const uint32_t row_end = db_u32_min(
            pixel_height, db_checked_add_u32(backend, "shadow_row_end",
                                             block.row_start, block.row_count));
        const uint32_t col_end = db_u32_min(
            pixel_width, db_checked_add_u32(backend, "shadow_col_end",
                                            block.col_start, block.col_count));
        if ((row_end <= block.row_start) || (col_end <= block.col_start)) {
            continue;
        }
        const uint32_t row_count = row_end - block.row_start;
        const uint32_t col_count = col_end - block.col_start;
        const uint32_t block_row_bytes = db_checked_mul_u32(
            backend, "shadow_block_row_bytes", col_count, pixel_bytes);
        const size_t src_offset_bytes =
            ((size_t)block.row_start * (size_t)row_bytes) +
            ((size_t)block.col_start * (size_t)pixel_bytes);
        if (use_unpack_row_length != 0) {
            db_gl_set_unpack_row_length_pixels(db_checked_u32_to_i32(
                backend, "shadow_unpack_row_length", pixel_width));
            if (use_pbo != 0) {
                const size_t block_bytes =
                    ((size_t)(row_count - 1U) * (size_t)row_bytes) +
                    (size_t)block_row_bytes;
                db_gl_upload_buffer_target(
                    ((const uint8_t *)selected_pixels) + src_offset_bytes,
                    block_bytes, DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER,
                    state->unpack_pbo, 0, NULL, 0, 0);
                if (state->selected_texture_format ==
                    DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F) {
                    db_gl_texture_sub_image_2d_rgba16f(
                        db_checked_u32_to_i32(backend, "shadow_upload_x",
                                              block.col_start),
                        db_checked_u32_to_i32(backend, "shadow_upload_y",
                                              block.row_start),
                        db_checked_u32_to_i32(backend, "shadow_upload_w",
                                              col_count),
                        db_checked_u32_to_i32(backend, "shadow_upload_h",
                                              row_count),
                        db_gl_vbo_offset_ptr(0U));
                } else {
                    db_gl_texture_sub_image_2d_rgba(
                        db_checked_u32_to_i32(backend, "shadow_upload_x",
                                              block.col_start),
                        db_checked_u32_to_i32(backend, "shadow_upload_y",
                                              block.row_start),
                        db_checked_u32_to_i32(backend, "shadow_upload_w",
                                              col_count),
                        db_checked_u32_to_i32(backend, "shadow_upload_h",
                                              row_count),
                        db_gl_vbo_offset_ptr(0U));
                }
            } else {
                const void *pixels_ptr =
                    (state->selected_texture_format ==
                     DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F)
                        ? (const void *)(((const uint16_t *)selected_pixels) +
                                         (src_offset_bytes /
                                          (sizeof(uint16_t) * 4U)))
                        : (const void *)(((const uint8_t *)selected_pixels) +
                                         src_offset_bytes);
                if (state->selected_texture_format ==
                    DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F) {
                    db_gl_texture_sub_image_2d_rgba16f(
                        db_checked_u32_to_i32(backend, "shadow_upload_x",
                                              block.col_start),
                        db_checked_u32_to_i32(backend, "shadow_upload_y",
                                              block.row_start),
                        db_checked_u32_to_i32(backend, "shadow_upload_w",
                                              col_count),
                        db_checked_u32_to_i32(backend, "shadow_upload_h",
                                              row_count),
                        pixels_ptr);
                } else {
                    db_gl_texture_sub_image_2d_rgba(
                        db_checked_u32_to_i32(backend, "shadow_upload_x",
                                              block.col_start),
                        db_checked_u32_to_i32(backend, "shadow_upload_y",
                                              block.row_start),
                        db_checked_u32_to_i32(backend, "shadow_upload_w",
                                              col_count),
                        db_checked_u32_to_i32(backend, "shadow_upload_h",
                                              row_count),
                        pixels_ptr);
                }
            }
            db_gl_set_unpack_row_length_pixels(0);
            continue;
        }
        for (uint32_t row = block.row_start; row < row_end; row++) {
            const size_t row_src_offset_bytes =
                ((size_t)row * (size_t)row_bytes) +
                ((size_t)block.col_start * (size_t)pixel_bytes);
            if (use_pbo != 0) {
                db_gl_upload_buffer_target(
                    ((const uint8_t *)selected_pixels) + row_src_offset_bytes,
                    (size_t)block_row_bytes,
                    DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER, state->unpack_pbo, 0,
                    NULL, 0, 0);
                if (state->selected_texture_format ==
                    DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F) {
                    db_gl_texture_sub_image_2d_rgba16f(
                        db_checked_u32_to_i32(backend, "shadow_upload_x",
                                              block.col_start),
                        db_checked_u32_to_i32(backend, "shadow_upload_y", row),
                        db_checked_u32_to_i32(backend, "shadow_upload_w",
                                              col_count),
                        1, db_gl_vbo_offset_ptr(0U));
                } else {
                    db_gl_texture_sub_image_2d_rgba(
                        db_checked_u32_to_i32(backend, "shadow_upload_x",
                                              block.col_start),
                        db_checked_u32_to_i32(backend, "shadow_upload_y", row),
                        db_checked_u32_to_i32(backend, "shadow_upload_w",
                                              col_count),
                        1, db_gl_vbo_offset_ptr(0U));
                }
            } else if (state->selected_texture_format ==
                       DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F) {
                const uint16_t *pixels_ptr =
                    ((const uint16_t *)selected_pixels) +
                    (row_src_offset_bytes / (sizeof(uint16_t) * 4U));
                db_gl_texture_sub_image_2d_rgba16f(
                    db_checked_u32_to_i32(backend, "shadow_upload_x",
                                          block.col_start),
                    db_checked_u32_to_i32(backend, "shadow_upload_y", row),
                    db_checked_u32_to_i32(backend, "shadow_upload_w",
                                          col_count),
                    1, pixels_ptr);
            } else {
                const uint8_t *pixels_ptr =
                    ((const uint8_t *)selected_pixels) + row_src_offset_bytes;
                db_gl_texture_sub_image_2d_rgba(
                    db_checked_u32_to_i32(backend, "shadow_upload_x",
                                          block.col_start),
                    db_checked_u32_to_i32(backend, "shadow_upload_y", row),
                    db_checked_u32_to_i32(backend, "shadow_upload_w",
                                          col_count),
                    1, pixels_ptr);
            }
        }
    }
    if (use_unpack_row_length != 0) {
        db_gl_set_unpack_row_length_pixels(0);
    }
    if (use_pbo != 0) {
        db_gl_pbo_unbind_unpack();
    }
}

void db_gl_shadow_present_frame(const db_gl_shadow_present_frame_t *frame) {
    if ((frame == NULL) || (frame->state == NULL) || (frame->backend == NULL) ||
        (frame->pixel_width == 0U) || (frame->pixel_height == 0U)) {
        return;
    }
    db_gl_shadow_present_prepare_texture(
        frame->state, frame->backend, frame->pixel_width, frame->pixel_height);
    if (frame->state->texture == 0U) {
        db_failf(frame->backend,
                 "shared shadow present texture is not initialized");
    }
    if (frame->selected_pixels == NULL) {
        db_failf(frame->backend, "shared shadow present pixels are missing");
    }
    if (frame->prepare_upload_target_fn != NULL) {
        frame->prepare_upload_target_fn(frame->state, frame->pixel_width,
                                        frame->pixel_height,
                                        frame->prepare_upload_target_user_data);
    }

    if ((frame->state->texture_needs_full_upload != 0) ||
        ((frame->state->texture_valid == 0) &&
         ((frame->damage_blocks == NULL) ||
          (frame->damage_block_count == 0U)))) {
        const db_damage_block_t full_block =
            db_damage_block_full(frame->pixel_height, frame->pixel_width);
        db_gl_shadow_present_upload_damage_blocks(
            frame->state, frame->backend, frame->selected_pixels,
            frame->pixel_width, frame->pixel_height, &full_block, 1U);
        frame->state->texture_valid = 1;
        frame->state->texture_needs_full_upload = 0;
    } else if ((frame->damage_blocks != NULL) &&
               (frame->damage_block_count > 0U)) {
        db_gl_shadow_present_upload_damage_blocks(
            frame->state, frame->backend, frame->selected_pixels,
            frame->pixel_width, frame->pixel_height, frame->damage_blocks,
            frame->damage_block_count);
        frame->state->texture_valid = 1;
    }

    db_gl_shadow_present_draw(frame->state, frame->pixel_width,
                              frame->pixel_height);
}

void db_gl_shadow_present_draw(db_gl_shadow_present_state_t *state,
                               uint32_t pixel_width, uint32_t pixel_height) {
    if ((state == NULL) || (state->texture == 0U) || (pixel_width == 0U) ||
        (pixel_height == 0U)) {
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
    state->texcoords[DB_GL_QUAD_V0_Y] = tex_v;
    state->texcoords[DB_GL_QUAD_V1_X] = tex_u;
    state->texcoords[DB_GL_QUAD_V1_Y] = tex_v;
    state->texcoords[DB_GL_QUAD_V2_X] = 0.0F;
    state->texcoords[DB_GL_QUAD_V2_Y] = 0.0F;
    state->texcoords[DB_GL_QUAD_V3_X] = tex_u;
    state->texcoords[DB_GL_QUAD_V3_Y] = 0.0F;

    db_gl_set_depth_test_enabled(0);
    db_gl_set_cull_face_enabled(0);
    db_gl_set_blend_enabled(0);
    db_gl_set_texture_2d_enabled(1);
    db_gl_set_client_state_vertex_array_enabled(1);
    db_gl_set_client_state_color_array_enabled(1);
    db_gl_set_client_state_texcoord_array_enabled(1);
    db_gl_set_vertex_pointer_2f(0, state->vertices);
    db_gl_set_color_pointer_f(4, 0, state->colors);
    db_gl_set_texcoord_pointer_2f(0, state->texcoords);
    db_gl_texture_bind_2d(state->texture);
    db_gl_draw_arrays_triangle_strip(0, 4);
    db_gl_texture_bind_2d(0U);
    db_gl_set_texture_2d_enabled(0);
    db_gl_set_client_state_texcoord_array_enabled(0);
}

void db_gl_set_viewport_px(int width_px, int height_px) {
    if ((width_px <= 0) || (height_px <= 0) ||
        (g_upload_proc_table.viewport == NULL)) {
        return;
    }
    g_upload_proc_table.viewport(0, 0, width_px, height_px);
}
