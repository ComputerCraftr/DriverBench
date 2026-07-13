#ifndef DRIVERBENCH_DISPLAY_RUNTIME_CONFIG_COMMON_H
#define DRIVERBENCH_DISPLAY_RUNTIME_CONFIG_COMMON_H

#include <stdint.h>
#include <string.h>

#include "../config/benchmark_config.h"
#include "../config/runtime_options.h"
#include "../core/db_core.h"
#include "../core/db_log.h"
#include "../core/db_renderer_runtime_contract.h"
#include "../core/db_renderer_support.h"
#include "../core/db_trace.h"
#include "../driverbench_config.h"
#include "benchmarks/db_benchmark_runtime_internal.h"
#include "display_hash_common.h"

enum {
    DB_DISPLAY_SDR_BIT_DEPTH = DB_SDR_NATIVE_BIT_DEPTH,
    DB_DISPLAY_HDR10_BIT_DEPTH = DB_HDR10_NATIVE_BIT_DEPTH,
};

typedef enum {
    DB_PRESENT_SCALE_NEAREST = 0,
} db_present_scale_filter_t;

typedef struct {
    uint32_t source_width;
    uint32_t source_height;
    uint32_t destination_width;
    uint32_t destination_height;
    uint32_t viewport_x;
    uint32_t viewport_y;
    uint32_t viewport_width;
    uint32_t viewport_height;
    db_present_scale_filter_t filter;
} db_presentation_transform_t;

typedef struct {
    int debug_clear_default_framebuffer;
    double fps_cap;
    uint32_t frame_limit;
    const char *hash_mode;
    const char *hash_report;
} db_display_runtime_config_t;

typedef struct {
    db_display_runtime_config_t runtime;
    db_display_hash_settings_t hash_settings;
} db_display_runtime_hash_config_t;

typedef struct {
    db_display_hash_tracker_t state;
    db_display_hash_tracker_t output;
} db_display_dual_hash_trackers_t;

typedef struct {
    db_cli_config_t effective_cfg;
    db_display_runtime_config_t display;
    db_display_hash_settings_t hash_settings;
    db_benchmark_runtime_init_t benchmark;
    db_renderer_runtime_contract_t renderer;
    db_presentation_transform_t presentation;
} db_display_renderer_runtime_t;

typedef struct {
    const char *api_name;
    const char *backend;
    const char *renderer_name;
    db_display_hash_tracker_t *output_hash_tracker;
    db_display_hash_tracker_t *state_hash_tracker;
    double *next_progress_log_due_ms;
    uint32_t frame_limit;
    uint32_t work_unit_count;
    int output_hash_enabled;
    int state_hash_enabled;
} db_display_frame_step_t;

static inline int db_display_runtime_option_parse_bool(const char *name,
                                                       int default_value,
                                                       int *out_was_explicit) {
    const char *value = db_runtime_option_get(name);
    if ((value == NULL) || (value[0] == '\0')) {
        if (out_was_explicit != NULL) {
            *out_was_explicit = 0;
        }
        return default_value;
    }
    int parsed = 0;
    if (db_parse_bool_text(value, &parsed) != 0) {
        if (out_was_explicit != NULL) {
            *out_was_explicit = 1;
        }
        return DB_BOOL(parsed);
    }
    if (out_was_explicit != NULL) {
        *out_was_explicit = 0;
    }
    return default_value;
}

static inline int
db_display_pixel_format_uses_rgba16f(db_pixel_format_t format) {
    return DB_BOOL(format == DB_PIXEL_FORMAT_RGBA16F);
}

static inline const char *
db_display_pixel_format_name(db_pixel_format_t format) {
    return (format == DB_PIXEL_FORMAT_RGBA16F) ? "rgba16f" : "rgba8";
}

static inline int db_display_shadow_present_texture_format_uses_rgba16f(
    db_gl_shadow_present_texture_format_t format) {
    return DB_BOOL(format == DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F);
}

