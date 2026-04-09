#include "cli/cli_parse.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config/benchmark_config.h"
#include "config/runtime_options.h"
#include "core/db_core.h"
#include "displays/display_dispatch.h"
#include "displays/display_types.h"
#include "driverbench_config.h"
#include "renderers/renderer_benchmark_types.h"
#include "renderers/renderer_gl_common.h"

typedef struct {
    const char *cli_option;
    const char *runtime_option;
    int kind;
} db_cli_runtime_option_map_t;

enum {
    DB_CLI_RT_BOOL = 0,
    DB_CLI_RT_FPS_CAP = 1,
    DB_CLI_RT_MODE = 2,
    DB_CLI_RT_RANDOM_SEED = 3,
    DB_CLI_RT_HASH_REPORT = 4,
    DB_CLI_RT_FRAME_LIMIT = 5,
    DB_CLI_RT_HASH_MODE = 6,
    DB_CLI_RT_BENCH_SPEED = 7,
    DB_CLI_RT_GLFW_HIDDEN_WINDOW = 8,
    DB_CLI_RT_VSYNC = 9,
    DB_CLI_RT_DEBUG_CLEAR_DEFAULT_FRAMEBUFFER = 10,
    DB_CLI_RT_BACKBUFFER_DRAW_MODE = 11,
    DB_CLI_RT_CPU_HDR = 12,
    DB_CLI_RT_VK_NO_PRESENT = 13,
    DB_CLI_RT_METRICS_MODE = 14,
    DB_CLI_RT_VK_ALLOW_CPU_WORKERS = 15,
    DB_CLI_RT_VK_MULTI_DEVICE_POLICY = 16,
    DB_CLI_RT_PRESENT_BUFFER_MODE = 17,
};

#define DB_CLI_RUNTIME_TEXT_LEN 64U
#define DB_CLI_RUNTIME_TEXT_SLOTS 32U

static struct {
    char slots[DB_CLI_RUNTIME_TEXT_SLOTS][DB_CLI_RUNTIME_TEXT_LEN];
    size_t used;
} g_cli_runtime_text_pool = {0};

static int db_string_is(const char *value, const char *expected) {
    return (value != NULL) && (expected != NULL) &&
           (strcmp(value, expected) == 0);
}

static int db_cli_set_error(char *error, size_t error_size, const char *fmt,
                            ...) {
    if ((error == NULL) || (error_size == 0U)) {
        return 0;
    }
    {
        __builtin_va_list args;
        __builtin_va_start(args, fmt);
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
        (void)db_vsnprintf(error, error_size, fmt, args);
#ifdef __clang__
#pragma clang diagnostic pop
#endif
        __builtin_va_end(args);
    }
    return 0;
}

static const char *db_cli_mode_normalized_or_null(const char *value) {
    if (db_string_is(value, DB_BENCHMARK_MODE_GRADIENT_SWEEP)) {
        return DB_BENCHMARK_MODE_GRADIENT_SWEEP;
    }
    if (db_string_is(value, DB_BENCHMARK_MODE_BANDS)) {
        return DB_BENCHMARK_MODE_BANDS;
    }
    if (db_string_is(value, DB_BENCHMARK_MODE_SNAKE_GRID)) {
        return DB_BENCHMARK_MODE_SNAKE_GRID;
    }
    if (db_string_is(value, DB_BENCHMARK_MODE_GRADIENT_FILL)) {
        return DB_BENCHMARK_MODE_GRADIENT_FILL;
    }
    if (db_string_is(value, DB_BENCHMARK_MODE_SNAKE_RECT)) {
        return DB_BENCHMARK_MODE_SNAKE_RECT;
    }
    if (db_string_is(value, DB_BENCHMARK_MODE_SNAKE_SHAPES)) {
        return DB_BENCHMARK_MODE_SNAKE_SHAPES;
    }
    return NULL;
}

static const char *db_cli_store_runtime_text_or_null(const char *value) {
    if ((value == NULL) ||
        (g_cli_runtime_text_pool.used >= DB_CLI_RUNTIME_TEXT_SLOTS)) {
        return NULL;
    }
    char *slot = g_cli_runtime_text_pool.slots[g_cli_runtime_text_pool.used];
    const int written = db_snprintf(slot, DB_CLI_RUNTIME_TEXT_LEN, "%s", value);
    if ((written < 0) || ((size_t)written >= DB_CLI_RUNTIME_TEXT_LEN)) {
        return NULL;
    }
    g_cli_runtime_text_pool.used++;
    return slot;
}

