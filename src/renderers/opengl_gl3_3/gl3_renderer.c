#include "gl3_renderer.h"
#include "core/db_log.h"
#include "core/db_renderer_runtime_contract.h"
#include "core/db_renderer_support.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "../../core/db_core.h"
#include "../../core/db_frame_plan.h"
#include "../../core/db_geometry.h"
#include "../../core/db_numeric.h"
#include "../damage_trace.h"
#include "../gl_api.h"
#include "../gl_common.h"
#include "../gl_hash_readback.h"
#include "../renderer_viewport_common.h"
#include "core/db_raster_geometry.h"
#include "core/db_render_result.h"
#include "core/db_render_types.h"
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Woverlength-strings"
#endif
#include "db_embedded_shaders.h"
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#define BACKEND_NAME "renderer_opengl_gl3_3"
#define ATTR_COLOR_LOC 1U
#define ATTR_POSITION_LOC 0U
#define SHADER_LOG_MSG_CAPACITY 1024
#define runtime_failf(...) DB_RUNTIME_FAIL(BACKEND_NAME, __VA_ARGS__)

typedef struct {
    int width;
    int height;
    unsigned int fbo;
    unsigned int texture;
    int valid;
    uint32_t generation;
    db_pixel_format_t format;
} gl3_persistent_target_t;

typedef struct {
    db_gl_vertex_init_t vertex;
    unsigned int vao;
    db_gl_buffer_cache_t buffers;
    db_gl_upload_stream_t stream;
} gl3_geometry_stream_t;

typedef struct {
    unsigned int draw_program;
    unsigned int present_program;
    int sampler_location;
    int hdr_output_location;
    int hdr_output_enabled;
    db_gl_viewport_cache_t viewport;
} gl3_presentation_pipeline_t;

typedef struct {
    db_gl_framebuffer_hash_scratch_t scratch;
} gl3_hash_workspace_t;

typedef struct {
    char capability_mode[DB_GL_CAPABILITY_MODE_MAX];
    db_renderer_frame_stats_t frame;
} gl3_telemetry_t;

typedef struct {
    db_renderer_execution_config_t runtime;
    gl3_persistent_target_t target;
    gl3_geometry_stream_t geometry;
    gl3_presentation_pipeline_t presentation;
    gl3_hash_workspace_t hash;
    gl3_telemetry_t telemetry;
} renderer_state_t;

static renderer_state_t g_state = {0};

uint64_t db_gl3_working_hash(void) {
    if ((g_state.target.fbo == 0U) || (g_state.target.width <= 0) ||
        (g_state.target.height <= 0)) {
        return 0U;
    }
    db_gl_bind_framebuffer(GL_FRAMEBUFFER, g_state.target.fbo);
    const uint64_t hash = db_gl_hash_framebuffer_rgba16f_or_fail(
        BACKEND_NAME,
        db_checked_int_to_u32(BACKEND_NAME, "backing_width",
                              g_state.target.width),
        db_checked_int_to_u32(BACKEND_NAME, "backing_height",
                              g_state.target.height),
        &g_state.hash.scratch, 1);
    db_gl_bind_framebuffer(GL_FRAMEBUFFER, 0U);
    return hash;
}

static void gl3_trace_full_frame(uint32_t frame_index,
                                 db_damage_trace_stage_t stage,
                                 db_damage_trace_operation_t operation,
                                 db_damage_trace_buffer_t source,
                                 db_damage_trace_buffer_t destination,
                                 uint32_t destination_index) {
    if (db_damage_trace_enabled() == 0) {
        return;
    }
    const uint32_t width =
        db_checked_int_to_u32(BACKEND_NAME, "trace_width",
                              g_state.presentation.viewport.last_viewport_w);
    const uint32_t height =
        db_checked_int_to_u32(BACKEND_NAME, "trace_height",
                              g_state.presentation.viewport.last_viewport_h);
    const db_damage_block_t full_block = db_damage_block_full(height, width);
    (void)db_damage_trace_emit(&(const db_damage_trace_event_t){
        .frame_index = frame_index,
        .backend = DB_DAMAGE_TRACE_BACKEND_GL3,
        .stage = stage,
        .operation = operation,
        .source = source,
        .destination = destination,
        .destination_index = destination_index,
        .space = DB_DAMAGE_TRACE_SPACE_PIXEL,
        .width = width,
        .height = height,
        .pixel_format = g_state.target.format,
        .blocks = &full_block,
        .block_count = 1U,
        .result = DB_DAMAGE_TRACE_RESULT_EXECUTED,
        .target = "gl3_backing",
        .target_generation = g_state.target.generation,
        .present_method = (stage == DB_DAMAGE_TRACE_STAGE_PRESENT)
                              ? "sample_fullscreen"
                              : "none",
    });
}