static inline const char *db_display_shadow_present_texture_format_name(
    db_gl_shadow_present_texture_format_t format) {
    switch (format) {
    case DB_GL_SHADOW_PRESENT_TEXTURE_RGBA8:
        return "rgba8";
    case DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F:
        return "rgba16f";
    case DB_GL_SHADOW_PRESENT_TEXTURE_BT2020_PQ_RGB10A2:
        return "bt2020_pq_rgb10a2";
    }
    return "unknown";
}

static inline db_pixel_format_t db_display_working_format_from_options(void) {
    const char *value = db_runtime_option_get(DB_RUNTIME_OPT_WORKING_FORMAT);
    return ((value != NULL) && (strcmp(value, "rgba8") == 0))
               ? DB_PIXEL_FORMAT_RGBA8
               : DB_PIXEL_FORMAT_RGBA16F;
}

static inline db_output_format_request_t
db_display_output_format_from_options(void) {
    const char *value = db_runtime_option_get(DB_RUNTIME_OPT_OUTPUT_FORMAT);
    if ((value != NULL) && (strcmp(value, "sdr") == 0)) {
        return DB_OUTPUT_FORMAT_SDR;
    }
    if ((value != NULL) && (strcmp(value, "hdr") == 0)) {
        return DB_OUTPUT_FORMAT_HDR;
    }
    return DB_OUTPUT_FORMAT_AUTO;
}

static inline db_presentation_transform_t
db_display_presentation_transform(uint32_t destination_width,
                                  uint32_t destination_height) {
    return (db_presentation_transform_t){
        .source_width = db_grid_cols_effective(),
        .source_height = db_grid_rows_effective(),
        .destination_width = destination_width,
        .destination_height = destination_height,
        .viewport_x = 0U,
        .viewport_y = 0U,
        .viewport_width = destination_width,
        .viewport_height = destination_height,
        .filter = DB_PRESENT_SCALE_NEAREST,
    };
}

static inline const char *
db_display_output_request_name(db_output_format_request_t request) {
    return db_output_format_request_name(request);
}