static int db_cli_try_parse_bool_value(const char *cli_option,
                                       const char *raw_value, int *out_value,
                                       char *error, size_t error_size) {
    int parsed = 0;
    if ((out_value == NULL) || (db_parse_bool_text(raw_value, &parsed) == 0)) {
        return db_cli_set_error(error, error_size,
                                "invalid value for %s: %s (expected bool)",
                                cli_option, raw_value);
    }
    *out_value = parsed;
    return 1;
}

static int db_cli_try_expect_value(int argc, char **argv, int *index,
                                   const char **out_value, char *error,
                                   size_t error_size) {
    if (((*index) + 1) >= argc) {
        return db_cli_set_error(error, error_size,
                                "missing value for option: %s", argv[*index]);
    }
    (*index)++;
    *out_value = argv[*index];
    return 1;
}

static int db_cli_try_parse_frame_limit(const char *cli_option,
                                        const char *raw_value,
                                        uint32_t *out_value, char *error,
                                        size_t error_size) {
    char *end = NULL;
    const unsigned long parsed = strtoul(raw_value, &end, 10);
    if ((out_value == NULL) || (end == raw_value) || (end == NULL) ||
        (*end != '\0') || (parsed > UINT32_MAX)) {
        return db_cli_set_error(error, error_size, "invalid value for %s: %s",
                                cli_option, raw_value);
    }
    *out_value = db_checked_ulong_to_u32("driverbench_cli", cli_option, parsed);
    return 1;
}

static int db_cli_try_parse_random_seed(const char *raw_value, char *error,
                                        size_t error_size) {
    char *end = NULL;
    const unsigned long parsed = strtoul(raw_value, &end, 0);
    if ((end == raw_value) || (end == NULL) || (*end != '\0') ||
        (parsed > UINT32_MAX)) {
        return db_cli_set_error(error, error_size,
                                "invalid value for --random-seed: %s",
                                raw_value);
    }

    char normalized[32];
    (void)db_snprintf(normalized, sizeof(normalized), "%lu", parsed);
    const char *stored = db_cli_store_runtime_text_or_null(normalized);
    if (stored == NULL) {
        return db_cli_set_error(error, error_size,
                                "CLI runtime value too long: %s", raw_value);
    }
    db_runtime_option_set(DB_RUNTIME_OPT_RANDOM_SEED, stored);
    return 1;
}

static int db_cli_try_parse_fps_cap(const char *raw_value, double *out_value,
                                    char *error, size_t error_size) {
    double parsed = 0.0;
    if ((out_value == NULL) ||
        (db_parse_fps_cap_text(raw_value, &parsed) == 0)) {
        return db_cli_set_error(error, error_size,
                                "invalid value for --fps-cap: %s", raw_value);
    }
    *out_value = (parsed <= 0.0) ? 0.0 : parsed;
    return 1;
}

static int db_cli_try_parse_bench_speed(const char *raw_value, char *error,
                                        size_t error_size) {
    if (raw_value == NULL) {
        return db_cli_set_error(error, error_size,
                                "invalid value for --bench-speed: (null)");
    }
    char *end = NULL;
    const double parsed = strtod(raw_value, &end);
    if ((end == raw_value) || (end == NULL) || (*end != '\0') ||
        !isfinite(parsed) || (parsed <= 0.0)) {
        return db_cli_set_error(error, error_size,
                                "invalid value for --bench-speed: %s",
                                raw_value);
    }
    if (parsed > DB_BENCH_SPEED_STEP_MAX) {
        return db_cli_set_error(error, error_size,
                                "invalid value for --bench-speed: %s (max: %u)",
                                raw_value, DB_BENCH_SPEED_STEP_MAX);
    }
    char normalized[32];
    (void)db_snprintf(normalized, sizeof(normalized), "%.9g", parsed);
    const char *stored = db_cli_store_runtime_text_or_null(normalized);
    if (stored == NULL) {
        return db_cli_set_error(error, error_size,
                                "CLI runtime value too long: %s", raw_value);
    }
    db_runtime_option_set(DB_RUNTIME_OPT_BENCH_SPEED, stored);
    return 1;
}

