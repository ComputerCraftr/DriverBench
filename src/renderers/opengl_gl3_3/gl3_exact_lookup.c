#include "gl3_exact_lookup.h"

#include "core/db_core.h"
#include "core/db_hash.h"
#include "core/db_numeric.h"
#include "core/db_render_ir.h"
#include "core/db_render_types.h"
#include "gl3_target.h"
#include "renderers/gl_api.h"
#include "renderers/gl_common.h"
#include "renderers/gl_proc_runtime.h"

#include "db_embedded_shaders.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define BACKEND_NAME "renderer_opengl_gl3_3"
#define DB_GL3_LOOKUP_SHADER_DOMAIN UINT32_C(0x47334C55)

enum { DB_GL3_RGBA8_ALPHA_SHIFT = 24U };

static int gl3_lookup_api_supported(db_pixel_format_t format) {
    db_gl_load_upload_proc_table();
    const db_gl_runtime_metadata_t runtime = db_gl_runtime_metadata_load();
    const int texture_buffer = DB_BOOL(
        ((runtime.version_major > 3) ||
         ((runtime.version_major == 3) && (runtime.version_minor >= 1)) ||
         db_gl_runtime_has_extension(&runtime,
                                     "GL_ARB_texture_buffer_object")) &&
        (g_upload_proc_table.tex_buffer != NULL));
    if ((texture_buffer == 0) || (format == DB_PIXEL_FORMAT_RGBA8)) {
        return texture_buffer;
    }
    return DB_BOOL(
        (runtime.version_major > 4) ||
        ((runtime.version_major == 4) && (runtime.version_minor >= 2)) ||
        db_gl_runtime_has_extension(&runtime,
                                    "GL_ARB_shading_language_packing"));
}

uint32_t db_gl3_exact_pack_rgba8(const double *rgba) {
    uint8_t bytes[4] = {0};
    db_rgba01_to_u8_rgba4(rgba, bytes);
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) |
           ((uint32_t)bytes[3] << DB_GL3_RGBA8_ALPHA_SHIFT);
}

void db_gl3_exact_pack_rgba16f(const double *rgba, uint32_t *words) {
    const uint16_t red = db_f64_to_f16_via_f32(rgba[0]);
    const uint16_t green = db_f64_to_f16_via_f32(rgba[1]);
    const uint16_t blue = db_f64_to_f16_via_f32(rgba[2]);
    const uint16_t alpha = db_f64_to_f16_via_f32(rgba[3]);
    words[0] = (uint32_t)red | ((uint32_t)green << 16U);
    words[1] = (uint32_t)blue | ((uint32_t)alpha << 16U);
}

int db_gl3_exact_lookup_capacity_supported(size_t required_rows,
                                           int maximum_texels) {
    return DB_BOOL((required_rows != 0U) && (maximum_texels > 0) &&
                   (required_rows <=
                    (size_t)db_nonnegative_int_to_u32_or_zero(maximum_texels)));
}