static inline void db_display_log_presentation_contract(
    const char *backend, const db_display_renderer_runtime_t *runtime,
    const db_presentation_transform_t *transform) {
    if ((backend == NULL) || (runtime == NULL) || (transform == NULL)) {
        return;
    }
    const db_log_field_t capability_fields[] = {
        DB_LOG_TOKEN("output_request",
                     db_display_output_request_name(
                         runtime->renderer.format.output_request)),
        DB_LOG_BOOL("native_output_resolved",
                    runtime->renderer.format.native_output_resolution_pending ==
                        0),
        DB_LOG_BOOL("native_hdr_verified",
                    runtime->renderer.format.native_hdr_enabled),
        DB_LOG_BOOL("hdr_content_supported",
                    runtime->renderer.format.hdr_content_supported),
        DB_LOG_BOOL("native_format_supported",
                    runtime->renderer.format.native_format_supported),
        DB_LOG_BOOL("colorspace_supported",
                    runtime->renderer.format.colorspace_supported),
        DB_LOG_BOOL("metadata_supported",
                    runtime->renderer.format.metadata_supported),
        DB_LOG_BOOL("sink_hdr_supported",
                    runtime->renderer.format.sink_hdr_supported),
        DB_LOG_BOOL("commit_verified",
                    runtime->renderer.format.commit_verified),
        DB_LOG_TOKEN("native_format",
                     db_native_output_format_name(
                         runtime->renderer.format.native_output_format)),
        DB_LOG_U64("native_bit_depth",
                   runtime->renderer.format.native_bit_depth),
        DB_LOG_TOKEN("colorspace",
                     db_output_colorspace_name(
                         runtime->renderer.format.output_colorspace)),
        DB_LOG_TOKEN("transfer", db_output_transfer_name(
                                     runtime->renderer.format.output_transfer)),
        DB_LOG_TOKEN("conversion",
                     db_output_conversion_name(
                         runtime->renderer.format.output_conversion)),
        DB_LOG_TOKEN("encoded_present_format",
                     db_encoded_present_format_name(
                         runtime->renderer.format.encoded_present_format)),
        DB_LOG_TOKEN("hdr_conversion",
                     db_hdr_conversion_implementation_name(
                         runtime->renderer.format.hdr_conversion)),
        DB_LOG_DOUBLE("reference_white_nits",
                      runtime->renderer.format.hdr10.reference_white_nits),
        DB_LOG_DOUBLE("peak_nits",
                      runtime->renderer.format.hdr10.mastering_max_nits),
        DB_LOG_STRING("reason",
                      (runtime->renderer.format.fallback_reason != NULL)
                          ? runtime->renderer.format.fallback_reason
                          : "none"),
    };
    db_log_info(backend, "presentation_capability", capability_fields,
                DB_LOG_FIELD_COUNT(capability_fields));
    const db_log_field_t contract_fields[] = {
        DB_LOG_U64("source_width", transform->source_width),
        DB_LOG_U64("source_height", transform->source_height),
        DB_LOG_U64("destination_width", transform->destination_width),
        DB_LOG_U64("destination_height", transform->destination_height),
        DB_LOG_TOKEN("scale_filter", "nearest"),
        DB_LOG_TOKEN("working_format",
                     db_display_pixel_format_name(
                         runtime->renderer.format.surface_pixel_format)),
        DB_LOG_TOKEN("native_format",
                     db_native_output_format_name(
                         runtime->renderer.format.native_output_format)),
        DB_LOG_BOOL("native_hdr", runtime->renderer.format.native_hdr_enabled),
        DB_LOG_U64("native_bit_depth",
                   runtime->renderer.format.native_bit_depth),
        DB_LOG_TOKEN("colorspace",
                     db_output_colorspace_name(
                         runtime->renderer.format.output_colorspace)),
        DB_LOG_TOKEN("transfer", db_output_transfer_name(
                                     runtime->renderer.format.output_transfer)),
        DB_LOG_TOKEN("conversion",
                     db_output_conversion_name(
                         runtime->renderer.format.output_conversion)),
        DB_LOG_TOKEN("encoded_present_format",
                     db_encoded_present_format_name(
                         runtime->renderer.format.encoded_present_format)),
        DB_LOG_TOKEN("hdr_conversion",
                     db_hdr_conversion_implementation_name(
                         runtime->renderer.format.hdr_conversion)),
    };
    db_log_info(backend, "presentation_contract", contract_fields,
                DB_LOG_FIELD_COUNT(contract_fields));
}

