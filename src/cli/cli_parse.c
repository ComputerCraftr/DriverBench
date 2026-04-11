#include "cli/cli_parse.h"
#include "cli/cli_validation.h"

#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "benchmarks/db_benchmark_types_internal.h"
#include "config/benchmark_config.h"
#include "config/runtime_options.h"
#include "core/db_core.h"
#include "displays/display_types.h"
#include "driverbench_config.h"

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
    DB_CLI_RT_WORKING_FORMAT = 12,
    DB_CLI_RT_VK_NO_PRESENT = 13,
    DB_CLI_RT_METRICS_MODE = 14,
    DB_CLI_RT_VK_ALLOW_CPU_WORKERS = 15,
    DB_CLI_RT_VK_MULTI_DEVICE_POLICY = 16,
    DB_CLI_RT_PRESENT_BUFFER_MODE = 17,
    DB_CLI_RT_TRACE_LEVEL = 18,
    DB_CLI_RT_TRACE_BOOL = 19,
    DB_CLI_RT_OUTPUT_FORMAT = 20,
    DB_CLI_RT_RESIZE_AT_FRAME = 21,
    DB_CLI_RT_TRACE_VULKAN = 22,
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

int db_cli_validation_set_error(char *error, size_t error_size, const char *fmt,
                                ...) {
    if ((error == NULL) || (error_size == 0U)) {
        return 0;
    }
    va_list args;
    va_start(args, fmt);
    (void)db_vsnprintf(error, error_size, fmt, args);
    va_end(args);
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
        return db_cli_validation_set_error(
            error, error_size, "invalid value for %s: %s (expected bool)",
            cli_option, raw_value);
    }
    *out_value = parsed;
    return 1;
}

