#include "gl3_target.h"

#include "core/db_core.h"
#include "core/db_frame_plan.h"
#include "core/db_log.h"
#include "core/db_numeric.h"
#include "core/db_render_ir.h"
#include "core/db_render_types.h"
#include "renderers/damage_trace.h"
#include "renderers/gl_api.h"
#include "renderers/gl_common.h"
#include "renderers/gl_probe_internal.h"

#include <stdint.h>

#define BACKEND_NAME "renderer_opengl_gl3_3"
#define runtime_failf(...) DB_RUNTIME_FAIL(BACKEND_NAME, __VA_ARGS__)
#define DB_GL3_SHADER_LOG_CAPACITY 1024U

static unsigned int db_gl3_compile_shader(unsigned int shader_type,
                                          const char *source) {
    unsigned int shader = db_gl_create_shader(shader_type);
    db_gl_shader_source_single(shader, source);
    db_gl_compile_shader(shader);

    int ok = 0;
    db_gl_get_shader_iv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_FALSE) {
        char log_message[DB_GL3_SHADER_LOG_CAPACITY];
        int message_length = 0;
        db_gl_get_shader_info_log(shader, sizeof(log_message), &message_length,
                                  log_message);
        runtime_failf("Shader compile failed (%u): %.*s", shader_type,
                      db_checked_int_to_i32(BACKEND_NAME, "shader_log_length",
                                            message_length),
                      log_message);
    }
    return shader;
}

unsigned int db_gl3_build_program(const char *vertex_source,
                                  const char *fragment_source) {
    unsigned int vertex =
        db_gl3_compile_shader(GL_VERTEX_SHADER, vertex_source);
    unsigned int fragment =
        db_gl3_compile_shader(GL_FRAGMENT_SHADER, fragment_source);
    unsigned int program = db_gl_create_program();
    db_gl_attach_shader(program, vertex);
    db_gl_attach_shader(program, fragment);
    db_gl_link_program(program);
    db_gl_delete_shader(vertex);
    db_gl_delete_shader(fragment);

    int ok = 0;
    db_gl_get_program_iv(program, GL_LINK_STATUS, &ok);
    if (ok == GL_FALSE) {
        char log_message[DB_GL3_SHADER_LOG_CAPACITY];
        int message_length = 0;
        db_gl_get_program_info_log(program, sizeof(log_message),
                                   &message_length, log_message);
        runtime_failf("Program link failed: %.*s",
                      db_checked_int_to_i32(BACKEND_NAME, "program_log_length",
                                            message_length),
                      log_message);
    }
    return program;
}

void db_gl3_target_destroy(gl3_persistent_target_t *target, const char *cause) {
    if ((target->texture != 0U) || (target->fbo != 0U)) {
        db_damage_trace_emit_target_lifecycle(&(
            const db_target_lifecycle_event_t){
            .backend = DB_DAMAGE_TRACE_BACKEND_GL3,
            .action = DB_TARGET_LIFECYCLE_DESTROY,
            .target = "gl3_backing",
            .target_id = 1U,
            .generation = target->generation,
            .old_width = db_checked_int_to_u32(BACKEND_NAME, "backing_width",
                                               target->width),
            .old_height = db_checked_int_to_u32(BACKEND_NAME, "backing_height",
                                                target->height),
            .format = target->format,
            .cause = (cause != NULL) ? cause : "unknown",
            .valid_before = target->valid,
            .valid_after = 0,
        });
    }
    if (target->fbo != 0U) {
        db_gl_delete_framebuffers(1, &target->fbo);
        target->fbo = 0U;
    }
    db_gl_texture_delete_if_valid(&target->texture);
    target->width = 0;
    target->height = 0;
    target->valid = 0;
}