static inline db_display_resolved_format_config_t
db_display_resolve_format_config_or_fail(
    const char *backend, db_pixel_format_t working_format,
    db_output_format_request_t output_request,
    const db_native_output_capability_t *capability) {
    const db_native_output_capability_t safe_capability =
        (capability != NULL)
            ? *capability
            : (db_native_output_capability_t){
                  .native_hdr_verified = 0,
                  .native_bit_depth = DB_DISPLAY_SDR_BIT_DEPTH,
                  .hdr_format = DB_NATIVE_OUTPUT_XRGB2101010,
                  .hdr_colorspace = DB_OUTPUT_COLORSPACE_BT2020,
                  .hdr_transfer = DB_OUTPUT_TRANSFER_PQ,
                  .unavailable_reason = "native_hdr_not_verified",
              };
    if ((output_request == DB_OUTPUT_FORMAT_HDR) &&
        (safe_capability.native_hdr_verified == 0)) {
        DB_RUNTIME_FAIL(
            (backend != NULL) ? backend : "display_runtime_config",
            "output-format=hdr requested but native HDR is unavailable: "
            "%s",
            (safe_capability.unavailable_reason != NULL)
                ? safe_capability.unavailable_reason
                : "native_hdr_not_verified");
    }
    const int hdr_enabled = DB_BOOL((output_request != DB_OUTPUT_FORMAT_SDR) &&
                                    (safe_capability.native_hdr_verified != 0));
    db_display_format_reason_t reason =
        DB_DISPLAY_FORMAT_REASON_AUTO_SDR_FALLBACK;
    if (hdr_enabled != 0) {
        reason = DB_DISPLAY_FORMAT_REASON_HDR_VERIFIED;
    } else if (output_request == DB_OUTPUT_FORMAT_SDR) {
        reason = DB_DISPLAY_FORMAT_REASON_SDR_REQUESTED;
    }
    uint32_t native_bit_depth = DB_DISPLAY_SDR_BIT_DEPTH;
    if (hdr_enabled != 0) {
        native_bit_depth = safe_capability.native_bit_depth;
        if (native_bit_depth == 0U) {
            native_bit_depth = DB_DISPLAY_HDR10_BIT_DEPTH;
        }
    }
    return (db_display_resolved_format_config_t){
        .output_request = output_request,
        .native_output_resolution_pending = 0,
        .hdr_content_supported = 1,
        .native_hdr_enabled = hdr_enabled,
        .native_format_supported = safe_capability.native_format_supported,
        .colorspace_supported = safe_capability.colorspace_supported,
        .metadata_supported = safe_capability.metadata_supported,
        .sink_hdr_supported = safe_capability.sink_hdr_supported,
        .commit_verified = safe_capability.commit_verified,
        .surface_pixel_format = working_format,
        .present_texture_format =
            db_gl_texture_format_from_pixel_format(working_format),
        .native_output_format = (hdr_enabled != 0) ? safe_capability.hdr_format
                                                   : DB_NATIVE_OUTPUT_XRGB8888,
        .output_colorspace = (hdr_enabled != 0) ? safe_capability.hdr_colorspace
                                                : DB_OUTPUT_COLORSPACE_SRGB,
        .output_transfer = (hdr_enabled != 0) ? safe_capability.hdr_transfer
                                              : DB_OUTPUT_TRANSFER_SRGB,
        .output_conversion = (hdr_enabled != 0)
                                 ? DB_OUTPUT_CONVERSION_LINEAR_SRGB_TO_BT2020_PQ
                                 : DB_OUTPUT_CONVERSION_LINEAR_SRGB_TO_SDR,
        .encoded_present_format = (hdr_enabled != 0)
                                      ? DB_ENCODED_PRESENT_BT2020_PQ_RGB10A2
                                      : DB_ENCODED_PRESENT_SDR_RGBA8,
        .hdr_conversion = DB_HDR_CONVERSION_NONE,
        .hdr10 = db_hdr10_mastering_profile(),
        .native_bit_depth = native_bit_depth,
        .framebuffer_hash_format = DB_PIXEL_FORMAT_RGBA8,
        .reason = reason,
        .fallback_reason =
            (hdr_enabled != 0) ? "none" : safe_capability.unavailable_reason,
    };
}

static inline db_render_format_contract_t
db_render_format_contract_from_display(
    const db_display_resolved_format_config_t *format) {
    const db_display_resolved_format_config_t safe_format =
        (format != NULL)
            ? *format
            : db_display_resolve_format_config_or_fail(
                  "display_runtime_config", DB_PIXEL_FORMAT_RGBA16F,
                  DB_OUTPUT_FORMAT_AUTO, NULL);
    return (db_render_format_contract_t){
        .renderer_write_format = safe_format.surface_pixel_format,
        .upload_format = db_gl_pixel_format_from_texture_format(
            safe_format.present_texture_format),
        .presentation_format = safe_format.present_texture_format,
        .canonical_hash_format = safe_format.framebuffer_hash_format,
        .conversion =
            (safe_format.surface_pixel_format == DB_PIXEL_FORMAT_RGBA16F)
                ? DB_RENDER_FORMAT_CONVERSION_F64_TO_RGBA16F
                : DB_RENDER_FORMAT_CONVERSION_F64_TO_RGBA8,
        .reason = safe_format.reason,
    };
}