static int db_cli_try_parse_runtime_mode(const char *raw_value, char *error,
                                         size_t error_size) {
    const char *normalized = db_cli_mode_normalized_or_null(raw_value);
    if (normalized == NULL) {
        return db_cli_set_error(
            error, error_size,
            "invalid value for --benchmark-mode: %s "
            "(expected: %s|%s|%s|%s|%s|%s)",
            raw_value, DB_BENCHMARK_MODE_GRADIENT_SWEEP,
            DB_BENCHMARK_MODE_BANDS, DB_BENCHMARK_MODE_SNAKE_GRID,
            DB_BENCHMARK_MODE_GRADIENT_FILL, DB_BENCHMARK_MODE_SNAKE_RECT,
            DB_BENCHMARK_MODE_SNAKE_SHAPES);
    }
    db_runtime_option_set(DB_RUNTIME_OPT_BENCHMARK_MODE, normalized);
    return 1;
}

static int db_cli_try_parse_backbuffer_draw_mode(const char *raw_value,
                                                 db_cli_config_t *cfg,
                                                 char *error,
                                                 size_t error_size) {
    if (cfg != NULL) {
        cfg->backbuffer_draw_mode_explicit = 1;
    }
    if (db_string_is(raw_value, "dirty")) {
        cfg->backbuffer_draw_full = 0;
        db_runtime_option_set_backbuffer_draw_full(0);
        return 1;
    }
    if (db_string_is(raw_value, "full")) {
        cfg->backbuffer_draw_full = 1;
        db_runtime_option_set_backbuffer_draw_full(1);
        return 1;
    }
    return db_cli_set_error(
        error, error_size,
        "invalid value for --backbuffer-draw-mode: %s (expected: dirty|full)",
        raw_value);
}

static int db_cli_try_parse_present_buffer_mode(const char *raw_value,
                                                db_cli_config_t *cfg,
                                                char *error,
                                                size_t error_size) {
    if (cfg != NULL) {
        cfg->present_buffer_mode_explicit = 1;
    }
    if (db_string_is(raw_value, "auto") || db_string_is(raw_value, "replace") ||
        db_string_is(raw_value, "single_source") ||
        db_string_is(raw_value, "ring")) {
        db_runtime_option_set_present_buffer_mode(raw_value);
        return 1;
    }
    return db_cli_set_error(error, error_size,
                            "invalid value for --present-buffer-mode: %s "
                            "(expected: auto|replace|single_source|ring)",
                            raw_value);
}

static int db_cli_try_parse_hash_report(const char *raw_value,
                                        const char **out_value, char *error,
                                        size_t error_size) {
    if ((out_value != NULL) && (db_string_is(raw_value, "final") ||
                                db_string_is(raw_value, "aggregate") ||
                                db_string_is(raw_value, "both"))) {
        *out_value = raw_value;
        return 1;
    }
    return db_cli_set_error(
        error, error_size,
        "invalid value for --hash-report: %s (expected: final|aggregate|both)",
        raw_value);
}

static int db_cli_try_parse_hash_mode(const char *raw_value,
                                      const char **out_value, char *error,
                                      size_t error_size) {
    if ((out_value != NULL) &&
        (db_string_is(raw_value, "none") || db_string_is(raw_value, "state") ||
         db_string_is(raw_value, "pixel") || db_string_is(raw_value, "both"))) {
        *out_value = raw_value;
        return 1;
    }
    return db_cli_set_error(
        error, error_size,
        "invalid value for --hash: %s (expected: none|state|pixel|both)",
        raw_value);
}

static int db_cli_try_parse_metrics_mode(const char *raw_value, char *error,
                                         size_t error_size) {
    if (db_string_is(raw_value, "basic") || db_string_is(raw_value, "dual")) {
        db_runtime_option_set(DB_RUNTIME_OPT_METRICS_MODE,
                              db_cli_store_runtime_text_or_null(raw_value));
        return 1;
    }
    return db_cli_set_error(
        error, error_size,
        "invalid value for --metrics-mode: %s (expected: basic|dual)",
        raw_value);
}

static int db_cli_try_parse_vk_multi_device_policy(const char *raw_value,
                                                   char *error,
                                                   size_t error_size) {
    if (db_string_is(raw_value, "auto") ||
        db_string_is(raw_value, "group_only") ||
        db_string_is(raw_value, "independent_ok")) {
        db_runtime_option_set(DB_RUNTIME_OPT_VK_MULTI_DEVICE_POLICY,
                              db_cli_store_runtime_text_or_null(raw_value));
        return 1;
    }
    return db_cli_set_error(error, error_size,
                            "invalid value for --vk-multi-device-policy: %s "
                            "(expected: auto|group_only|independent_ok)",
                            raw_value);
}