int db_gl3_target_ensure(gl3_persistent_target_t *target, int width,
                         int height) {
    if ((width <= 0) || (height <= 0)) {
        return 0;
    }
    if ((target->width == width) && (target->height == height) &&
        (target->texture != 0U) && (target->fbo != 0U)) {
        return 0;
    }
    const uint32_t old_width = db_nonnegative_int_to_u32_or_zero(target->width);
    const uint32_t old_height =
        db_nonnegative_int_to_u32_or_zero(target->height);
    const int had_target =
        DB_BOOL((target->texture != 0U) || (target->fbo != 0U));
    db_gl3_target_destroy(target, "resize");
    const uint32_t texture_width =
        db_checked_int_to_u32(BACKEND_NAME, "backing_width", width);
    const uint32_t texture_height =
        db_checked_int_to_u32(BACKEND_NAME, "backing_height", height);
    const int texture_created =
        (target->format == DB_PIXEL_FORMAT_RGBA16F)
            ? db_gl_texture_create_rgba16f(&target->texture, texture_width,
                                           texture_height, NULL)
            : db_gl_texture_create_rgba8(&target->texture, texture_width,
                                         texture_height, NULL);
    if (texture_created == 0) {
        runtime_failf("failed to create persistent backing texture");
    }
    db_gl_gen_framebuffers(1, &target->fbo);
    db_gl_bind_framebuffer(GL_FRAMEBUFFER, target->fbo);
    db_gl_framebuffer_texture_2d(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                 GL_TEXTURE_2D, target->texture, 0);
    if (db_gl_check_framebuffer_status(GL_FRAMEBUFFER) !=
        GL_FRAMEBUFFER_COMPLETE) {
        runtime_failf("persistent backing framebuffer incomplete");
    }
    target->width = width;
    target->height = height;
    target->generation = db_checked_add_u32(
        BACKEND_NAME, "persistent_target_generation", target->generation, 1U);
    db_damage_trace_emit_target_lifecycle(&(const db_target_lifecycle_event_t){
        .backend = DB_DAMAGE_TRACE_BACKEND_GL3,
        .action = (had_target != 0) ? DB_TARGET_LIFECYCLE_RECREATE
                                    : DB_TARGET_LIFECYCLE_CREATE,
        .target = "gl3_backing",
        .target_id = 1U,
        .generation = target->generation,
        .old_width = old_width,
        .old_height = old_height,
        .new_width = texture_width,
        .new_height = texture_height,
        .format = target->format,
        .cause = (had_target != 0) ? "resize" : "initial_target",
        .valid_before = 0,
        .valid_after = 0,
    });
    return 1;
}

void db_gl3_target_restore(gl3_persistent_target_t *target,
                           const db_frame_plan_t *plan) {
    db_render_ir_upload_command_t upload = {0};
    db_render_ir_external_binding_t source = {0};
    if ((db_render_ir_resolve_full_upload(&plan->rebuild_ir,
                                          plan->external_bindings, &upload,
                                          &source) == 0) ||
        (source.pixels == NULL) || (source.width != (uint32_t)target->width) ||
        (source.height != (uint32_t)target->height) ||
        (source.format != target->format)) {
        runtime_failf("invalid canonical raster rebuild seed");
    }
    uint32_t row_length_pixels = 0U;
    if (db_gl_external_binding_unpack_row_length(&source, 1,
                                                 &row_length_pixels) == 0) {
        runtime_failf("invalid canonical raster rebuild row stride");
    }
    db_gl_active_texture(GL_TEXTURE0);
    db_gl_texture_bind_2d(target->texture);
    db_gl_set_unpack_row_length_pixels(row_length_pixels);
    if (source.format == DB_PIXEL_FORMAT_RGBA16F) {
        db_gl_texture_sub_image_2d_rgba16f(0U, 0U, source.width, source.height,
                                           (const uint16_t *)source.pixels);
    } else {
        db_gl_texture_sub_image_2d_rgba(0U, 0U, source.width, source.height,
                                        (const uint8_t *)source.pixels);
    }
    db_gl_set_unpack_row_length_pixels(0U);
    db_gl_texture_bind_2d(0U);
}