static int db_cli_try_expect_value(size_t argc, const char *const *argv,
                                   size_t *index, const char **out_value,
                                   char *error, size_t error_size) {
    if (((*index) + 1) >= argc) {
        return db_cli_validation_set_error(
            error, error_size, "missing value for option: %s", argv[*index]);
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
        return db_cli_validation_set_error(error, error_size,
                                           "invalid value for %s: %s",
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
        return db_cli_validation_set_error(
            error, error_size, "invalid value for --random-seed: %s",
            raw_value);
    }

    char normalized[32];
    (void)db_snprintf(normalized, sizeof(normalized), "%lu", parsed);
    const char *stored = db_cli_store_runtime_text_or_null(normalized);
    if (stored == NULL) {
        return db_cli_validation_set_error(
            error, error_size, "CLI runtime value too long: %s", raw_value);
    }
    db_runtime_option_set(DB_RUNTIME_OPT_RANDOM_SEED, stored);
    return 1;
}

static int db_cli_try_parse_fps_cap(const char *raw_value, double *out_value,
                                    char *error, size_t error_size) {
    double parsed = 0.0;
    if ((out_value == NULL) ||
        (db_parse_fps_cap_text(raw_value, &parsed) == 0)) {
        return db_cli_validation_set_error(
            error, error_size, "invalid value for --fps-cap: %s", raw_value);
    }
    *out_value = (parsed <= 0.0) ? 0.0 : parsed;
    return 1;
}

static int db_cli_try_parse_bench_speed(const char *raw_value, char *error,
                                        size_t error_size) {
    if (raw_value == NULL) {
        return db_cli_validation_set_error(
            error, error_size, "invalid value for --bench-speed: (null)");
    }
    char *end = NULL;
    const double parsed = strtod(raw_value, &end);
    if ((end == raw_value) || (end == NULL) || (*end != '\0') ||
        !isfinite(parsed) || (parsed <= 0.0)) {
        return db_cli_validation_set_error(
            error, error_size, "invalid value for --bench-speed: %s",
            raw_value);
    }
    if (parsed > DB_BENCH_SPEED_STEP_MAX) {
        return db_cli_validation_set_error(
            error, error_size, "invalid value for --bench-speed: %s (max: %u)",
            raw_value, DB_BENCH_SPEED_STEP_MAX);
    }
    char normalized[32];
    (void)db_snprintf(normalized, sizeof(normalized), "%.9g", parsed);
    const char *stored = db_cli_store_runtime_text_or_null(normalized);
    if (stored == NULL) {
        return db_cli_validation_set_error(
            error, error_size, "CLI runtime value too long: %s", raw_value);
    }
    db_runtime_option_set(DB_RUNTIME_OPT_BENCH_SPEED, stored);
    return 1;
}

static int db_cli_try_parse_runtime_mode(const char *raw_value, char *error,
                                         size_t error_size) {
    const char *normalized = db_cli_mode_normalized_or_null(raw_value);
    if (normalized == NULL) {
        return db_cli_validation_set_error(
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
    return db_cli_validation_set_error(
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
    return db_cli_validation_set_error(
        error, error_size,
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
    return db_cli_validation_set_error(
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
    return db_cli_validation_set_error(
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
    return db_cli_validation_set_error(
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
    return db_cli_validation_set_error(
        error, error_size,
        "invalid value for --vk-multi-device-policy: %s "
        "(expected: auto|group_only|independent_ok)",
        raw_value);
}

static int db_cli_try_parse_api(const char *value, db_cli_config_t *cfg,
                                char *error, size_t error_size) {
    if ((cfg == NULL) || (value == NULL)) {
        return db_cli_validation_set_error(error, error_size, "config is null");
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
    return db_cli_validation_set_error(error, error_size, "Unsupported api: %s",
                                       value);
}

static int db_cli_try_parse_display(const char *value, db_cli_config_t *cfg,
                                    char *error, size_t error_size) {
    if ((cfg == NULL) || (value == NULL)) {
        return db_cli_validation_set_error(error, error_size, "config is null");
    }
    if (db_string_is(value, "offscreen")) {
        cfg->display = DB_OFFSCREEN_DISPLAY;
        cfg->display_is_set = 1;
        return 1;
    }
    if (db_string_is(value, "glfw_window")) {
        cfg->display = DB_GLFW_WINDOW_DISPLAY;
        cfg->display_is_set = 1;
        return 1;
    }
    if (db_string_is(value, "linux_kms_atomic")) {
        cfg->display = DB_KMS_DISPLAY;
        cfg->display_is_set = 1;
        return 1;
    }
    return db_cli_validation_set_error(error, error_size,
                                       "Unsupported display: %s", value);
}

static int db_cli_try_parse_renderer(const char *value, db_cli_config_t *cfg,
                                     char *error, size_t error_size) {
    if ((cfg == NULL) || (value == NULL)) {
        return db_cli_validation_set_error(error, error_size, "config is null");
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
    return db_cli_validation_set_error(error, error_size,
                                       "Unsupported renderer: %s", value);
}

static int db_cli_try_parse_runtime_override_option(
    const char *arg, size_t argc, const char *const *argv, size_t *index,
    db_cli_config_t *cfg, char *error, size_t error_size) {
    static const db_cli_runtime_option_map_t mappings[] = {
        {"--allow-remote-display", DB_RUNTIME_OPT_ALLOW_REMOTE_DISPLAY,
         DB_CLI_RT_BOOL},
        {"--bench-speed", DB_RUNTIME_OPT_BENCH_SPEED, DB_CLI_RT_BENCH_SPEED},
        {"--backbuffer-draw-mode", DB_RUNTIME_OPT_BACKBUFFER_DRAW_MODE,
         DB_CLI_RT_BACKBUFFER_DRAW_MODE},
        {"--benchmark-mode", DB_RUNTIME_OPT_BENCHMARK_MODE, DB_CLI_RT_MODE},
        {"--output-format", DB_RUNTIME_OPT_OUTPUT_FORMAT,
         DB_CLI_RT_OUTPUT_FORMAT},
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
        {"--resize-at-frame", DB_RUNTIME_OPT_RESIZE_AT_FRAME,
         DB_CLI_RT_RESIZE_AT_FRAME},
        {"--vk-allow-cpu-workers", DB_RUNTIME_OPT_VK_ALLOW_CPU_WORKERS,
         DB_CLI_RT_VK_ALLOW_CPU_WORKERS},
        {"--vk-multi-device-policy", DB_RUNTIME_OPT_VK_MULTI_DEVICE_POLICY,
         DB_CLI_RT_VK_MULTI_DEVICE_POLICY},
        {"--vk-no-present", DB_RUNTIME_OPT_VK_NO_PRESENT,
         DB_CLI_RT_VK_NO_PRESENT},
        {"--trace-damage", DB_RUNTIME_OPT_TRACE_DAMAGE, DB_CLI_RT_TRACE_LEVEL},
        {"--trace-gl-errors", DB_RUNTIME_OPT_TRACE_GL_ERRORS,
         DB_CLI_RT_TRACE_BOOL},
        {"--trace-shadow-upload", DB_RUNTIME_OPT_TRACE_SHADOW_UPLOAD,
         DB_CLI_RT_TRACE_LEVEL},
        {"--trace-vulkan", DB_RUNTIME_OPT_TRACE_VULKAN, DB_CLI_RT_TRACE_VULKAN},
        {"--vsync", DB_RUNTIME_OPT_VSYNC, DB_CLI_RT_VSYNC},
        {"--working-format", DB_RUNTIME_OPT_WORKING_FORMAT,
         DB_CLI_RT_WORKING_FORMAT},
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
        case DB_CLI_RT_WORKING_FORMAT:
            if (!db_string_is(value, "rgba8") &&
                !db_string_is(value, "rgba16f")) {
                (void)db_cli_validation_set_error(error, error_size,
                                                  "Invalid working format: %s "
                                                  "(expected rgba8|rgba16f)",
                                                  value);
                return -1;
            }
            db_runtime_option_set(mappings[map_index].runtime_option, value);
            break;
        case DB_CLI_RT_OUTPUT_FORMAT:
            if (!db_string_is(value, "auto") && !db_string_is(value, "sdr") &&
                !db_string_is(value, "hdr")) {
                (void)db_cli_validation_set_error(error, error_size,
                                                  "Invalid output format: %s "
                                                  "(expected auto|sdr|hdr)",
                                                  value);
                return -1;
            }
            db_runtime_option_set(mappings[map_index].runtime_option, value);
            break;
        case DB_CLI_RT_RESIZE_AT_FRAME: {
            db_resize_schedule_t resize = {0};
            if (db_resize_schedule_parse(value, &resize) == 0) {
                (void)db_cli_validation_set_error(
                    error, error_size,
                    "Invalid resize schedule: %s (expected FRAME:WIDTHxHEIGHT)",
                    value);
                return -1;
            }
            db_runtime_option_set(mappings[map_index].runtime_option, value);
            break;
        }
        case DB_CLI_RT_TRACE_LEVEL:
        case DB_CLI_RT_TRACE_VULKAN:
        case DB_CLI_RT_TRACE_BOOL: {
            int parsed = 0;
            int max_level = 1;
            switch ((int)mappings[map_index].kind) {
            case DB_CLI_RT_TRACE_LEVEL:
                max_level = 3;
                break;
            case DB_CLI_RT_TRACE_VULKAN:
                max_level = 2;
                break;
            default:
                break;
            }
            if ((db_parse_int_text(value, &parsed) == 0) || (parsed < 0) ||
                (parsed > max_level)) {
                (void)db_cli_validation_set_error(
                    error, error_size,
                    "Invalid trace level for %s: %s "
                    "(expected 0..%d)",
                    mappings[map_index].cli_option, value, max_level);
                return -1;
            }
            db_runtime_option_set(mappings[map_index].runtime_option, value);
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
            return db_cli_validation_set_error(
                       error, error_size, "Unhandled runtime option kind: %d",
                       mappings[map_index].kind)
                       ? 1
                       : -1;
        }
        return 1;
    }
    return 0;
}

int db_cli_try_parse(size_t argc, const char *const *argv,
                     db_cli_config_t *out_cfg, int *out_show_help,
                     int *out_print_usage, char *error, size_t error_size) {
    if (out_show_help != NULL) {
        *out_show_help = 0;
    }
    if (out_print_usage != NULL) {
        *out_print_usage = 0;
    }
    if (out_cfg == NULL) {
        return db_cli_validation_set_error(error, error_size,
                                           "output config is null");
    }

    g_cli_runtime_text_pool.used = 0U;
    db_runtime_options_reset_all();
    *out_cfg = (db_cli_config_t){
        .api = DB_API_OPENGL,
        .display = DB_OFFSCREEN_DISPLAY,
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

    for (size_t i = 1U; i < argc; i++) {
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
        if (parsed_runtime != 0) {
            continue;
        }
        if (out_print_usage != NULL) {
            *out_print_usage = 1;
        }
        return db_cli_validation_set_error(error, error_size,
                                           "unknown option: %s", arg);
    }

    if (out_cfg->display_is_set == 0) {
        if (out_print_usage != NULL) {
            *out_print_usage = 1;
        }
        return db_cli_validation_set_error(
            error, error_size,
            "missing required option: --display "
            "<offscreen|glfw_window|linux_kms_atomic>");
    }
    return db_cli_validate_config(out_cfg, error, error_size);
}