static inline void db_display_apply_native_output_capability_or_fail(
    const char *backend, db_display_renderer_runtime_t *runtime,
    const db_native_output_capability_t *capability) {
    if (runtime == NULL) {
        return;
    }
    runtime->renderer.format = db_display_resolve_format_config_or_fail(
        backend, runtime->renderer.format.surface_pixel_format,
        runtime->renderer.format.output_request, capability);
    runtime->renderer.format_contract =
        db_render_format_contract_from_display(&runtime->renderer.format);
    db_run_log_identity_t identity = db_run_log_identity_current();
    identity.native_format = db_native_output_format_name(
        runtime->renderer.format.native_output_format);
    db_run_log_identity_configure(&identity);
}

static inline db_display_runtime_config_t
db_display_runtime_config_from_cli(const db_cli_config_t *cfg) {
    db_display_runtime_config_t config = {
        .debug_clear_default_framebuffer = 0,
        .fps_cap = BENCH_FPS_CAP,
        .frame_limit = 0U,
        .hash_mode = "none",
        .hash_report = "both",
    };
    if (cfg == NULL) {
        return config;
    }
    config.debug_clear_default_framebuffer =
        cfg->debug_clear_default_framebuffer;
    config.fps_cap = cfg->fps_cap;
    config.frame_limit = cfg->frame_limit;
    config.hash_mode = (cfg->hash_mode != NULL) ? cfg->hash_mode : "none";
    config.hash_report = (cfg->hash_report != NULL) ? cfg->hash_report : "both";
    return config;
}

static inline db_display_hash_settings_t
db_display_hash_settings_from_config(const db_display_runtime_config_t *config,
                                     int default_state_hash_enabled,
                                     int default_output_hash_enabled) {
    if (config == NULL) {
        return db_display_resolve_hash_settings(
            default_state_hash_enabled, default_output_hash_enabled, "none");
    }
    return db_display_resolve_hash_settings(default_state_hash_enabled,
                                            default_output_hash_enabled,
                                            config->hash_mode);
}

static inline db_display_runtime_hash_config_t
db_display_runtime_hash_config_from_cli(const db_cli_config_t *cfg,
                                        int default_state_hash_enabled,
                                        int default_output_hash_enabled) {
    const db_display_runtime_config_t runtime =
        db_display_runtime_config_from_cli(cfg);
    return (db_display_runtime_hash_config_t){
        .runtime = runtime,
        .hash_settings = db_display_hash_settings_from_config(
            &runtime, default_state_hash_enabled, default_output_hash_enabled),
    };
}

