#ifdef DB_HAS_VULKAN_API
#define GLFW_INCLUDE_VULKAN
#endif
#include <GLFW/glfw3.h>

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "../../core/db_core.h"
#include "../../core/db_hash.h"
#include "../../driverbench_cli.h"
#ifdef DB_HAS_OPENGL_API
#include "../../renderers/cpu_renderer/renderer_cpu_renderer.h"
#include "../../renderers/opengl_gl1_5_gles1_1/renderer_opengl_gl1_5_gles1_1.h"
#ifdef DB_HAS_OPENGL_DESKTOP
#include "../../renderers/opengl_gl3_3/renderer_opengl_gl3_3.h"
#endif
#include "../../renderers/renderer_benchmark_common.h"
#include "../../renderers/renderer_gl_common.h"
#endif
#include "../../renderers/renderer_identity.h"
#ifdef DB_HAS_VULKAN_API
#include "../../renderers/vulkan_1_2_multi_gpu/renderer_vulkan_1_2_multi_gpu.h"
#endif
#include "../../config/benchmark_config.h"
#include "../display_dispatch.h"
#include "../display_gl_runtime_common.h"
#include "../display_hash_common.h"
#include "display_glfw_window_common.h"
#ifdef DB_HAS_OPENGL_API
#include "../display_gl_hash_readback_common.h"
#endif

#ifdef DB_HAS_OPENGL_API
#ifdef __APPLE__
#include <OpenGL/gl.h>
#include <OpenGL/gltypes.h>
#elifdef DB_HAS_OPENGL_DESKTOP
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#else
#include <GLES/gl.h>
#endif
#endif

#define BACKEND_NAME_CPU "display_glfw_window_cpu_renderer"
#define DB_CAP_MODE_CPU_GLFW_PBO "cpu_glfw_window_pbo"
#define DB_CAP_MODE_CPU_GLFW_TEX_SUB_IMAGE "cpu_glfw_window_tex_sub_image"
#ifdef DB_HAS_OPENGL_API
#define BACKEND_NAME_GL "display_glfw_window_opengl"
#endif
#ifdef DB_HAS_VULKAN_API
#define BACKEND_NAME_VK "display_glfw_window_vulkan"
#endif

#ifdef DB_HAS_VULKAN_API
typedef struct {
    const char *backend_name;
    db_display_hash_tracker_t *hash_tracker;
    int state_hash_enabled;
} db_glfw_vulkan_loop_ctx_t;
#endif

typedef enum {
    DB_GLFW_LOOP_CONTINUE = 0,
    DB_GLFW_LOOP_STOP = 1,
    DB_GLFW_LOOP_RETRY = 2,
} db_glfw_loop_result_t;

typedef db_glfw_loop_result_t (*db_glfw_frame_fn_t)(void *user_data,
                                                    uint64_t frame_index);

typedef struct {
    const char *backend;
    db_glfw_frame_fn_t frame_fn;
    double fps_cap;
    uint32_t frame_limit;
    void *user_data;
    GLFWwindow *window;
} db_glfw_loop_t;

static uint64_t db_glfw_run_loop(const db_glfw_loop_t *loop) {
    uint64_t frames = 0U;
    while (!glfwWindowShouldClose(loop->window) && !db_should_stop()) {
        if ((loop->frame_limit > 0U) && (frames >= loop->frame_limit)) {
            break;
        }
        const double frame_start_s = db_glfw_time_seconds();
        db_glfw_poll_events();
        const db_glfw_loop_result_t frame_result =
            loop->frame_fn(loop->user_data, frames);
        if (frame_result == DB_GLFW_LOOP_STOP) {
            break;
        }
        db_glfw_sleep_to_fps_cap(loop->backend, frame_start_s, loop->fps_cap);
        if (frame_result != DB_GLFW_LOOP_RETRY) {
            frames++;
        }
    }
    return frames;
}

static db_display_hash_settings_t
db_glfw_hash_settings_for_backend(const db_cli_config_t *cfg) {
    return db_display_resolve_hash_settings(
        0, 0, (cfg != NULL) ? cfg->hash_mode : "none");
}

#ifdef DB_HAS_OPENGL_API
typedef struct {
    GLuint pbo;
    int has_pbo;
    int initialized;
    int needs_full_frame_upload;
    GLuint texture;
    uint32_t texture_height;
    int use_npot;
    uint32_t texture_width;
    GLfloat texcoords[8];
    GLfloat vertices[8];
} db_cpu_present_gl_state_t;

typedef struct {
    db_cpu_present_gl_state_t *state;
    const uint8_t *pixels;
    uint32_t pixel_width;
    int use_pbo;
} db_cpu_upload_apply_ctx_t;

typedef struct {
    double bench_start;
    const char *capability_mode;
    double next_progress_log_due_ms;
    db_display_hash_tracker_t *state_hash_tracker;
    db_display_hash_tracker_t *bo_hash_tracker;
    int state_hash_enabled;
    int output_hash_enabled;
    db_cpu_present_gl_state_t *present;
    uint32_t work_unit_count;
    GLFWwindow *window;
} db_glfw_cpu_loop_ctx_t;