static int db_cli_try_parse_api(const char *value, db_cli_config_t *cfg,
                                char *error, size_t error_size) {
    if ((cfg == NULL) || (value == NULL)) {
        return db_cli_set_error(error, error_size, "config is null");
    }
    if (db_string_is(value, "auto")) {
        cfg->api_is_auto = 1;
        return 1;
    }
    cfg->api_is_auto = 0;
    if (db_string_is(value, "cpu")) {
        cfg->api = DB_API_CPU;
        return 1;
    }
    if (db_string_is(value, "opengl")) {
        cfg->api = DB_API_OPENGL;
        return 1;
    }
    if (db_string_is(value, "vulkan")) {
        cfg->api = DB_API_VULKAN;
        return 1;
    }
    return db_cli_set_error(error, error_size, "Unsupported api: %s", value);
}

static int db_cli_try_parse_display(const char *value, db_cli_config_t *cfg,
                                    char *error, size_t error_size) {
    if ((cfg == NULL) || (value == NULL)) {
        return db_cli_set_error(error, error_size, "config is null");
    }
    if (db_string_is(value, "offscreen")) {
        cfg->display = DB_DISPLAY_OFFSCREEN;
        cfg->display_is_set = 1;
        return 1;
    }
    if (db_string_is(value, "glfw_window")) {
        cfg->display = DB_DISPLAY_GLFW_WINDOW;
        cfg->display_is_set = 1;
        return 1;
    }
    if (db_string_is(value, "linux_kms_atomic")) {
        cfg->display = DB_DISPLAY_LINUX_KMS_ATOMIC;
        cfg->display_is_set = 1;
        return 1;
    }
    return db_cli_set_error(error, error_size, "Unsupported display: %s",
                            value);
}

static int db_cli_try_parse_renderer(const char *value, db_cli_config_t *cfg,
                                     char *error, size_t error_size) {
    if ((cfg == NULL) || (value == NULL)) {
        return db_cli_set_error(error, error_size, "config is null");
    }
    if (db_string_is(value, "auto")) {
        cfg->renderer_is_auto = 1;
        return 1;
    }
    cfg->renderer_is_auto = 0;
    if (db_string_is(value, "gl1_5_gles1_1")) {
        cfg->renderer = DB_GL_RENDERER_GL1_5_GLES1_1;
        return 1;
    }
    if (db_string_is(value, "gl3_3")) {
        cfg->renderer = DB_GL_RENDERER_GL3_3;
        return 1;
    }
    return db_cli_set_error(error, error_size, "Unsupported renderer: %s",
                            value);
}