static inline db_display_renderer_runtime_t
db_display_renderer_runtime_from_cli(
    const char *backend_name, const db_cli_config_t *effective_cfg,
    uint32_t preserved_framebuffer_count, int default_state_hash_enabled,
    int default_output_hash_enabled,
    db_native_output_resolution_policy_t native_output_resolution_policy) {
    const db_cli_config_t safe_cfg =
        (effective_cfg != NULL) ? *effective_cfg : (db_cli_config_t){0};
    const db_display_runtime_hash_config_t runtime_hash_cfg =
        db_display_runtime_hash_config_from_cli(
            &safe_cfg, default_state_hash_enabled, default_output_hash_enabled);
    db_benchmark_runtime_init_t benchmark = {0};
    (void)db_init_benchmark_runtime_from_options(
        backend_name,
        &(const db_benchmark_runtime_options_t){
            .benchmark_mode_text =
                db_runtime_option_get(DB_RUNTIME_OPT_BENCHMARK_MODE),
            .bench_speed_text =
                db_runtime_option_get(DB_RUNTIME_OPT_BENCH_SPEED),
            .random_seed_text =
                db_runtime_option_get(DB_RUNTIME_OPT_RANDOM_SEED),
            .backbuffer_draw_full = safe_cfg.backbuffer_draw_full,
        },
        &benchmark);
    const db_output_format_request_t output_request =
        db_display_output_format_from_options();
    const int native_output_resolution_deferred =
        DB_BOOL(native_output_resolution_policy ==
                DB_NATIVE_OUTPUT_RESOLVE_AFTER_PRESENTER_PROBE);
    db_display_resolved_format_config_t format =
        db_display_resolve_format_config_or_fail(
            backend_name, db_display_working_format_from_options(),
            (native_output_resolution_deferred != 0) ? DB_OUTPUT_FORMAT_AUTO
                                                     : output_request,
            NULL);
    if (native_output_resolution_deferred != 0) {
        format.output_request = output_request;
        format.native_output_resolution_pending = 1;
        format.fallback_reason = "presenter_capability_pending";
    }
    db_renderer_execution_config_t renderer =
        db_benchmark_renderer_execution_config(&benchmark);
    db_trace_config_t trace = {0};
    (void)db_parse_int_text(db_runtime_option_get(DB_RUNTIME_OPT_TRACE_DAMAGE),
                            &trace.damage);
    (void)db_parse_int_text(
        db_runtime_option_get(DB_RUNTIME_OPT_TRACE_SHADOW_UPLOAD),
        &trace.shadow_upload);
    (void)db_parse_int_text(
        db_runtime_option_get(DB_RUNTIME_OPT_TRACE_GL_ERRORS),
        &trace.gl_errors);
    (void)db_parse_int_text(db_runtime_option_get(DB_RUNTIME_OPT_TRACE_VULKAN),
                            &trace.vulkan);
    renderer.trace = trace;
    db_trace_configure(&trace);
    const char *execution_strategy = "cpu";
    if (safe_cfg.api == DB_API_VULKAN) {
        execution_strategy = "vulkan";
    } else if (safe_cfg.api == DB_API_OPENGL) {
        execution_strategy =
            (safe_cfg.renderer == DB_GL_RENDERER_GL3_3) ? "gl3" : "gl1";
    }
    const char *present_method = "cpu_present";
    if (safe_cfg.display == DB_OFFSCREEN_DISPLAY) {
        present_method = "offscreen";
    } else if ((safe_cfg.api == DB_API_OPENGL) ||
               (safe_cfg.api == DB_API_VULKAN)) {
        present_method = "sample_fullscreen";
    }
    db_run_log_identity_configure(&(const db_run_log_identity_t){
        .benchmark_mode = db_pattern_mode_name(benchmark.pattern),
        .presenter = backend_name,
        .execution_strategy = execution_strategy,
        .working_format = db_pixel_format_name(format.surface_pixel_format),
        .native_format =
            db_native_output_format_name(format.native_output_format),
        .present_method = present_method,
    });
    return (db_display_renderer_runtime_t){
        .effective_cfg = safe_cfg,
        .display = runtime_hash_cfg.runtime,
        .hash_settings = runtime_hash_cfg.hash_settings,
        .benchmark = benchmark,
        .renderer =
            {
                .execution = renderer,
                .format = format,
                .format_contract =
                    db_render_format_contract_from_display(&format),
                .preserved_framebuffer_count = preserved_framebuffer_count,
            },
        .presentation = db_display_presentation_transform(
            db_grid_cols_effective(), db_grid_rows_effective()),
    };
}

static inline db_display_frame_step_t db_display_frame_step_make(
    const char *api_name, const char *backend, const char *renderer_name,
    db_display_hash_tracker_t *output_hash_tracker,
    db_display_hash_tracker_t *state_hash_tracker,
    double *next_progress_log_due_ms, uint32_t work_unit_count,
    int output_hash_enabled, int state_hash_enabled, uint32_t frame_limit) {
    return (db_display_frame_step_t){
        .api_name = api_name,
        .backend = backend,
        .renderer_name = renderer_name,
        .output_hash_tracker = output_hash_tracker,
        .state_hash_tracker = state_hash_tracker,
        .next_progress_log_due_ms = next_progress_log_due_ms,
        .frame_limit = frame_limit,
        .work_unit_count = work_unit_count,
        .output_hash_enabled = output_hash_enabled,
        .state_hash_enabled = state_hash_enabled,
    };
}

static inline int
db_display_frame_step_should_hash_output(const db_display_frame_step_t *step,
                                         uint32_t frame_index) {
    return DB_BOOL(
        (step != NULL) && (step->output_hash_enabled != 0) &&
        db_display_hash_tracker_should_sample(step->output_hash_tracker,
                                              frame_index, step->frame_limit));
}