static void gl3_bind_main_vbo_layout(void) {
    (void)db_gl_upload_stream_bind(&g_state.geometry.stream);
    const int32_t stride_bytes =
        db_checked_size_to_i32(BACKEND_NAME, "vertex_stride_bytes",
                               DB_VERTEX_FLOAT_STRIDE * sizeof(float));
    db_gl_vertex_attrib_pointer_2f(ATTR_POSITION_LOC, stride_bytes, 0U);
    db_gl_vertex_attrib_pointer_3f(ATTR_COLOR_LOC, stride_bytes,
                                   DB_VERTEX_POSITION_FLOAT_COUNT *
                                       sizeof(float));
}

static uint32_t
db_gl3_upload_canonical_geometry(db_colored_f64_block_view_t blocks) {
    if ((blocks.blocks == NULL) || (blocks.count == 0U)) {
        return 0U;
    }
    if (db_gl_upload_stream_wait(&g_state.geometry.stream) == 0) {
        DB_RUNTIME_FAIL(BACKEND_NAME,
                        "canonical GL3 geometry stream reuse timed out");
    }
    const size_t stride = DB_VERTEX_FLOAT_STRIDE;
    const size_t floats_per_rect = db_checked_mul_size(
        BACKEND_NAME, "floats_per_rect", (size_t)DB_RECT_VERTEX_COUNT, stride);
    const size_t capacity = (size_t)g_state.geometry.vertex.draw_vertex_count /
                            DB_RECT_VERTEX_COUNT;
    if (blocks.count > capacity) {
        DB_RUNTIME_FAIL(BACKEND_NAME,
                        "canonical GL3 geometry capacity exceeded");
    }
    for (size_t index = 0U; index < blocks.count; index++) {
        const db_colored_f64_block_t *const block = &blocks.blocks[index];
        float rgb[3] = {0};
        db_rgb_f64_quantize_f16_to_f32_rgb3(block->rgb, rgb);
        const db_grid_block_t grid_block = {
            .row_start = block->row_start,
            .row_count = block->row_count,
            .col_start = block->col_start,
            .col_count = block->col_count,
        };
        float x0 = 0.0F;
        float y0 = 0.0F;
        float x1 = 0.0F;
        float y1 = 0.0F;
        db_grid_block_bounds_ndc_for_extent(g_state.runtime.grid_cols,
                                            g_state.runtime.grid_rows,
                                            &grid_block, &x0, &y0, &x1, &y1);
        float *const vertices =
            &g_state.geometry.vertex.vertices[index * floats_per_rect];
        db_fill_rect_unit_pos(vertices, x0, y0, x1, y1, stride);
        db_set_rect_unit_rgb(vertices, stride, DB_VERTEX_POSITION_FLOAT_COUNT,
                             rgb);
    }
    const size_t vertex_count =
        db_checked_mul_size(BACKEND_NAME, "canonical_vertex_count",
                            blocks.count, DB_RECT_VERTEX_COUNT);
    const size_t float_count = db_checked_mul_size(
        BACKEND_NAME, "canonical_vertex_float_count", vertex_count, stride);
    const size_t byte_count = db_checked_mul_size(
        BACKEND_NAME, "canonical_vertex_bytes", float_count, sizeof(float));
    if (db_gl_upload_stream_write(&g_state.geometry.stream, BACKEND_NAME,
                                  g_state.geometry.vertex.vertices,
                                  g_state.geometry.buffers.vbo_bytes, 0U,
                                  byte_count) == 0) {
        DB_RUNTIME_FAIL(BACKEND_NAME, "canonical GL3 geometry upload failed");
    }
    gl3_bind_main_vbo_layout();
    return db_checked_size_to_u32(BACKEND_NAME, "canonical_vertex_count",
                                  vertex_count);
}