typedef struct {
    const char *backend_name;
    const char *capability_mode;
    const char *renderer_name;
    db_display_hash_tracker_t *state_hash_tracker;
    db_display_hash_tracker_t *framebuffer_hash_tracker;
    db_gl_framebuffer_hash_scratch_t *hash_scratch;
    double bench_start;
    double next_progress_log_due_ms;
    db_gl_renderer_t renderer;
    int state_hash_enabled;
    int output_hash_enabled;
    uint32_t work_unit_count;
    GLFWwindow *window;
} db_glfw_opengl_loop_ctx_t;

enum {
    DB_CPU_QUAD_V0_X = 0,
    DB_CPU_QUAD_V0_Y = 1,
    DB_CPU_QUAD_V1_X = 2,
    DB_CPU_QUAD_V1_Y = 3,
    DB_CPU_QUAD_V2_X = 4,
    DB_CPU_QUAD_V2_Y = 5,
    DB_CPU_QUAD_V3_X = 6,
    DB_CPU_QUAD_V3_Y = 7,
};

static void db_present_cpu_texture_init(db_cpu_present_gl_state_t *state) {
    if (state == NULL) {
        return;
    }
    glGenTextures(1, &state->texture);
    if (state->texture == 0U) {
        db_failf(BACKEND_NAME_CPU, "failed to create CPU present texture");
    }
    glBindTexture(GL_TEXTURE_2D, state->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
#ifdef GL_CLAMP_TO_EDGE
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
#else
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
#endif
    state->needs_full_frame_upload = 1;
}

static void db_present_cpu_texture_resize(db_cpu_present_gl_state_t *state,
                                          uint32_t pixel_width,
                                          uint32_t pixel_height) {
    if ((state == NULL) || (state->texture == 0U) || (pixel_width == 0U) ||
        (pixel_height == 0U)) {
        return;
    }

    const uint32_t target_width =
        (state->use_npot != 0) ? pixel_width : db_u32_next_pow2(pixel_width);
    const uint32_t target_height =
        (state->use_npot != 0) ? pixel_height : db_u32_next_pow2(pixel_height);
    if ((target_width == state->texture_width) &&
        (target_height == state->texture_height)) {
        return;
    }

    state->texture_width = target_width;
    state->texture_height = target_height;
    glBindTexture(GL_TEXTURE_2D, state->texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 db_checked_u32_to_i32(BACKEND_NAME_CPU, "cpu_tex_width",
                                       state->texture_width),
                 db_checked_u32_to_i32(BACKEND_NAME_CPU, "cpu_tex_height",
                                       state->texture_height),
                 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    state->needs_full_frame_upload = 1;
}

static void db_present_cpu_try_init_pbo(db_cpu_present_gl_state_t *state,
                                        int enable_pbo_probe) {
    if ((state == NULL) || (enable_pbo_probe == 0)) {
        return;
    }
    if (db_gl_has_pbo_upload_support() == 0) {
        return;
    }
    state->pbo = (GLuint)db_gl_pbo_create_or_zero();
    if (state->pbo != 0U) {
        state->has_pbo = 1;
    }
}

static void db_present_cpu_init_state(db_cpu_present_gl_state_t *state,
                                      int enable_pbo_probe) {
    if ((state == NULL) || (state->initialized != 0)) {
        return;
    }
    db_present_cpu_texture_init(state);
    db_present_cpu_try_init_pbo(state, enable_pbo_probe);

    state->vertices[DB_CPU_QUAD_V0_X] = -1.0F;
    state->vertices[DB_CPU_QUAD_V0_Y] = -1.0F;
    state->vertices[DB_CPU_QUAD_V1_X] = 1.0F;
    state->vertices[DB_CPU_QUAD_V1_Y] = -1.0F;
    state->vertices[DB_CPU_QUAD_V2_X] = -1.0F;
    state->vertices[DB_CPU_QUAD_V2_Y] = 1.0F;
    state->vertices[DB_CPU_QUAD_V3_X] = 1.0F;
    state->vertices[DB_CPU_QUAD_V3_Y] = 1.0F;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, state->vertices);
    glTexCoordPointer(2, GL_FLOAT, 0, state->texcoords);
    state->initialized = 1;
}

static void db_present_cpu_upload_rows(db_cpu_present_gl_state_t *state,
                                       uint32_t pixel_width, uint32_t row_start,
                                       uint32_t row_count,
                                       const void *pixel_data,
                                       int allow_null_data) {
    if ((state == NULL) || (pixel_width == 0U) || (row_count == 0U) ||
        ((allow_null_data == 0) && (pixel_data == NULL))) {
        return;
    }
    glTexSubImage2D(
        GL_TEXTURE_2D, 0, 0,
        db_checked_u32_to_i32(BACKEND_NAME_CPU, "cpu_upload_y", row_start),
        db_checked_u32_to_i32(BACKEND_NAME_CPU, "cpu_upload_w", pixel_width),
        db_checked_u32_to_i32(BACKEND_NAME_CPU, "cpu_upload_h", row_count),
        GL_RGBA, GL_UNSIGNED_BYTE, pixel_data);
}

// NOLINTBEGIN(performance-no-int-to-ptr)
static const GLubyte *db_pbo_offset_ptr(size_t byte_offset) {
    return (const GLubyte *)(uintptr_t)byte_offset;
}
// NOLINTEND(performance-no-int-to-ptr)

static void db_apply_cpu_upload_span(const db_gl_upload_row_span_t *span,
                                     void *user_data) {
    db_cpu_upload_apply_ctx_t *ctx = (db_cpu_upload_apply_ctx_t *)user_data;
    if ((ctx == NULL) || (span == NULL)) {
        return;
    }
    if (ctx->use_pbo != 0) {
        const GLubyte *pbo_offset =
            db_pbo_offset_ptr(span->range.dst_offset_bytes);
        db_present_cpu_upload_rows(ctx->state, ctx->pixel_width,
                                   span->rows.row_start, span->rows.row_count,
                                   pbo_offset, 1);
    } else {
        db_present_cpu_upload_rows(ctx->state, ctx->pixel_width,
                                   span->rows.row_start, span->rows.row_count,
                                   ctx->pixels + span->range.src_offset_bytes,
                                   0);
    }
}

static void db_present_cpu_upload_spans(db_cpu_present_gl_state_t *state,
                                        const uint8_t *pixels,
                                        uint32_t pixel_width,
                                        uint32_t pixel_height,
                                        const db_gl_upload_range_t *ranges,
                                        size_t span_count) {
    if ((state == NULL) || (pixels == NULL) || (pixel_width == 0U) ||
        (pixel_height == 0U) || (ranges == NULL) || (span_count == 0U)) {
        return;
    }

    const size_t total_bytes = (size_t)db_checked_mul_u32(
        BACKEND_NAME_CPU, "cpu_upload_total_bytes",
        db_checked_mul_u32(BACKEND_NAME_CPU, "cpu_upload_row_bytes",
                           pixel_width, 4U),
        pixel_height);
    if (total_bytes > (size_t)PTRDIFF_MAX) {
        db_failf(BACKEND_NAME_CPU, "cpu_upload_total_bytes too large: %zu",
                 total_bytes);
    }
    const int use_pbo = (state->has_pbo != 0) && (state->pbo != 0U) &&
                        (db_gl_has_pbo_upload_support() != 0);
    if (use_pbo != 0) {
        db_gl_upload_ranges_target(pixels, total_bytes, ranges, span_count,
                                   DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER,
                                   (unsigned int)state->pbo, 0, NULL, 0, 0);
    }
    db_cpu_upload_apply_ctx_t apply_ctx = {
        .state = state,
        .pixels = pixels,
        .pixel_width = pixel_width,
        .use_pbo = use_pbo,
    };
    (void)db_gl_for_each_upload_row_span(BACKEND_NAME_CPU, pixel_width, ranges,
                                         span_count, db_apply_cpu_upload_span,
                                         &apply_ctx);
    if (use_pbo != 0) {
        db_gl_pbo_unbind_unpack();
    }
}

static void db_present_cpu_framebuffer(GLFWwindow *window,
                                       db_cpu_present_gl_state_t *state,
                                       const db_dirty_row_range_t *ranges,
                                       size_t range_count) {
    uint32_t pixel_width = 0U;
    uint32_t pixel_height = 0U;
    const uint32_t *pixels =
        db_renderer_cpu_renderer_pixels_rgba8(&pixel_width, &pixel_height);
    if ((pixels == NULL) || (pixel_width == 0U) || (pixel_height == 0U)) {
        db_failf(BACKEND_NAME_CPU, "cpu renderer returned invalid framebuffer");
    }

    int framebuffer_width_px = 0;
    int framebuffer_height_px = 0;
    glfwGetFramebufferSize(window, &framebuffer_width_px,
                           &framebuffer_height_px);
    glViewport(0, 0, framebuffer_width_px, framebuffer_height_px);

    db_present_cpu_texture_resize(state, pixel_width, pixel_height);
    if (state->texture == 0U) {
        db_failf(BACKEND_NAME_CPU, "CPU present texture is not initialized");
    }
    glBindTexture(GL_TEXTURE_2D, state->texture);
    const int force_full_upload = (state->needs_full_frame_upload != 0) ? 1 : 0;
    const size_t damage_row_count = db_u32_min((uint32_t)range_count, 2U);
    db_gl_upload_range_t upload_ranges[2] = {{0U, 0U, 0U}, {0U, 0U, 0U}};
    const db_gl_pattern_upload_collect_t upload_collect = {
        .pattern = DB_PATTERN_BANDS,
        .cols = pixel_width,
        .rows = pixel_height,
        .upload_bytes = (size_t)db_checked_mul_u32(
            BACKEND_NAME_CPU, "cpu_upload_total_bytes",
            db_checked_mul_u32(BACKEND_NAME_CPU, "cpu_upload_row_bytes",
                               pixel_width, 4U),
            pixel_height),
        .upload_tile_bytes = 4U,
        .force_full_upload = force_full_upload,
        .snake_plan = NULL,
        .snake_prev_start = 0U,
        .snake_prev_count = 0U,
        .pattern_seed = 0U,
        .snake_spans = NULL,
        .snake_scratch_capacity = 0U,
        .snake_row_bounds = NULL,
        .snake_row_bounds_capacity = 0U,
        .damage_row_ranges = ranges,
        .damage_row_count = damage_row_count,
        .use_damage_row_ranges = 1,
    };
    const size_t upload_span_count =
        db_gl_collect_pattern_upload_ranges(&upload_collect, upload_ranges, 2U);
    if ((force_full_upload != 0) && (upload_span_count == 0U)) {
        db_failf(BACKEND_NAME_CPU, "failed to compute cpu upload spans");
    }
    if (upload_span_count > 0U) {
        db_present_cpu_upload_spans(state, (const uint8_t *)pixels, pixel_width,
                                    pixel_height, upload_ranges,
                                    upload_span_count);
    }
    state->needs_full_frame_upload = 0;

    const float tex_u = (state->texture_width == 0U)
                            ? 1.0F
                            : (float)pixel_width / (float)state->texture_width;
    const float tex_v =
        (state->texture_height == 0U)
            ? 1.0F
            : (float)pixel_height / (float)state->texture_height;
    state->texcoords[DB_CPU_QUAD_V0_X] = 0.0F;
    state->texcoords[DB_CPU_QUAD_V0_Y] = tex_v;
    state->texcoords[DB_CPU_QUAD_V1_X] = tex_u;
    state->texcoords[DB_CPU_QUAD_V1_Y] = tex_v;
    state->texcoords[DB_CPU_QUAD_V2_X] = 0.0F;
    state->texcoords[DB_CPU_QUAD_V2_Y] = 0.0F;
    state->texcoords[DB_CPU_QUAD_V3_X] = tex_u;
    state->texcoords[DB_CPU_QUAD_V3_Y] = 0.0F;
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

static db_glfw_loop_result_t db_glfw_cpu_frame(void *user_data,
                                               uint64_t frame_index) {
    db_glfw_cpu_loop_ctx_t *ctx = (db_glfw_cpu_loop_ctx_t *)user_data;
    const double frame_time_s = (double)frame_index / BENCH_TARGET_FPS_D;
    db_renderer_cpu_renderer_render_frame(frame_time_s);
    size_t damage_count = 0U;
    const db_dirty_row_range_t *damage_ranges =
        db_renderer_cpu_renderer_damage_rows(&damage_count);
    db_present_cpu_framebuffer(ctx->window, ctx->present, damage_ranges,
                               damage_count);

    if (ctx->state_hash_enabled != 0) {
        const uint64_t state_hash = db_renderer_cpu_renderer_state_hash();
        db_display_hash_tracker_record(ctx->state_hash_tracker, state_hash);
    }
    if (ctx->output_hash_enabled != 0) {
        uint32_t pixel_width = 0U;
        uint32_t pixel_height = 0U;
        const uint32_t *pixels =
            db_renderer_cpu_renderer_pixels_rgba8(&pixel_width, &pixel_height);
        if (pixels == NULL) {
            db_failf(BACKEND_NAME_CPU,
                     "cpu renderer returned invalid framebuffer");
        }
        const uint64_t bo_hash = db_hash_rgba8_pixels_canonical(
            (const uint8_t *)pixels, pixel_width, pixel_height,
            (size_t)pixel_width * 4U, 0);
        db_display_hash_tracker_record(ctx->bo_hash_tracker, bo_hash);
    }

    glfwSwapBuffers(ctx->window);
    const uint64_t logged_frames = frame_index + 1U;
    const double bench_ms =
        (db_glfw_time_seconds() - ctx->bench_start) * DB_MS_PER_SECOND_D;
    db_benchmark_log_periodic(
        db_dispatch_api_name(DB_API_CPU), db_renderer_name_cpu(),
        BACKEND_NAME_CPU, logged_frames, ctx->work_unit_count, bench_ms,
        ctx->capability_mode, &ctx->next_progress_log_due_ms,
        BENCH_LOG_INTERVAL_MS_D);
    return DB_GLFW_LOOP_CONTINUE;
}

static int db_run_glfw_window_cpu(const db_cli_config_t *cfg) {
    db_validate_runtime_environment(BACKEND_NAME_CPU,
                                    DB_RUNTIME_OPT_ALLOW_REMOTE_DISPLAY);
    db_install_signal_handlers();

    const int swap_interval =
        ((cfg != NULL) && (cfg->vsync_enabled != 0)) ? 1 : 0;
    const double fps_cap = (cfg != NULL) ? cfg->fps_cap : BENCH_FPS_CAP_D;
    const uint32_t frame_limit = (cfg != NULL) ? cfg->frame_limit : 0U;
    const db_display_hash_settings_t hash_settings =
        db_glfw_hash_settings_for_backend(cfg);

    const int gl_legacy_context_major = 2;
    const int gl_legacy_context_minor = 1;
    int is_gles = 0;
    GLFWwindow *window = db_glfw_create_gl1_5_or_gles1_1_window(
        BACKEND_NAME_CPU, "CPU Renderer GLFW DriverBench",
        BENCH_WINDOW_WIDTH_PX, BENCH_WINDOW_HEIGHT_PX, gl_legacy_context_major,
        gl_legacy_context_minor, swap_interval, &is_gles,
        (cfg != NULL) ? cfg->offscreen_enabled : 0);

    const char *runtime_version = (const char *)glGetString(GL_VERSION);
    const char *runtime_renderer = (const char *)glGetString(GL_RENDERER);
    const int runtime_is_gles = db_display_log_gl_runtime_api(
        BACKEND_NAME_CPU, runtime_version, runtime_renderer);
    const char *runtime_exts = (const char *)glGetString(GL_EXTENSIONS);
    const int has_npot =
        (runtime_is_gles == 0) ||
        db_gl_version_text_at_least(runtime_version, 2, 0) ||
        db_has_gl_extension_token(runtime_exts, "GL_OES_texture_npot");
    const int has_pbo =
        db_gl_runtime_supports_pbo(runtime_version, runtime_exts);
    db_gl_preload_upload_proc_table();
    if ((is_gles != 0) && (runtime_is_gles == 0)) {
        db_infof(BACKEND_NAME_CPU, "context creation reported GLES fallback, "
                                   "but runtime API is OpenGL");
    }

    db_renderer_cpu_renderer_init();
    db_cpu_present_gl_state_t present = {
        .has_pbo = 0,
        .initialized = 0,
        .pbo = 0U,
        .texture = 0U,
        .texture_height = 0U,
        .texture_width = 0U,
        .use_npot = has_npot,
    };
    db_present_cpu_init_state(&present, has_pbo);
    const char *capability_mode =
        ((present.has_pbo != 0) && (present.pbo != 0U) &&
         (db_gl_has_pbo_upload_support() != 0))
            ? DB_CAP_MODE_CPU_GLFW_PBO
            : DB_CAP_MODE_CPU_GLFW_TEX_SUB_IMAGE;

    const uint32_t work_unit_count = db_renderer_cpu_renderer_work_unit_count();
    const double bench_start = db_glfw_time_seconds();
    db_display_hash_tracker_t state_hash_tracker =
        db_display_hash_tracker_create(
            BACKEND_NAME_CPU, hash_settings.state_hash_enabled, "state_hash",
            (cfg != NULL) ? cfg->hash_report : "both");
    db_display_hash_tracker_t bo_hash_tracker = db_display_hash_tracker_create(
        BACKEND_NAME_CPU, hash_settings.output_hash_enabled, "bo_hash",
        (cfg != NULL) ? cfg->hash_report : "both");
    db_glfw_cpu_loop_ctx_t loop_ctx = {
        .bench_start = bench_start,
        .capability_mode = capability_mode,
        .next_progress_log_due_ms = 0.0,
        .state_hash_tracker = &state_hash_tracker,
        .bo_hash_tracker = &bo_hash_tracker,
        .state_hash_enabled = hash_settings.state_hash_enabled,
        .output_hash_enabled = hash_settings.output_hash_enabled,
        .present = &present,
        .work_unit_count = work_unit_count,
        .window = window,
    };
    const db_glfw_loop_t loop = {
        .backend = BACKEND_NAME_CPU,
        .frame_fn = db_glfw_cpu_frame,
        .fps_cap = fps_cap,
        .frame_limit = frame_limit,
        .user_data = &loop_ctx,
        .window = window,
    };
    const uint64_t frames = db_glfw_run_loop(&loop);

    const double bench_ms =
        (db_glfw_time_seconds() - bench_start) * DB_MS_PER_SECOND_D;
    db_benchmark_log_final(db_dispatch_api_name(DB_API_CPU),
                           db_renderer_name_cpu(), BACKEND_NAME_CPU, frames,
                           work_unit_count, bench_ms, capability_mode);
    db_display_hash_tracker_log_final(BACKEND_NAME_CPU, &state_hash_tracker);
    db_display_hash_tracker_log_final(BACKEND_NAME_CPU, &bo_hash_tracker);

    db_renderer_cpu_renderer_shutdown();
    if (present.pbo != 0U) {
        db_gl_pbo_delete_if_valid((unsigned int)present.pbo);
    }
    if (present.texture != 0U) {
        glDeleteTextures(1, &present.texture);
    }
    db_glfw_destroy_window(window);
    return 0;
}
#else
static int db_run_glfw_window_cpu(const db_cli_config_t *cfg) {
    (void)cfg;
    db_failf(BACKEND_NAME_CPU,
             "CPU + glfw_window presentation requires OpenGL support in this "
             "build");
    return 1;
}
#endif

#ifdef DB_HAS_OPENGL_API
static const char *db_gl_backend_name(db_gl_renderer_t renderer) {
    (void)renderer;
    return BACKEND_NAME_GL;
}

static const char *db_gl_renderer_name(db_gl_renderer_t renderer) {
#ifdef DB_HAS_OPENGL_DESKTOP
    return (renderer == DB_GL_RENDERER_GL1_5_GLES1_1)
               ? db_renderer_name_opengl_gl1_5_gles1_1()
               : db_renderer_name_opengl_gl3_3();
#else
    (void)renderer;
    return db_renderer_name_opengl_gl1_5_gles1_1();
#endif
}

static void db_gl_renderer_init(db_gl_renderer_t renderer) {
    if (renderer == DB_GL_RENDERER_GL1_5_GLES1_1) {
        db_renderer_opengl_gl1_5_gles1_1_init();
        return;
    }
#ifdef DB_HAS_OPENGL_DESKTOP
    db_renderer_opengl_gl3_3_init();
#else
    db_failf(BACKEND_NAME_GL, "renderer gl3_3 is not compiled in this build");
#endif
}

static void db_gl_renderer_render_frame(db_gl_renderer_t renderer,
                                        double frame_time_s) {
    if (renderer == DB_GL_RENDERER_GL1_5_GLES1_1) {
        db_renderer_opengl_gl1_5_gles1_1_render_frame(frame_time_s);
        return;
    }
#ifdef DB_HAS_OPENGL_DESKTOP
    db_renderer_opengl_gl3_3_render_frame(frame_time_s);
#else
    (void)frame_time_s;
    db_failf(BACKEND_NAME_GL, "renderer gl3_3 is not compiled in this build");
#endif
}

static void db_gl_renderer_shutdown(db_gl_renderer_t renderer) {
    if (renderer == DB_GL_RENDERER_GL1_5_GLES1_1) {
        db_renderer_opengl_gl1_5_gles1_1_shutdown();
        return;
    }
#ifdef DB_HAS_OPENGL_DESKTOP
    db_renderer_opengl_gl3_3_shutdown();
#else
    db_failf(BACKEND_NAME_GL, "renderer gl3_3 is not compiled in this build");
#endif
}

static const char *db_gl_renderer_capability_mode(db_gl_renderer_t renderer) {
    if (renderer == DB_GL_RENDERER_GL1_5_GLES1_1) {
        return db_renderer_opengl_gl1_5_gles1_1_capability_mode();
    }
#ifdef DB_HAS_OPENGL_DESKTOP
    return db_renderer_opengl_gl3_3_capability_mode();
#else
    db_failf(BACKEND_NAME_GL, "renderer gl3_3 is not compiled in this build");
#endif
}

static uint32_t db_gl_renderer_work_unit_count(db_gl_renderer_t renderer) {
    if (renderer == DB_GL_RENDERER_GL1_5_GLES1_1) {
        return db_renderer_opengl_gl1_5_gles1_1_work_unit_count();
    }
#ifdef DB_HAS_OPENGL_DESKTOP
    return db_renderer_opengl_gl3_3_work_unit_count();
#else
    db_failf(BACKEND_NAME_GL, "renderer gl3_3 is not compiled in this build");
#endif
}

static uint64_t db_gl_renderer_state_hash(db_gl_renderer_t renderer) {
    if (renderer == DB_GL_RENDERER_GL1_5_GLES1_1) {
        return db_renderer_opengl_gl1_5_gles1_1_state_hash();
    }
#ifdef DB_HAS_OPENGL_DESKTOP
    return db_renderer_opengl_gl3_3_state_hash();
#else
    db_failf(BACKEND_NAME_GL, "renderer gl3_3 is not compiled in this build");
#endif
}

static db_glfw_loop_result_t db_glfw_opengl_frame(void *user_data,
                                                  uint64_t frame_index) {
    db_glfw_opengl_loop_ctx_t *ctx = (db_glfw_opengl_loop_ctx_t *)user_data;
    int framebuffer_width_px = 0;
    int framebuffer_height_px = 0;
    glfwGetFramebufferSize(ctx->window, &framebuffer_width_px,
                           &framebuffer_height_px);
    glViewport(0, 0, framebuffer_width_px, framebuffer_height_px);
    glClearColor(BENCH_CLEAR_COLOR_R_F, BENCH_CLEAR_COLOR_G_F,
                 BENCH_CLEAR_COLOR_B_F, BENCH_CLEAR_COLOR_A_F);
    glClear(GL_COLOR_BUFFER_BIT);

    const double frame_time_s = (double)frame_index / BENCH_TARGET_FPS_D;
    db_gl_renderer_render_frame(ctx->renderer, frame_time_s);

    if (ctx->state_hash_enabled != 0) {
        const uint64_t state_hash = db_gl_renderer_state_hash(ctx->renderer);
        db_display_hash_tracker_record(ctx->state_hash_tracker, state_hash);
    }
    if (ctx->output_hash_enabled != 0) {
        const uint8_t *framebuffer_pixels =
            db_gl_read_framebuffer_rgba8_or_fail(
                ctx->backend_name, framebuffer_width_px, framebuffer_height_px,
                ctx->hash_scratch);
        const uint64_t framebuffer_hash = db_hash_rgba8_pixels_canonical(
            framebuffer_pixels,
            db_checked_int_to_u32(ctx->backend_name, "fb_w",
                                  framebuffer_width_px),
            db_checked_int_to_u32(ctx->backend_name, "fb_h",
                                  framebuffer_height_px),
            (size_t)db_checked_int_to_u32(ctx->backend_name, "fb_row_bytes",
                                          framebuffer_width_px) *
                4U,
            1);
        db_display_hash_tracker_record(ctx->framebuffer_hash_tracker,
                                       framebuffer_hash);
    }

    glfwSwapBuffers(ctx->window);
    const uint64_t logged_frames = frame_index + 1U;
    const double bench_ms =
        (db_glfw_time_seconds() - ctx->bench_start) * DB_MS_PER_SECOND_D;
    db_benchmark_log_periodic(
        db_dispatch_api_name(DB_API_OPENGL), ctx->renderer_name,
        ctx->backend_name, logged_frames, ctx->work_unit_count, bench_ms,
        ctx->capability_mode, &ctx->next_progress_log_due_ms,
        BENCH_LOG_INTERVAL_MS_D);
    return DB_GLFW_LOOP_CONTINUE;
}

static int db_run_glfw_window_opengl(db_gl_renderer_t renderer,
                                     const db_cli_config_t *cfg) {
    const char *backend_name = db_gl_backend_name(renderer);
    db_validate_runtime_environment(backend_name,
                                    DB_RUNTIME_OPT_ALLOW_REMOTE_DISPLAY);
    db_install_signal_handlers();

    const int swap_interval =
        ((cfg != NULL) && (cfg->vsync_enabled != 0)) ? 1 : 0;
    const double fps_cap = (cfg != NULL) ? cfg->fps_cap : BENCH_FPS_CAP_D;
    const uint32_t frame_limit = (cfg != NULL) ? cfg->frame_limit : 0U;
    const db_display_hash_settings_t hash_settings =
        db_glfw_hash_settings_for_backend(cfg);

    GLFWwindow *window = NULL;
    if (renderer == DB_GL_RENDERER_GL1_5_GLES1_1) {
        const int gl_legacy_context_major = 2;
        const int gl_legacy_context_minor = 1;
        int is_gles = 0;
        window = db_glfw_create_gl1_5_or_gles1_1_window(
            backend_name, "OpenGL 1.5/GLES1.1 GLFW DriverBench",
            BENCH_WINDOW_WIDTH_PX, BENCH_WINDOW_HEIGHT_PX,
            gl_legacy_context_major, gl_legacy_context_minor, swap_interval,
            &is_gles, (cfg != NULL) ? cfg->offscreen_enabled : 0);
        const char *runtime_version = (const char *)glGetString(GL_VERSION);
        const char *runtime_renderer = (const char *)glGetString(GL_RENDERER);
        const int runtime_is_gles = db_display_log_gl_runtime_api(
            backend_name, runtime_version, runtime_renderer);
        if (runtime_is_gles != 0) {
            db_display_validate_gles_1x_runtime_or_fail(backend_name,
                                                        runtime_version);
        }
        if ((is_gles != 0) && (runtime_is_gles == 0)) {
            db_infof(backend_name, "context creation reported GLES fallback, "
                                   "but runtime API is OpenGL");
        }
    }
#ifdef DB_HAS_OPENGL_DESKTOP
    else {
        const int gl3_context_major = 3;
        const int gl3_context_minor = 3;
        window = db_glfw_create_opengl_window(
            backend_name, "OpenGL 3.3 Shader GLFW DriverBench",
            BENCH_WINDOW_WIDTH_PX, BENCH_WINDOW_HEIGHT_PX, gl3_context_major,
            gl3_context_minor, 1, swap_interval,
            (cfg != NULL) ? cfg->offscreen_enabled : 0);
        const char *runtime_version = (const char *)glGetString(GL_VERSION);
        const char *runtime_renderer = (const char *)glGetString(GL_RENDERER);
        (void)db_display_log_gl_runtime_api(backend_name, runtime_version,
                                            runtime_renderer);
    }
#else
    else {
        db_failf(backend_name, "renderer gl3_3 is not compiled in this build");
    }
#endif

    db_gl_preload_upload_proc_table();
    db_gl_renderer_init(renderer);
    const char *capability_mode = db_gl_renderer_capability_mode(renderer);
    const uint32_t work_unit_count = db_gl_renderer_work_unit_count(renderer);
    const double bench_start = db_glfw_time_seconds();
    db_display_hash_tracker_t state_hash_tracker =
        db_display_hash_tracker_create(
            backend_name, hash_settings.state_hash_enabled, "state_hash",
            (cfg != NULL) ? cfg->hash_report : "both");
    db_display_hash_tracker_t framebuffer_hash_tracker =
        db_display_hash_tracker_create(
            backend_name, hash_settings.output_hash_enabled, "framebuffer_hash",
            (cfg != NULL) ? cfg->hash_report : "both");
    db_gl_framebuffer_hash_scratch_t hash_scratch = {0};
    db_glfw_opengl_loop_ctx_t loop_ctx = {
        .backend_name = backend_name,
        .capability_mode = capability_mode,
        .renderer_name = db_gl_renderer_name(renderer),
        .state_hash_tracker = &state_hash_tracker,
        .framebuffer_hash_tracker = &framebuffer_hash_tracker,
        .hash_scratch = &hash_scratch,
        .renderer = renderer,
        .bench_start = bench_start,
        .next_progress_log_due_ms = 0.0,
        .state_hash_enabled = hash_settings.state_hash_enabled,
        .output_hash_enabled = hash_settings.output_hash_enabled,
        .work_unit_count = work_unit_count,
        .window = window,
    };
    const db_glfw_loop_t loop = {
        .backend = backend_name,
        .frame_fn = db_glfw_opengl_frame,
        .fps_cap = fps_cap,
        .frame_limit = frame_limit,
        .user_data = &loop_ctx,
        .window = window,
    };
    const uint64_t frames = db_glfw_run_loop(&loop);

    const double bench_ms =
        (db_glfw_time_seconds() - bench_start) * DB_MS_PER_SECOND_D;
    db_benchmark_log_final(db_dispatch_api_name(DB_API_OPENGL),
                           db_gl_renderer_name(renderer), backend_name, frames,
                           work_unit_count, bench_ms, capability_mode);
    db_display_hash_tracker_log_final(backend_name, &state_hash_tracker);
    db_display_hash_tracker_log_final(backend_name, &framebuffer_hash_tracker);

    db_gl_renderer_shutdown(renderer);
    db_glfw_destroy_window(window);
    db_gl_hash_scratch_release(&hash_scratch);
    return 0;
}
#endif

#ifdef DB_HAS_VULKAN_API
// NOLINTBEGIN(misc-include-cleaner)
static const char *const *
db_glfw_vk_required_instance_extensions(uint32_t *count, void *user_data) {
    (void)user_data;
    return glfwGetRequiredInstanceExtensions(count);
}

static VkResult db_glfw_vk_create_surface(VkInstance instance,
                                          void *window_handle,
                                          VkSurfaceKHR *surface,
                                          void *user_data) {
    (void)user_data;
    return glfwCreateWindowSurface(instance, (GLFWwindow *)window_handle, NULL,
                                   surface);
}

static void db_glfw_vk_get_framebuffer_size(void *window_handle, int *width,
                                            int *height, void *user_data) {
    (void)user_data;
    glfwGetFramebufferSize((GLFWwindow *)window_handle, width, height);
}

static db_glfw_loop_result_t db_glfw_vulkan_frame(void *user_data,
                                                  uint64_t frame_index) {
    const db_glfw_vulkan_loop_ctx_t *ctx =
        (const db_glfw_vulkan_loop_ctx_t *)user_data;
    const db_vk_frame_result_t frame_result =
        db_renderer_vulkan_1_2_multi_gpu_render_frame();
    if ((ctx->state_hash_enabled != 0) && (frame_result == DB_VK_FRAME_OK)) {
        const uint64_t state_hash =
            db_renderer_vulkan_1_2_multi_gpu_state_hash();
        db_display_hash_tracker_record(ctx->hash_tracker, state_hash);
    }
    if (frame_result == DB_VK_FRAME_STOP) {
        db_infof(ctx->backend_name, "renderer requested stop");
        return DB_GLFW_LOOP_STOP;
    }
    if (frame_result == DB_VK_FRAME_RETRY) {
        return DB_GLFW_LOOP_RETRY;
    }
    return DB_GLFW_LOOP_CONTINUE;
}

static int db_run_glfw_window_vulkan(const db_cli_config_t *cfg) {
    db_validate_runtime_environment(BACKEND_NAME_VK,
                                    DB_RUNTIME_OPT_ALLOW_REMOTE_DISPLAY);
    db_install_signal_handlers();
    const double fps_cap = (cfg != NULL) ? cfg->fps_cap : BENCH_FPS_CAP_D;
    const uint32_t frame_limit = (cfg != NULL) ? cfg->frame_limit : 0U;
    const db_display_hash_settings_t hash_settings =
        db_glfw_hash_settings_for_backend(cfg);
    (void)hash_settings.output_hash_enabled;

    GLFWwindow *window = db_glfw_create_no_api_window(
        BACKEND_NAME_VK, "Vulkan 1.2 opportunistic multi-GPU (device groups)",
        BENCH_WINDOW_WIDTH_PX, BENCH_WINDOW_HEIGHT_PX,
        (cfg != NULL) ? cfg->offscreen_enabled : 0);
    uint32_t runtime_api_version = VK_API_VERSION_1_0;
    const VkResult version_result =
        vkEnumerateInstanceVersion(&runtime_api_version);
    if (version_result != VK_SUCCESS) {
        runtime_api_version = VK_API_VERSION_1_0;
    }
    db_display_log_vulkan_runtime_api(BACKEND_NAME_VK, runtime_api_version,
                                      "(selected by renderer)");

    const db_vk_wsi_config_t wsi_config = {
        .window_handle = window,
        .user_data = (void *)BACKEND_NAME_VK,
        .get_required_instance_extensions =
            db_glfw_vk_required_instance_extensions,
        .create_window_surface = db_glfw_vk_create_surface,
        .get_framebuffer_size = db_glfw_vk_get_framebuffer_size,
    };
    db_renderer_vulkan_1_2_multi_gpu_init(&wsi_config);
    db_display_hash_tracker_t hash_tracker = db_display_hash_tracker_create(
        BACKEND_NAME_VK, hash_settings.state_hash_enabled, "state_hash",
        (cfg != NULL) ? cfg->hash_report : "both");
    const db_glfw_vulkan_loop_ctx_t loop_ctx = {
        .backend_name = BACKEND_NAME_VK,
        .hash_tracker = &hash_tracker,
        .state_hash_enabled = hash_settings.state_hash_enabled,
    };
    const db_glfw_loop_t loop = {
        .backend = BACKEND_NAME_VK,
        .frame_fn = db_glfw_vulkan_frame,
        .fps_cap = fps_cap,
        .frame_limit = frame_limit,
        .user_data = (void *)&loop_ctx,
        .window = window,
    };
    (void)db_glfw_run_loop(&loop);
    db_renderer_vulkan_1_2_multi_gpu_shutdown();
    db_display_hash_tracker_log_final(BACKEND_NAME_VK, &hash_tracker);
    db_glfw_destroy_window(window);
    return 0;
}
// NOLINTEND(misc-include-cleaner)
#endif

int db_run_glfw_window(db_api_t api, db_gl_renderer_t renderer,
                       const db_cli_config_t *cfg) {
    if (api == DB_API_CPU) {
        return db_run_glfw_window_cpu(cfg);
    }
#ifdef DB_HAS_VULKAN_API
    if (api == DB_API_VULKAN) {
        return db_run_glfw_window_vulkan(cfg);
    }
#endif
#ifdef DB_HAS_OPENGL_API
    return db_run_glfw_window_opengl(renderer, cfg);
#else
    (void)renderer;
    db_failf(BACKEND_NAME_CPU, "OpenGL backend is unavailable in this build");
    return 1;
#endif
}