static inline int
db_display_frame_step_should_hash_state(const db_display_frame_step_t *step,
                                        uint32_t frame_index) {
    return DB_BOOL(
        (step != NULL) && (step->state_hash_enabled != 0) &&
        db_display_hash_tracker_should_sample(step->state_hash_tracker,
                                              frame_index, step->frame_limit));
}

static inline db_display_dual_hash_trackers_t
db_display_dual_hash_trackers_create(const char *backend,
                                     const db_display_hash_settings_t *settings,
                                     const char *hash_report,
                                     const char *state_key,
                                     const char *output_key) {
    const db_display_hash_settings_t safe_settings =
        (settings != NULL) ? *settings : (db_display_hash_settings_t){0, 0};
    return (db_display_dual_hash_trackers_t){
        .state = db_display_hash_tracker_create(
            backend, safe_settings.state_hash_enabled, state_key, hash_report),
        .output = db_display_hash_tracker_create(
            backend, safe_settings.output_hash_enabled, output_key,
            hash_report),
    };
}

static inline db_display_dual_hash_trackers_t
db_display_dual_hash_trackers_create_from_runtime(
    const char *backend,
    const db_display_runtime_hash_config_t *runtime_hash_cfg,
    const char *state_key, const char *output_key) {
    if (runtime_hash_cfg == NULL) {
        return db_display_dual_hash_trackers_create(backend, NULL, "both",
                                                    state_key, output_key);
    }
    return db_display_dual_hash_trackers_create(
        backend, &runtime_hash_cfg->hash_settings,
        runtime_hash_cfg->runtime.hash_report, state_key, output_key);
}

static inline db_display_dual_hash_trackers_t
db_display_dual_hash_trackers_create_from_resolved_runtime(
    const char *backend, const db_display_renderer_runtime_t *resolved_runtime,
    const char *state_key, const char *output_key) {
    if (resolved_runtime == NULL) {
        return db_display_dual_hash_trackers_create(backend, NULL, "both",
                                                    state_key, output_key);
    }
    return db_display_dual_hash_trackers_create(
        backend, &resolved_runtime->hash_settings,
        resolved_runtime->display.hash_report, state_key, output_key);
}

static inline void db_display_dual_hash_trackers_log_final(
    const char *backend, const db_display_dual_hash_trackers_t *trackers) {
    if (trackers == NULL) {
        return;
    }
    db_display_hash_tracker_log_final(backend, &trackers->state);
    db_display_hash_tracker_log_final(backend, &trackers->output);
}

static inline void db_display_dual_hash_trackers_finalize(
    db_display_dual_hash_trackers_t *trackers,
    const db_display_hash_settings_t *settings, uint64_t (*state_hash_fn)(void),
    uint64_t (*output_hash_fn)(void)) {
    if ((trackers == NULL) || (settings == NULL)) {
        return;
    }
    if ((settings->state_hash_enabled != 0) && (state_hash_fn != NULL)) {
        trackers->state.final_hash = state_hash_fn();
    }
    if ((settings->output_hash_enabled != 0) && (output_hash_fn != NULL)) {
        trackers->output.final_hash = output_hash_fn();
    }
}

static inline void
db_display_cpu_frame_step(const db_display_frame_step_t *step,
                          uint32_t frame_index, double elapsed_ms,
                          uint64_t (*state_hash_fn)(void),
                          uint64_t (*output_hash_fn)(void)) {
    if ((step == NULL) || (step->next_progress_log_due_ms == NULL)) {
        return;
    }
    if ((step->state_hash_enabled != 0) && (step->state_hash_tracker != NULL) &&
        (state_hash_fn != NULL)) {
        db_display_hash_tracker_record(step->state_hash_tracker,
                                       state_hash_fn());
    }
    if ((step->output_hash_enabled != 0) &&
        (step->output_hash_tracker != NULL) && (output_hash_fn != NULL)) {
        db_display_hash_tracker_record(step->output_hash_tracker,
                                       output_hash_fn());
    }
    db_log_progress_periodic(step->api_name, step->renderer_name, step->backend,
                             (uint64_t)frame_index + 1U, step->work_unit_count,
                             elapsed_ms, step->next_progress_log_due_ms,
                             BENCH_LOG_INTERVAL_MS);
}