static uint32_t
db_gl3_draw_canonical_geometry(db_colored_f64_block_view_t blocks) {
    const uint32_t vertex_count = db_gl3_upload_canonical_geometry(blocks);
    if (vertex_count == 0U) {
        return 0U;
    }
    // Presentation damage may leave scissoring enabled. Canonical geometry
    // already carries exact logical bounds, so the backing draw owns a full
    // target raster state rather than inheriting presentation clipping.
    db_gl_set_scissor_enabled(0);
    db_gl_draw_arrays_triangles(0U, vertex_count);
    db_gl_upload_stream_record_sync(&g_state.geometry.stream);
    return vertex_count;
}

static void gl3_refresh_capability_mode(void) {
    const db_gl_runtime_draw_mode_t draw_mode =
        ((g_state.runtime.backbuffer_draw_full != 0) ||
         (g_state.runtime.pipeline.uses_history_pipeline == 0))
            ? DB_GL_RUNTIME_DRAW_FULL_PRESENT
            : DB_GL_RUNTIME_DRAW_DIRTY_REPLAY;
    const db_gl_runtime_mode_desc_t mode = db_gl_runtime_mode_desc_renderer(
        draw_mode,
        db_gl_stream_upload_uses_buffer_object(
            &g_state.geometry.stream.capability) &&
            (g_state.geometry.stream.buffer != 0U),
        &g_state.geometry.stream.capability,
        g_state.runtime.backbuffer_replay_enabled);
    db_gl_runtime_mode_format_renderer(
        g_state.telemetry.capability_mode,
        sizeof(g_state.telemetry.capability_mode), &mode);
}

static void destroy_backing_target(const char *cause) {
    if ((g_state.target.texture != 0U) || (g_state.target.fbo != 0U)) {
        db_damage_trace_emit_target_lifecycle(&(
            const db_target_lifecycle_event_t){
            .backend = DB_DAMAGE_TRACE_BACKEND_GL3,
            .action = DB_TARGET_LIFECYCLE_DESTROY,
            .target = "gl3_backing",
            .target_id = 1U,
            .generation = g_state.target.generation,
            .old_width = db_checked_int_to_u32(BACKEND_NAME, "backing_width",
                                               g_state.target.width),
            .old_height = db_checked_int_to_u32(BACKEND_NAME, "backing_height",
                                                g_state.target.height),
            .format = g_state.target.format,
            .cause = (cause != NULL) ? cause : "unknown",
            .valid_before = g_state.target.valid,
            .valid_after = 0,
        });
    }
    if (g_state.target.fbo != 0U) {
        db_gl_delete_framebuffers(1, &g_state.target.fbo);
        g_state.target.fbo = 0U;
    }
    db_gl_texture_delete_if_valid(&g_state.target.texture);
    g_state.target.width = 0;
    g_state.target.height = 0;
    g_state.target.valid = 0;
}