static int db_cli_try_parse_runtime_override_option(const char *arg, int argc,
                                                    char **argv, int *index,
                                                    db_cli_config_t *cfg,
                                                    char *error,
                                                    size_t error_size) {
    static const db_cli_runtime_option_map_t mappings[] = {
        {"--allow-remote-display", DB_RUNTIME_OPT_ALLOW_REMOTE_DISPLAY,
         DB_CLI_RT_BOOL},
        {"--bench-speed", DB_RUNTIME_OPT_BENCH_SPEED, DB_CLI_RT_BENCH_SPEED},
        {"--backbuffer-draw-mode", DB_RUNTIME_OPT_BACKBUFFER_DRAW_MODE,
         DB_CLI_RT_BACKBUFFER_DRAW_MODE},
        {"--benchmark-mode", DB_RUNTIME_OPT_BENCHMARK_MODE, DB_CLI_RT_MODE},
        {"--cpu-hdr", DB_RUNTIME_OPT_CPU_HDR, DB_CLI_RT_CPU_HDR},
        {"--debug-clear-default-framebuffer", NULL,
         DB_CLI_RT_DEBUG_CLEAR_DEFAULT_FRAMEBUFFER},
        {"--fps-cap", DB_RUNTIME_OPT_FPS_CAP, DB_CLI_RT_FPS_CAP},
        {"--hash", DB_RUNTIME_OPT_HASH, DB_CLI_RT_HASH_MODE},
        {"--frame-limit", DB_RUNTIME_OPT_FRAME_LIMIT, DB_CLI_RT_FRAME_LIMIT},
        {"--glfw-hidden-window", NULL, DB_CLI_RT_GLFW_HIDDEN_WINDOW},
        {"--hash-report", DB_RUNTIME_OPT_HASH_REPORT, DB_CLI_RT_HASH_REPORT},
        {"--metrics-mode", DB_RUNTIME_OPT_METRICS_MODE, DB_CLI_RT_METRICS_MODE},
        {"--present-buffer-mode", DB_RUNTIME_OPT_PRESENT_BUFFER_MODE,
         DB_CLI_RT_PRESENT_BUFFER_MODE},
        {"--random-seed", DB_RUNTIME_OPT_RANDOM_SEED, DB_CLI_RT_RANDOM_SEED},
        {"--vk-allow-cpu-workers", DB_RUNTIME_OPT_VK_ALLOW_CPU_WORKERS,
         DB_CLI_RT_VK_ALLOW_CPU_WORKERS},
        {"--vk-multi-device-policy", DB_RUNTIME_OPT_VK_MULTI_DEVICE_POLICY,
         DB_CLI_RT_VK_MULTI_DEVICE_POLICY},
        {"--vk-no-present", DB_RUNTIME_OPT_VK_NO_PRESENT,
         DB_CLI_RT_VK_NO_PRESENT},
        {"--vsync", DB_RUNTIME_OPT_VSYNC, DB_CLI_RT_VSYNC},
    };

    for (size_t map_index = 0U;
         map_index < (sizeof(mappings) / sizeof(mappings[0])); map_index++) {
        if (db_string_is(arg, mappings[map_index].cli_option) == 0) {
            continue;
        }
        const char *value = NULL;
        if (db_cli_try_expect_value(argc, argv, index, &value, error,
                                    error_size) == 0) {
            return -1;
        }
        switch (mappings[map_index].kind) {
        case DB_CLI_RT_BOOL:
        case DB_CLI_RT_CPU_HDR:
        case DB_CLI_RT_VK_ALLOW_CPU_WORKERS:
        case DB_CLI_RT_VK_NO_PRESENT: {
            int parsed = 0;
            if (db_cli_try_parse_bool_value(mappings[map_index].cli_option,
                                            value, &parsed, error,
                                            error_size) == 0) {
                return -1;
            }
            db_runtime_option_set(mappings[map_index].runtime_option,
                                  (parsed != 0) ? "1" : "0");
            break;
        }
        case DB_CLI_RT_FRAME_LIMIT:
            if (db_cli_try_parse_frame_limit(mappings[map_index].cli_option,
                                             value, &cfg->frame_limit, error,
                                             error_size) == 0) {
                return -1;
            }
            break;
        case DB_CLI_RT_MODE:
            if (db_cli_try_parse_runtime_mode(value, error, error_size) == 0) {
                return -1;
            }
            break;
        case DB_CLI_RT_BACKBUFFER_DRAW_MODE:
            if (db_cli_try_parse_backbuffer_draw_mode(value, cfg, error,
                                                      error_size) == 0) {
                return -1;
            }
            break;
        case DB_CLI_RT_PRESENT_BUFFER_MODE:
            if (db_cli_try_parse_present_buffer_mode(value, cfg, error,
                                                     error_size) == 0) {
                return -1;
            }
            break;
        case DB_CLI_RT_FPS_CAP:
            if (db_cli_try_parse_fps_cap(value, &cfg->fps_cap, error,
                                         error_size) == 0) {
                return -1;
            }
            break;
        case DB_CLI_RT_RANDOM_SEED:
            if (db_cli_try_parse_random_seed(value, error, error_size) == 0) {
                return -1;
            }
            break;
        case DB_CLI_RT_HASH_REPORT:
            if (db_cli_try_parse_hash_report(value, &cfg->hash_report, error,
                                             error_size) == 0) {
                return -1;
            }
            break;
        case DB_CLI_RT_HASH_MODE:
            if (db_cli_try_parse_hash_mode(value, &cfg->hash_mode, error,
                                           error_size) == 0) {
                return -1;
            }
            break;
        case DB_CLI_RT_METRICS_MODE:
            if (db_cli_try_parse_metrics_mode(value, error, error_size) == 0) {
                return -1;
            }
            break;
        case DB_CLI_RT_VK_MULTI_DEVICE_POLICY:
            if (db_cli_try_parse_vk_multi_device_policy(value, error,
                                                        error_size) == 0) {
                return -1;
            }
            break;
        case DB_CLI_RT_BENCH_SPEED:
            if (db_cli_try_parse_bench_speed(value, error, error_size) == 0) {
                return -1;
            }
            break;
        case DB_CLI_RT_GLFW_HIDDEN_WINDOW:
            if (db_cli_try_parse_bool_value("--glfw-hidden-window", value,
                                            &cfg->glfw_window_hidden, error,
                                            error_size) == 0) {
                return -1;
            }
            break;
        case DB_CLI_RT_VSYNC:
            if (db_cli_try_parse_bool_value("--vsync", value,
                                            &cfg->vsync_enabled, error,
                                            error_size) == 0) {
                return -1;
            }
            break;
        case DB_CLI_RT_DEBUG_CLEAR_DEFAULT_FRAMEBUFFER:
            if (db_cli_try_parse_bool_value(
                    "--debug-clear-default-framebuffer", value,
                    &cfg->debug_clear_default_framebuffer, error,
                    error_size) == 0) {
                return -1;
            }
            break;
        default:
            return db_cli_set_error(error, error_size,
                                    "Unhandled runtime option kind: %d",
                                    mappings[map_index].kind)
                       ? 1
                       : -1;
        }
        return 1;
    }
    return 0;
}