int db_gl3_exact_lookup_init(gl3_exact_lookup_t *lookup,
                             db_pixel_format_t format, size_t row_capacity) {
    if ((lookup == NULL) || (row_capacity == 0U) ||
        ((format != DB_PIXEL_FORMAT_RGBA8) &&
         (format != DB_PIXEL_FORMAT_RGBA16F))) {
        return 0;
    }
    *lookup = (gl3_exact_lookup_t){
        .format = format,
        .row_capacity = row_capacity,
        .words_per_row = (format == DB_PIXEL_FORMAT_RGBA16F) ? 2U : 1U,
        .unavailable_reason = "texture_buffer_unavailable",
    };
    if (gl3_lookup_api_supported(format) == 0) {
        return 1;
    }
    db_gl_get_integerv(GL_MAX_TEXTURE_BUFFER_SIZE, &lookup->max_texture_rows);
    if (db_gl3_exact_lookup_capacity_supported(row_capacity,
                                               lookup->max_texture_rows) == 0) {
        lookup->unavailable_reason = "texture_buffer_capacity";
        return 1;
    }
    const size_t word_count =
        db_checked_mul_size(BACKEND_NAME, "lookup_word_capacity", row_capacity,
                            lookup->words_per_row);
    const size_t byte_count = db_checked_mul_size(
        BACKEND_NAME, "lookup_byte_capacity", word_count, sizeof(uint32_t));
    lookup->words = (uint32_t *)calloc(word_count, sizeof(uint32_t));
    if (lookup->words == NULL) {
        lookup->unavailable_reason = "lookup_allocation_failed";
        return 1;
    }
    const db_gl_stream_upload_capability_t capability =
        db_gl_stream_upload_capability_probe(DB_GL_UPLOAD_TARGET_TEXTURE_BUFFER,
                                             byte_count, lookup->words, 1);
    db_gl_upload_stream_init(
        &lookup->stream, DB_GL_UPLOAD_TARGET_TEXTURE_BUFFER, capability, 0U, 1);
    lookup->stream.hot_path_fixed_capacity_bytes = byte_count;
    if ((db_gl_upload_stream_create_owned_buffer(&lookup->stream,
                                                 BACKEND_NAME) == 0) ||
        (db_gl_upload_stream_prepare_storage(&lookup->stream, BACKEND_NAME,
                                             byte_count) == 0)) {
        lookup->unavailable_reason = "lookup_buffer_unavailable";
        return 1;
    }
    const unsigned int internal_format =
        (format == DB_PIXEL_FORMAT_RGBA16F) ? GL_RG32UI : GL_R32UI;
    if (db_gl_texture_buffer_create(&lookup->texture, lookup->stream.buffer,
                                    internal_format) == 0) {
        lookup->unavailable_reason = "lookup_texture_unavailable";
        return 1;
    }
    const char *const fragment = (format == DB_PIXEL_FORMAT_RGBA16F)
                                     ? db_gl3_exact_lookup_rgba16f_frag_source
                                     : db_gl3_exact_lookup_rgba8_frag_source;
    lookup->program =
        db_gl3_build_program(db_gl3_exact_lookup_vert_source, fragment);
    lookup->sampler_location =
        db_gl_get_uniform_location(lookup->program, "row_lookup");
    lookup->available =
        DB_BOOL((lookup->program != 0U) && (lookup->sampler_location >= 0));
    lookup->unavailable_reason =
        (lookup->available != 0) ? "none" : "lookup_shader_unavailable";
    return 1;
}

void db_gl3_exact_lookup_reset(gl3_exact_lookup_t *lookup) {
    if (lookup != NULL) {
        lookup->row_count = 0U;
    }
}

int db_gl3_exact_lookup_append(
    gl3_exact_lookup_t *lookup,
    const db_render_ir_linear_gradient_command_t *gradient,
    uint32_t *lookup_base) {
    int32_t x_end = 0;
    int32_t y_end = 0;
    if ((lookup == NULL) || (lookup->available == 0) || (gradient == NULL) ||
        (lookup_base == NULL) || (lookup->words == NULL) ||
        (db_render_ir_rect_endpoints(gradient->bounds, &x_end, &y_end) == 0) ||
        ((lookup->format == DB_PIXEL_FORMAT_RGBA8) &&
         (lookup->words_per_row != 1U)) ||
        ((lookup->format == DB_PIXEL_FORMAT_RGBA16F) &&
         (lookup->words_per_row != 2U))) {
        return 0;
    }
    const size_t row_count =
        db_positive_i32_to_size_or_zero(gradient->bounds.height);
    if ((row_count > lookup->row_capacity) ||
        (lookup->row_count > lookup->row_capacity - row_count)) {
        return 0;
    }
    *lookup_base =
        db_checked_size_to_u32(BACKEND_NAME, "lookup_base", lookup->row_count);
    for (size_t row_offset = 0U; row_offset < row_count; row_offset++) {
        const int32_t logical_row =
            gradient->bounds.y + db_checked_size_to_i32(BACKEND_NAME,
                                                        "lookup_row_offset",
                                                        row_offset);
        const db_render_ir_color_t color =
            db_render_ir_linear_gradient_color_at(gradient, logical_row);
        uint32_t *const destination =
            &lookup->words[(lookup->row_count + row_offset) *
                           lookup->words_per_row];
        if (lookup->format == DB_PIXEL_FORMAT_RGBA16F) {
            db_gl3_exact_pack_rgba16f(color.rgba, destination);
        } else {
            destination[0] = db_gl3_exact_pack_rgba8(color.rgba);
        }
    }
    lookup->row_count += row_count;
    return 1;
}