static inline void
db_display_gl_frame_step(const db_display_frame_step_t *step,
                         uint32_t frame_index, double elapsed_ms,
                         int has_state_hash, uint64_t state_hash_value,
                         int has_output_hash, uint64_t output_hash_value) {
    if ((step == NULL) || (step->next_progress_log_due_ms == NULL)) {
        return;
    }
    if ((step->state_hash_enabled != 0) && (step->state_hash_tracker != NULL) &&
        (has_state_hash != 0)) {
        db_display_hash_tracker_record(step->state_hash_tracker,
                                       state_hash_value);
    }
    if ((step->output_hash_enabled != 0) &&
        (step->output_hash_tracker != NULL) && (has_output_hash != 0)) {
        db_display_hash_tracker_record(step->output_hash_tracker,
                                       output_hash_value);
    }
    db_log_progress_periodic(step->api_name, step->renderer_name, step->backend,
                             (uint64_t)frame_index + 1U, step->work_unit_count,
                             elapsed_ms, step->next_progress_log_due_ms,
                             BENCH_LOG_INTERVAL_MS);
}

static inline int
db_display_format_draw_stats_log(char *buffer, size_t buffer_size,
                                 const db_renderer_draw_path_stats_t *stats) {
    const db_renderer_draw_path_stats_t safe_stats =
        (stats != NULL) ? *stats : (db_renderer_draw_path_stats_t){0};
    return db_snprintf(
        buffer, buffer_size,
        "draw stats: full_present_frames=%llu dirty_geometry_frames=%llu "
        "shadow_fallback_frames=%llu replay_only_frames=%llu",
        (unsigned long long)safe_stats.full_present_frames,
        (unsigned long long)safe_stats.dirty_geometry_frames,
        (unsigned long long)safe_stats.shadow_fallback_frames,
        (unsigned long long)safe_stats.replay_only_frames);
}

static inline void
db_display_log_draw_stats(const char *backend,
                          const db_renderer_draw_path_stats_t *stats) {
    const db_renderer_draw_path_stats_t safe_stats =
        (stats != NULL) ? *stats : (db_renderer_draw_path_stats_t){0};
    const db_log_field_t fields[] = {
        DB_LOG_U64("full_present_frames", safe_stats.full_present_frames),
        DB_LOG_U64("dirty_geometry_frames", safe_stats.dirty_geometry_frames),
        DB_LOG_U64("shadow_fallback_frames", safe_stats.shadow_fallback_frames),
        DB_LOG_U64("replay_only_frames", safe_stats.replay_only_frames),
    };
    db_log_info(backend, "draw_stats", fields, DB_LOG_FIELD_COUNT(fields));
}

static inline void db_display_log_draw_stats_with_fn(
    const char *backend, void (*draw_stats)(db_renderer_draw_path_stats_t *)) {
    if (draw_stats == NULL) {
        return;
    }
    db_renderer_draw_path_stats_t stats = {0};
    draw_stats(&stats);
    db_display_log_draw_stats(backend, &stats);
}

// Final summary contract:
// - draw stats are emitted only from final summary ownership
// - benchmark final logs are emitted only from the shared final summary path
static inline void db_display_log_renderer_final_summary(
    const char *api_name, const char *renderer_name, const char *backend,
    uint64_t frames, uint32_t work_unit_count, double elapsed_ms,
    void (*draw_stats)(db_renderer_draw_path_stats_t *)) {
    db_display_log_draw_stats_with_fn(backend, draw_stats);
    db_benchmark_log_final(api_name, renderer_name, backend, frames,
                           work_unit_count, elapsed_ms);
}

#endif