static int db_cli_validate_compiled_support(const db_cli_config_t *cfg,
                                            char *error, size_t error_size) {
    if (cfg == NULL) {
        return db_cli_set_error(error, error_size, "config is null");
    }
    if (db_dispatch_display_capabilities(cfg->display).compiled == 0) {
        char caps_text[DB_DISPLAY_CAPABILITY_TEXT_MAX] = {0};
        (void)db_dispatch_format_display_capabilities(cfg->display, caps_text,
                                                      sizeof(caps_text));
        return db_cli_set_error(
            error, error_size,
            "requested display is unavailable in this build (%s)", caps_text);
    }
    if (cfg->api_is_auto != 0) {
        if (db_dispatch_display_has_any_api(cfg->display) == 0) {
            char caps_text[DB_DISPLAY_CAPABILITY_TEXT_MAX] = {0};
            (void)db_dispatch_format_display_capabilities(
                cfg->display, caps_text, sizeof(caps_text));
            return db_cli_set_error(
                error, error_size,
                "requested display has no compatible API (%s)", caps_text);
        }
        return 1;
    }
    if (db_dispatch_api_is_compiled(cfg->api) == 0) {
        return db_cli_set_error(error, error_size,
                                "requested API is unavailable in this build");
    }
    if (db_dispatch_display_supports_api(cfg->display, cfg->api) == 0) {
        char caps_text[DB_DISPLAY_CAPABILITY_TEXT_MAX] = {0};
        (void)db_dispatch_format_display_capabilities(cfg->display, caps_text,
                                                      sizeof(caps_text));
        return db_cli_set_error(
            error, error_size,
            "requested display/API combination is unavailable in this build "
            "(%s)",
            caps_text);
    }
    return 1;
}

static int db_cli_resolve_effective_api(const db_cli_config_t *cfg,
                                        db_api_t *out_api, char *error,
                                        size_t error_size) {
    if ((cfg == NULL) || (out_api == NULL)) {
        return db_cli_set_error(error, error_size, "config is null");
    }
    if (cfg->api_is_auto == 0) {
        *out_api = cfg->api;
        return 1;
    }
    if (db_dispatch_display_has_any_api(cfg->display) == 0) {
        char caps_text[DB_DISPLAY_CAPABILITY_TEXT_MAX] = {0};
        (void)db_dispatch_format_display_capabilities(cfg->display, caps_text,
                                                      sizeof(caps_text));
        return db_cli_set_error(error, error_size,
                                "requested display has no compatible API (%s)",
                                caps_text);
    }
    *out_api = db_dispatch_display_preferred_auto_api(cfg->display);
    return 1;
}

static int db_cli_validate_renderer_selection(const db_cli_config_t *cfg,
                                              char *error, size_t error_size) {
    if ((cfg == NULL) || (cfg->renderer_is_auto != 0)) {
        return 1;
    }
    if (cfg->api_is_auto != 0) {
        return db_cli_set_error(
            error, error_size,
            "--renderer requires an explicit API; set --api opengl");
    }
    db_api_t effective_api = DB_API_OPENGL;
    if (db_cli_resolve_effective_api(cfg, &effective_api, error, error_size) ==
        0) {
        return 0;
    }
    if (effective_api != DB_API_OPENGL) {
        return db_cli_set_error(error, error_size,
                                "--renderer requires effective API OpenGL, but "
                                "effective API is %s; "
                                "set --api opengl",
                                db_dispatch_api_name(effective_api));
    }
    if (db_dispatch_display_supports_backend(cfg->display, DB_API_OPENGL,
                                             cfg->renderer) == 0) {
        return db_cli_set_error(
            error, error_size,
            "--renderer %s is unavailable for selected display/backend",
            db_dispatch_gl_renderer_name(cfg->renderer));
    }
    return 1;
}