static int ensure_backing_target(int width, int height) {
    if ((width <= 0) || (height <= 0)) {
        return 0;
    }
    if ((g_state.target.width == width) && (g_state.target.height == height) &&
        (g_state.target.texture != 0U) && (g_state.target.fbo != 0U)) {
        return 0;
    }
    const uint32_t old_width =
        (g_state.target.width > 0) ? (uint32_t)g_state.target.width : 0U;
    const uint32_t old_height =
        (g_state.target.height > 0) ? (uint32_t)g_state.target.height : 0U;
    const int had_target =
        DB_BOOL((g_state.target.texture != 0U) || (g_state.target.fbo != 0U));
    destroy_backing_target("resize");
    const uint32_t texture_width =
        db_checked_int_to_u32(BACKEND_NAME, "backing_width", width);
    const uint32_t texture_height =
        db_checked_int_to_u32(BACKEND_NAME, "backing_height", height);
    const int texture_created =
        (g_state.target.format == DB_PIXEL_FORMAT_RGBA16F)
            ? db_gl_texture_create_rgba16f(&g_state.target.texture,
                                           texture_width, texture_height, NULL)
            : db_gl_texture_create_rgba8(&g_state.target.texture, texture_width,
                                         texture_height, NULL);
    if (texture_created == 0) {
        runtime_failf("failed to create persistent backing texture");
    }
    db_gl_gen_framebuffers(1, &g_state.target.fbo);
    db_gl_bind_framebuffer(GL_FRAMEBUFFER, g_state.target.fbo);
    db_gl_framebuffer_texture_2d(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                 GL_TEXTURE_2D, g_state.target.texture, 0);
    if (db_gl_check_framebuffer_status(GL_FRAMEBUFFER) !=
        GL_FRAMEBUFFER_COMPLETE) {
        runtime_failf("persistent backing framebuffer incomplete");
    }
    g_state.target.width = width;
    g_state.target.height = height;
    g_state.target.generation++;
    db_damage_trace_emit_target_lifecycle(&(const db_target_lifecycle_event_t){
        .backend = DB_DAMAGE_TRACE_BACKEND_GL3,
        .action = (had_target != 0) ? DB_TARGET_LIFECYCLE_RECREATE
                                    : DB_TARGET_LIFECYCLE_CREATE,
        .target = "gl3_backing",
        .target_id = 1U,
        .generation = g_state.target.generation,
        .old_width = old_width,
        .old_height = old_height,
        .new_width = texture_width,
        .new_height = texture_height,
        .format = g_state.target.format,
        .cause = (had_target != 0) ? "resize" : "initial_target",
        .valid_before = 0,
        .valid_after = 0,
    });
    return 1;
}

static void gl3_restore_raster_seed(const db_frame_rebuild_seed_t *seed) {
    if ((seed == NULL) || (seed->kind != DB_FRAME_REBUILD_SEED_RASTER) ||
        (seed->raster.pixels == NULL) ||
        (seed->raster.pixel_width != (uint32_t)g_state.target.width) ||
        (seed->raster.pixel_height != (uint32_t)g_state.target.height) ||
        (seed->raster.format != g_state.target.format)) {
        runtime_failf("invalid canonical raster rebuild seed");
    }
    db_gl_active_texture(GL_TEXTURE0);
    db_gl_texture_bind_2d(g_state.target.texture);
    if (seed->raster.format == DB_PIXEL_FORMAT_RGBA16F) {
        db_gl_texture_sub_image_2d_rgba16f(
            0U, 0U, seed->raster.pixel_width, seed->raster.pixel_height,
            (const uint16_t *)seed->raster.pixels);
    } else {
        db_gl_texture_sub_image_2d_rgba(0U, 0U, seed->raster.pixel_width,
                                        seed->raster.pixel_height,
                                        (const uint8_t *)seed->raster.pixels);
    }
    db_gl_texture_bind_2d(0U);
}

static unsigned int compile_shader(unsigned int shader_type,
                                   const char *source) {
    unsigned int shader = db_gl_create_shader(shader_type);
    db_gl_shader_source_single(shader, source);
    db_gl_compile_shader(shader);

    int ok = 0;
    db_gl_get_shader_iv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_FALSE) {
        char log_msg[SHADER_LOG_MSG_CAPACITY];
        int msg_len = 0;
        db_gl_get_shader_info_log(shader, sizeof(log_msg), &msg_len, log_msg);
        const int msg_len_i32 =
            db_checked_int_to_i32(BACKEND_NAME, "shader_log_msg_len", msg_len);
        runtime_failf("Shader compile failed (%u): %.*s", (unsigned)shader_type,
                      msg_len_i32, log_msg);
    }
    return shader;
}

