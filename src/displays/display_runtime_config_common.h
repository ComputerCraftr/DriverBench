#ifndef DRIVERBENCH_DISPLAY_RUNTIME_CONFIG_COMMON_H
#define DRIVERBENCH_DISPLAY_RUNTIME_CONFIG_COMMON_H

#include <stdint.h>

#include "../config/benchmark_config.h"
#include "../config/runtime_options.h"
#include "../core/db_core.h"
#include "../driverbench_config.h"
#include "display_hash_common.h"

typedef struct {
    int option_enables_hdr;
    int option_explicitly_requests_hdr;
} db_display_cpu_hdr_option_state_t;

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
    const char *api_name;
    const char *backend;
    const char *capability_mode;
    const char *renderer_name;
    db_display_hash_tracker_t *output_hash_tracker;
    db_display_hash_tracker_t *state_hash_tracker;
    double *next_progress_log_due_ms;
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
        return (parsed != 0) ? 1 : 0;
    }
    if (out_was_explicit != NULL) {
        *out_was_explicit = 0;
    }
    return default_value;
}

static inline db_display_cpu_hdr_option_state_t
db_display_cpu_hdr_option_state(void) {
    db_display_cpu_hdr_option_state_t state = {0};
    state.option_enables_hdr = db_display_runtime_option_parse_bool(
        DB_RUNTIME_OPT_CPU_HDR, 1, &state.option_explicitly_requests_hdr);
    return state;
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

static inline db_display_frame_step_t db_display_frame_step_make(
    const char *api_name, const char *backend, const char *capability_mode,
    const char *renderer_name, db_display_hash_tracker_t *output_hash_tracker,
    db_display_hash_tracker_t *state_hash_tracker,
    double *next_progress_log_due_ms, uint32_t work_unit_count,
    int output_hash_enabled, int state_hash_enabled) {
    return (db_display_frame_step_t){
        .api_name = api_name,
        .backend = backend,
        .capability_mode = capability_mode,
        .renderer_name = renderer_name,
        .output_hash_tracker = output_hash_tracker,
        .state_hash_tracker = state_hash_tracker,
        .next_progress_log_due_ms = next_progress_log_due_ms,
        .work_unit_count = work_unit_count,
        .output_hash_enabled = output_hash_enabled,
        .state_hash_enabled = state_hash_enabled,
    };
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
    db_benchmark_log_periodic(
        step->api_name, step->renderer_name, step->backend,
        (uint64_t)frame_index + 1U, step->work_unit_count, elapsed_ms,
        step->capability_mode, step->next_progress_log_due_ms,
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
    db_benchmark_log_periodic(
        step->api_name, step->renderer_name, step->backend,
        (uint64_t)frame_index + 1U, step->work_unit_count, elapsed_ms,
        step->capability_mode, step->next_progress_log_due_ms,
        BENCH_LOG_INTERVAL_MS);
}

static inline void db_display_log_draw_stats(const char *backend,
                                             uint64_t full_draw_frames,
                                             uint64_t dirty_draw_frames) {
    db_infof(backend,
             "draw stats: full_draw_frames=%llu dirty_draw_frames=%llu",
             (unsigned long long)full_draw_frames,
             (unsigned long long)dirty_draw_frames);
}

static inline void
db_display_log_draw_stats_with_fn(const char *backend,
                                  void (*draw_stats)(uint64_t *, uint64_t *)) {
    if (draw_stats == NULL) {
        return;
    }
    uint64_t full_draw_frames = 0U;
    uint64_t dirty_draw_frames = 0U;
    draw_stats(&full_draw_frames, &dirty_draw_frames);
    db_display_log_draw_stats(backend, full_draw_frames, dirty_draw_frames);
}

#endif