static int db_cli_validate_hash_mode(const db_cli_config_t *cfg, char *error,
                                     size_t error_size) {
    const char *hash_mode = cfg->hash_mode;
    if ((hash_mode == NULL) || (hash_mode[0] == '\0') ||
        db_string_is(hash_mode, "none")) {
        return 1;
    }
    db_api_t api = DB_API_OPENGL;
    if (db_cli_resolve_effective_api(cfg, &api, error, error_size) == 0) {
        return 0;
    }
    const int needs_state =
        db_string_is(hash_mode, "state") || db_string_is(hash_mode, "both");
    const int needs_pixel =
        db_string_is(hash_mode, "pixel") || db_string_is(hash_mode, "both");
    int supports_state = 0;
    int supports_pixel = 0;
    if ((cfg->display == DB_DISPLAY_GLFW_WINDOW) ||
        (cfg->display == DB_DISPLAY_OFFSCREEN)) {
        if (api == DB_API_VULKAN) {
            supports_state = 1;
            supports_pixel = (cfg->display == DB_DISPLAY_GLFW_WINDOW) ? 1 : 0;
        } else if ((api == DB_API_OPENGL) || (api == DB_API_CPU)) {
            supports_state = 1;
            supports_pixel = 1;
        }
    }
    if ((needs_state != 0) && (supports_state == 0)) {
        return db_cli_set_error(
            error, error_size,
            "hash mode '%s' is unsupported for display/API combination "
            "(display=%d api=%d): state hash unavailable",
            hash_mode, (int)cfg->display, (int)api);
    }
    if ((needs_pixel != 0) && (supports_pixel == 0)) {
        return db_cli_set_error(
            error, error_size,
            "hash mode '%s' is unsupported for display/API combination "
            "(display=%d api=%d): pixel hash unavailable",
            hash_mode, (int)cfg->display, (int)api);
    }
    return 1;
}

static int db_cli_validate_glfw_hidden_window(const db_cli_config_t *cfg,
                                              char *error, size_t error_size) {
    if ((cfg == NULL) || (cfg->glfw_window_hidden == 0)) {
        return 1;
    }
    if (cfg->display != DB_DISPLAY_GLFW_WINDOW) {
        return db_cli_set_error(
            error, error_size,
            "--glfw-hidden-window requires --display glfw_window");
    }
    return 1;
}

static const char *db_cli_present_buffer_mode_or_default(void) {
    const char *mode =
        db_runtime_option_get(DB_RUNTIME_OPT_PRESENT_BUFFER_MODE);
    return ((mode != NULL) && (mode[0] != '\0')) ? mode : "auto";
}

static int db_cli_validate_present_buffer_mode(const db_cli_config_t *cfg,
                                               char *error, size_t error_size) {
    if ((cfg == NULL) || (cfg->present_buffer_mode_explicit == 0)) {
        return 1;
    }
    db_gl_present_buffer_mode_t present_mode = DB_GL_PRESENT_BUFFER_MODE_AUTO;
    if (db_gl_present_buffer_mode_parse(db_cli_present_buffer_mode_or_default(),
                                        &present_mode) == 0) {
        return db_cli_set_error(error, error_size,
                                "invalid --present-buffer-mode");
    }
    if (present_mode == DB_GL_PRESENT_BUFFER_MODE_AUTO) {
        return 1;
    }
    db_api_t api = DB_API_OPENGL;
    if (db_cli_resolve_effective_api(cfg, &api, error, error_size) == 0) {
        return 0;
    }
    if (api == DB_API_VULKAN) {
        return db_cli_set_error(
            error, error_size,
            "--present-buffer-mode is unsupported for Vulkan");
    }
    const char *reason = NULL;
    if (db_gl_present_mode_validate_request(
            (api == DB_API_CPU) ? 1 : 0,
            (cfg->display == DB_DISPLAY_GLFW_WINDOW) ? 1 : 0,
            (cfg->renderer == DB_GL_RENDERER_GL1_5_GLES1_1) ? 1 : 0,
            (cfg->backbuffer_draw_full != 0) ? DB_GL_BACKBUFFER_DRAW_FULL
                                             : DB_GL_BACKBUFFER_DRAW_DIRTY,
            present_mode, &reason) == 0) {
        return db_cli_set_error(error, error_size, "%s",
                                (reason != NULL)
                                    ? reason
                                    : "invalid --present-buffer-mode request");
    }
    return 1;
}