static unsigned int build_program(const char *vertex_source,
                                  const char *fragment_source) {
    unsigned int vert = compile_shader(GL_VERTEX_SHADER, vertex_source);
    unsigned int frag = compile_shader(GL_FRAGMENT_SHADER, fragment_source);

    unsigned int program = db_gl_create_program();
    db_gl_attach_shader(program, vert);
    db_gl_attach_shader(program, frag);
    db_gl_link_program(program);
    db_gl_delete_shader(vert);
    db_gl_delete_shader(frag);

    int ok = 0;
    db_gl_get_program_iv(program, GL_LINK_STATUS, &ok);
    if (ok == GL_FALSE) {
        char log_msg[SHADER_LOG_MSG_CAPACITY];
        int msg_len = 0;
        db_gl_get_program_info_log(program, sizeof(log_msg), &msg_len, log_msg);
        const int msg_len_i32 =
            db_checked_int_to_i32(BACKEND_NAME, "program_log_msg_len", msg_len);
        runtime_failf("Program link failed: %.*s", msg_len_i32, log_msg);
    }
    return program;
}

static int db_init_vertices_for_mode(
    const db_renderer_runtime_contract_t *resolved_runtime) {
    if (resolved_runtime == NULL) {
        return 0;
    }
    const db_renderer_execution_config_t runtime_state =
        resolved_runtime->execution;
    db_gl_vertex_init_t init_state = {0};
    if (!db_init_vertices_for_execution_config(BACKEND_NAME, &init_state,
                                               &runtime_state,
                                               DB_VERTEX_FLOAT_STRIDE)) {
        return 0;
    }

    g_state.geometry.vertex = init_state;
    g_state.runtime = runtime_state;
    return 1;
}