int db_gl3_exact_lookup_upload(gl3_exact_lookup_t *lookup,
                               size_t *uploaded_bytes) {
    if ((lookup == NULL) || (lookup->available == 0) ||
        (uploaded_bytes == NULL)) {
        return 0;
    }
    *uploaded_bytes = db_checked_mul_size(
        BACKEND_NAME, "lookup_upload_bytes",
        db_checked_mul_size(BACKEND_NAME, "lookup_upload_words",
                            lookup->row_count, lookup->words_per_row),
        sizeof(uint32_t));
    if (*uploaded_bytes == 0U) {
        return 1;
    }
    if ((db_gl_upload_stream_wait(&lookup->stream) == 0) ||
        (db_gl_upload_stream_write(&lookup->stream, BACKEND_NAME, lookup->words,
                                   lookup->stream.buffer_reserved_bytes, 0U,
                                   *uploaded_bytes) == 0)) {
        return 0;
    }
    db_gl_upload_stream_record_sync(&lookup->stream);
    return 1;
}

void db_gl3_exact_lookup_bind(const gl3_exact_lookup_t *lookup) {
    if ((lookup == NULL) || (lookup->available == 0)) {
        return;
    }
    db_gl_use_program(lookup->program);
    db_gl_active_texture(GL_TEXTURE0 + 1U);
    db_gl_texture_buffer_bind(lookup->texture);
    db_gl_uniform1i(lookup->sampler_location, 1);
}

void db_gl3_exact_lookup_shutdown(gl3_exact_lookup_t *lookup) {
    if (lookup == NULL) {
        return;
    }
    db_gl_upload_stream_shutdown(&lookup->stream);
    db_gl_texture_delete_if_valid(&lookup->texture);
    db_gl_delete_program(lookup->program);
    free(lookup->words);
    *lookup = (gl3_exact_lookup_t){0};
}

uint64_t
db_gl3_exact_lookup_implementation_hash(const gl3_exact_lookup_t *lookup) {
    if ((lookup == NULL) || (lookup->available == 0)) {
        return 0U;
    }
    uint64_t hash =
        db_fnv1a64_tree(db_gl3_exact_lookup_vert_source,
                        strlen(db_gl3_exact_lookup_vert_source),
                        DB_GL3_LOOKUP_SHADER_DOMAIN, DB_FNV1A64_OFFSET);
    const char *const fragment = (lookup->format == DB_PIXEL_FORMAT_RGBA16F)
                                     ? db_gl3_exact_lookup_rgba16f_frag_source
                                     : db_gl3_exact_lookup_rgba8_frag_source;
    hash = db_fnv1a64_tree(fragment, strlen(fragment),
                           DB_GL3_LOOKUP_SHADER_DOMAIN, hash);
    const uint8_t format_token =
        (lookup->format == DB_PIXEL_FORMAT_RGBA16F) ? 16U : 8U;
    hash = db_fnv1a64_tree(&format_token, sizeof(format_token),
                           DB_GL3_LOOKUP_SHADER_DOMAIN, hash);
    return hash;
}