int db_cli_try_parse(int argc, char **argv, db_cli_config_t *out_cfg,
                     int *out_show_help, int *out_print_usage, char *error,
                     size_t error_size) {
    if (out_show_help != NULL) {
        *out_show_help = 0;
    }
    if (out_print_usage != NULL) {
        *out_print_usage = 0;
    }
    if (out_cfg == NULL) {
        return db_cli_set_error(error, error_size, "output config is null");
    }

    g_cli_runtime_text_pool.used = 0U;
    db_runtime_options_reset_all();
    *out_cfg = (db_cli_config_t){
        .api = DB_API_OPENGL,
        .display = DB_DISPLAY_OFFSCREEN,
        .renderer = DB_GL_RENDERER_GL3_3,
        .kms_card = "/dev/dri/card0",
        .hash_mode = "none",
        .hash_report = "both",
        .fps_cap = BENCH_FPS_CAP,
        .frame_limit = 0U,
        .backbuffer_draw_full = 0,
        .backbuffer_draw_mode_explicit = 0,
        .present_buffer_mode_explicit = 0,
        .debug_clear_default_framebuffer = 0,
        .glfw_window_hidden = 0,
        .vsync_enabled = BENCH_DEFAULT_VSYNC_ENABLED,
        .api_is_auto = 1,
        .display_is_set = 0,
        .renderer_is_auto = 1,
    };

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (db_string_is(arg, "--help")) {
            if (out_show_help != NULL) {
                *out_show_help = 1;
            }
            return 1;
        }
        if (db_string_is(arg, "--api")) {
            const char *value = NULL;
            if (db_cli_try_expect_value(argc, argv, &i, &value, error,
                                        error_size) == 0) {
                return 0;
            }
            if (db_cli_try_parse_api(value, out_cfg, error, error_size) == 0) {
                return 0;
            }
            continue;
        }
        if (db_string_is(arg, "--renderer")) {
            const char *value = NULL;
            if (db_cli_try_expect_value(argc, argv, &i, &value, error,
                                        error_size) == 0) {
                return 0;
            }
            if (db_cli_try_parse_renderer(value, out_cfg, error, error_size) ==
                0) {
                return 0;
            }
            continue;
        }
        if (db_string_is(arg, "--display")) {
            const char *value = NULL;
            if (db_cli_try_expect_value(argc, argv, &i, &value, error,
                                        error_size) == 0) {
                return 0;
            }
            if (db_cli_try_parse_display(value, out_cfg, error, error_size) ==
                0) {
                return 0;
            }
            continue;
        }
        if (db_string_is(arg, "--kms-card")) {
            const char *value = NULL;
            if (db_cli_try_expect_value(argc, argv, &i, &value, error,
                                        error_size) == 0) {
                return 0;
            }
            out_cfg->kms_card = value;
            continue;
        }
        const int parsed_runtime = db_cli_try_parse_runtime_override_option(
            arg, argc, argv, &i, out_cfg, error, error_size);
        if (parsed_runtime < 0) {
            return 0;
        }
        if (parsed_runtime > 0) {
            continue;
        }
        if (out_print_usage != NULL) {
            *out_print_usage = 1;
        }
        return db_cli_set_error(error, error_size, "unknown option: %s", arg);
    }

    if (out_cfg->display_is_set == 0) {
        if (out_print_usage != NULL) {
            *out_print_usage = 1;
        }
        return db_cli_set_error(error, error_size,
                                "missing required option: --display "
                                "<offscreen|glfw_window|linux_kms_atomic>");
    }
    if (db_cli_validate_compiled_support(out_cfg, error, error_size) == 0) {
        return 0;
    }
    if (db_cli_validate_renderer_selection(out_cfg, error, error_size) == 0) {
        return 0;
    }
    if (db_cli_validate_glfw_hidden_window(out_cfg, error, error_size) == 0) {
        return 0;
    }
    if (db_cli_validate_present_buffer_mode(out_cfg, error, error_size) == 0) {
        return 0;
    }
    if (db_cli_validate_hash_mode(out_cfg, error, error_size) == 0) {
        return 0;
    }
    return 1;
}