void db_gl3_init(const db_renderer_runtime_contract_t *resolved_runtime) {
    g_state = (renderer_state_t){0};
    if (resolved_runtime == NULL) {
        runtime_failf("missing resolved runtime");
    }
    g_state.target.format = resolved_runtime->format.surface_pixel_format;
    if (db_init_vertices_for_mode(resolved_runtime) == 0) {
        runtime_failf("failed to allocate benchmark vertex buffers");
    }

    db_gl_gen_vertex_arrays(1, &g_state.geometry.vao);
    const size_t vertex_float_count =
        db_checked_mul_size(BACKEND_NAME, "vertex_float_count",
                            (size_t)g_state.geometry.vertex.draw_vertex_count,
                            DB_VERTEX_FLOAT_STRIDE);
    g_state.geometry.buffers.vbo_bytes = db_checked_mul_size(
        BACKEND_NAME, "vertex_buffer_bytes", vertex_float_count, sizeof(float));
    db_gl_geometry_stream_init_result_t stream_init = {0};
    if (db_gl_geometry_stream_init(
            &g_state.geometry.stream, &stream_init, BACKEND_NAME,
            g_state.geometry.buffers.vbo_bytes,
            g_state.geometry.vertex.vertices, g_state.geometry.vertex.vertices,
            g_state.geometry.buffers.vbo_bytes, 1, 0) == 0) {
        runtime_failf("failed to initialize GL3 vertex stream");
    }
    if (g_state.geometry.stream.buffer == 0U) {
        runtime_failf("failed to create GL array buffer");
    }
    db_gl_bind_vertex_array(g_state.geometry.vao);
    if (db_gl_upload_stream_bind(&g_state.geometry.stream) == 0) {
        runtime_failf("failed to bind GL array buffer");
    }

    db_gl_enable_vertex_attrib_array(ATTR_POSITION_LOC);
    db_gl_enable_vertex_attrib_array(ATTR_COLOR_LOC);
    gl3_bind_main_vbo_layout();

    gl3_refresh_capability_mode();
    db_log_renderer_capability(
        BACKEND_NAME,
        ((g_state.runtime.backbuffer_draw_full != 0) ||
         (g_state.runtime.pipeline.uses_history_pipeline == 0))
            ? "full_present"
            : "dirty_replay",
        db_gl_stream_upload_uses_buffer_object(
            &g_state.geometry.stream.capability)
            ? "buffer_object"
            : "client_arrays",
        g_state.runtime.backbuffer_replay_enabled, "geometry_stream");
    db_gl_upload_stream_log_selection(&g_state.geometry.stream, BACKEND_NAME,
                                      "rectangle_geometry");
    g_state.presentation.draw_program =
        build_program(db_gl3_rect_vert_source, db_gl3_rect_frag_source);
    g_state.presentation.present_program =
        build_program(db_gl3_present_vert_source, db_gl3_present_frag_source);
    g_state.presentation.sampler_location = db_gl_get_uniform_location(
        g_state.presentation.present_program, "backing_texture");
    g_state.presentation.hdr_output_location = db_gl_get_uniform_location(
        g_state.presentation.present_program, "hdr_output_enabled");
    g_state.presentation.hdr_output_enabled =
        resolved_runtime->format.native_hdr_enabled;
    db_gl_use_program(g_state.presentation.draw_program);
}
void db_gl3_render_frame(const db_frame_plan_t *plan, int viewport_width_px,
                         int viewport_height_px) {
    if (plan == NULL) {
        return;
    }
    (void)db_renderer_resolve_viewport_state(
        BACKEND_NAME, plan->grid_cols, plan->grid_rows, &viewport_width_px,
        &viewport_height_px, &g_state.presentation.viewport.last_viewport_w,
        &g_state.presentation.viewport.last_viewport_h);
    if (plan->geometry_overflowed != 0) {
        DB_RUNTIME_FAIL(BACKEND_NAME, "canonical geometry emitter overflow");
    }

    int presentation_fbo = 0;
    db_gl_get_integerv(GL_DRAW_FRAMEBUFFER_BINDING, &presentation_fbo);
    const uint32_t presentation_fbo_u32 = db_checked_int_to_u32(
        BACKEND_NAME, "presentation_fbo", presentation_fbo);

    const int backing_width = db_checked_u32_to_i32(
        BACKEND_NAME, "logical_raster_width", plan->pixel_width);
    const int backing_height = db_checked_u32_to_i32(
        BACKEND_NAME, "logical_raster_height", plan->pixel_height);
    const int recreated = ensure_backing_target(backing_width, backing_height);
    db_damage_trace_emit_frame_plan(DB_DAMAGE_TRACE_BACKEND_GL3, "gl3_backing",
                                    g_state.target.generation, plan);
    const int rebuild =
        DB_BOOL((recreated != 0) || (g_state.target.valid == 0) ||
                (plan->rebuild_required != 0));
    const int use_raster_seed =
        DB_BOOL((rebuild != 0) &&
                (plan->rebuild_seed.kind == DB_FRAME_REBUILD_SEED_RASTER));
    if (use_raster_seed != 0) {
        gl3_restore_raster_seed(&plan->rebuild_seed);
    }
    const db_colored_f64_block_view_t draw_view =
        ((rebuild != 0) &&
         (plan->rebuild_seed.kind == DB_FRAME_REBUILD_SEED_GEOMETRY))
            ? plan->rebuild_seed.geometry
            : plan->geometry.current_blocks;
    if ((rebuild != 0) && (use_raster_seed == 0) && (draw_view.count == 0U)) {
        runtime_failf(
            "persistent backing rebuild has no authoritative geometry");
    }
    db_gl_bind_framebuffer(GL_FRAMEBUFFER, g_state.target.fbo);
    db_gl_set_viewport_px(backing_width, backing_height);
    db_gl_use_program(g_state.presentation.draw_program);
    const uint32_t draw_vertices = db_gl3_draw_canonical_geometry(draw_view);
    (void)db_damage_trace_emit_colored_grid(
        &(const db_damage_trace_event_t){
            .frame_index = plan->frame_index,
            .backend = DB_DAMAGE_TRACE_BACKEND_GL3,
            .stage = DB_DAMAGE_TRACE_STAGE_RENDERER_WRITE,
            .operation = (rebuild != 0) ? DB_DAMAGE_TRACE_OP_REBUILD
                                        : DB_DAMAGE_TRACE_OP_INCREMENTAL,
            .source = DB_DAMAGE_TRACE_BUFFER_LOGICAL_PLAN,
            .destination = DB_DAMAGE_TRACE_BUFFER_GL_FBO,
            .space = DB_DAMAGE_TRACE_SPACE_GRID,
            .width = plan->grid_cols,
            .height = plan->grid_rows,
            .pixel_format = g_state.target.format,
            .transfer_size_bytes = db_checked_mul_size(
                BACKEND_NAME, "trace_vertex_bytes",
                db_checked_mul_size(BACKEND_NAME, "trace_vertex_float_count",
                                    (size_t)draw_vertices,
                                    DB_VERTEX_FLOAT_STRIDE),
                sizeof(float)),
            .result = DB_DAMAGE_TRACE_RESULT_EXECUTED,
            .target = "gl3_backing",
            .target_generation = g_state.target.generation,
        },
        draw_view.blocks, draw_view.count);
    g_state.target.valid = 1;
    if (rebuild != 0) {
        db_damage_trace_emit_target_lifecycle(
            &(const db_target_lifecycle_event_t){
                .backend = DB_DAMAGE_TRACE_BACKEND_GL3,
                .action = DB_TARGET_LIFECYCLE_REBUILD,
                .target = "gl3_backing",
                .target_id = 1U,
                .generation = g_state.target.generation,
                .new_width = (uint32_t)g_state.target.width,
                .new_height = (uint32_t)g_state.target.height,
                .format = g_state.target.format,
                .cause = (recreated != 0) ? "target_recreated" : "frame_plan",
                .valid_before = DB_BOOL(recreated == 0),
                .valid_after = 1,
            });
    }
    gl3_trace_full_frame(plan->frame_index, DB_DAMAGE_TRACE_STAGE_RENDER_TARGET,
                         DB_DAMAGE_TRACE_OP_DRAW, DB_DAMAGE_TRACE_BUFFER_GL_FBO,
                         DB_DAMAGE_TRACE_BUFFER_GL_FBO, 0U);

    db_gl_bind_framebuffer(GL_FRAMEBUFFER, presentation_fbo_u32);
    db_gl_set_viewport_px(g_state.presentation.viewport.last_viewport_w,
                          g_state.presentation.viewport.last_viewport_h);
    db_gl_use_program(g_state.presentation.present_program);
    db_gl_active_texture(GL_TEXTURE0);
    db_gl_texture_bind_2d(g_state.target.texture);
    db_gl_uniform1i(g_state.presentation.sampler_location, 0);
    db_gl_uniform1i(g_state.presentation.hdr_output_location,
                    g_state.presentation.hdr_output_enabled);
    db_gl_draw_arrays_triangles(0U, 3U);
    db_gl_texture_bind_2d(0U);

    const uint32_t work_count = draw_vertices / DB_RECT_VERTEX_COUNT;
    const int full_draw = rebuild;
    db_renderer_record_draw_stats_for_work(
        &g_state.telemetry.frame.full_draw_frames,
        &g_state.telemetry.frame.dirty_draw_frames, full_draw,
        DB_BOOL(full_draw == 0), work_count);
    gl3_trace_full_frame(plan->frame_index, DB_DAMAGE_TRACE_STAGE_PRESENT,
                         DB_DAMAGE_TRACE_OP_PRESENT,
                         DB_DAMAGE_TRACE_BUFFER_GL_FBO,
                         DB_DAMAGE_TRACE_BUFFER_GL_DEFAULT_FRAMEBUFFER, 0U);
    g_state.telemetry.frame.state_hash = plan->expected_state_hash;
    g_state.telemetry.frame.frame_index++;
}

void db_gl3_shutdown(void) {
    db_gl_upload_stream_shutdown(&g_state.geometry.stream);
    db_gl_hash_scratch_release(&g_state.hash.scratch);
    destroy_backing_target("shutdown");
    db_gl_delete_program(g_state.presentation.present_program);
    db_gl_delete_program(g_state.presentation.draw_program);
    db_gl_delete_vertex_arrays(1, &g_state.geometry.vao);
    free(g_state.geometry.vertex.vertices);
    g_state = (renderer_state_t){0};
}

const char *db_gl3_capability_mode(void) {
    if (g_state.telemetry.capability_mode[0] == '\0') {
        gl3_refresh_capability_mode();
    }
    return g_state.telemetry.capability_mode;
}

uint32_t db_gl3_work_unit_count(void) {
    return g_state.runtime.work_unit_count;
}

uint64_t db_gl3_state_hash(void) { return g_state.telemetry.frame.state_hash; }

void db_gl3_draw_stats(db_renderer_draw_path_stats_t *stats) {
    db_renderer_copy_draw_path_stats(&g_state.telemetry.frame, stats);
}
