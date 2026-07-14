#include "cli_runtime_options.h"

#include "cli_validation.h"

#include <stdint.h>
#include <string.h>

#include "benchmarks/db_benchmark_types_internal.h"
#include "config/runtime_options.h"
#include "core/db_core.h"
#include "core/db_numeric.h"
#include "driverbench_config.h"

static int db_string_is(const char *value, const char *expected) {
    return (value != NULL) && (expected != NULL) &&
           (strcmp(value, expected) == 0);
}

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
    DB_CLI_RT_GL1_TARGET = 23,
    DB_CLI_RT_GL1_GRADIENT = 24,
    DB_CLI_RT_GL1_REPLAY_CAPACITY = 25,
    DB_CLI_RT_GL3_GRADIENT = 26,
    DB_CLI_RT_VK_GRADIENT = 27,
    DB_CLI_RT_PATH = 28,
};

#define DB_CLI_RUNTIME_TEXT_LEN 64U
#define DB_CLI_RUNTIME_TEXT_SLOTS 32U

static struct {
    char slots[DB_CLI_RUNTIME_TEXT_SLOTS][DB_CLI_RUNTIME_TEXT_LEN];
    size_t used;
} g_cli_runtime_text_pool = {0};

void db_cli_runtime_options_begin_parse(void) {
    g_cli_runtime_text_pool.used = 0U;
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

int db_cli_try_expect_value(size_t argc, const char *const *argv, size_t *index,
                            const char **out_value, char *error,
                            size_t error_size) {
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
    const char *end = NULL;
    uint32_t parsed = 0U;
    if ((out_value == NULL) ||
        (db_parse_u32_prefix(raw_value, DB_PARSE_BASE_DECIMAL, &parsed, &end) ==
         0) ||
        (*end != '\0')) {
        return db_cli_validation_set_error(error, error_size,
                                           "invalid value for %s: %s",
                                           cli_option, raw_value);
    }
    *out_value = parsed;
    return 1;
}

static int db_cli_try_parse_random_seed(const char *raw_value, char *error,
                                        size_t error_size) {
    const char *end = NULL;
    uint32_t parsed = 0U;
    if ((db_parse_u32_prefix(raw_value, DB_PARSE_BASE_AUTODETECT, &parsed,
                             &end) == 0) ||
        (*end != '\0')) {
        return db_cli_validation_set_error(
            error, error_size, "invalid value for --random-seed: %s",
            raw_value);
    }

    char normalized[32];
    (void)db_snprintf(normalized, sizeof(normalized), "%u", parsed);
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
    *out_value = db_max_f64(parsed, 0.0);
    return 1;
}

static int db_cli_try_parse_bench_speed(const char *raw_value, char *error,
                                        size_t error_size) {
    if (raw_value == NULL) {
        return db_cli_validation_set_error(
            error, error_size, "invalid value for --bench-speed: (null)");
    }
    const char *end = NULL;
    double parsed = 0.0;
    if ((db_parse_double_prefix(raw_value, &parsed, &end) == 0) ||
        (*end != '\0') || (parsed <= 0.0)) {
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
    if (cfg == NULL) {
        return db_cli_validation_set_error(
            error, error_size,
            "internal error while parsing --backbuffer-draw-mode");
    }
    cfg->backbuffer_draw_mode_explicit = 1;
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

int db_cli_try_parse_runtime_override_option(const char *arg, size_t argc,
                                             const char *const *argv,
                                             size_t *index,
                                             db_cli_config_t *cfg, char *error,
                                             size_t error_size) {
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
        {"--gl1-gradient", DB_RUNTIME_OPT_GL1_GRADIENT, DB_CLI_RT_GL1_GRADIENT},
        {"--gl1-replay-capacity", DB_RUNTIME_OPT_GL1_REPLAY_CAPACITY,
         DB_CLI_RT_GL1_REPLAY_CAPACITY},
        {"--gl1-target", DB_RUNTIME_OPT_GL1_TARGET, DB_CLI_RT_GL1_TARGET},
        {"--gl3-gradient", DB_RUNTIME_OPT_GL3_GRADIENT, DB_CLI_RT_GL3_GRADIENT},
        {"--hash", DB_RUNTIME_OPT_HASH, DB_CLI_RT_HASH_MODE},
        {"--frame-limit", DB_RUNTIME_OPT_FRAME_LIMIT, DB_CLI_RT_FRAME_LIMIT},
        {"--glfw-hidden-window", NULL, DB_CLI_RT_GLFW_HIDDEN_WINDOW},
        {"--hash-report", DB_RUNTIME_OPT_HASH_REPORT, DB_CLI_RT_HASH_REPORT},
        {"--ignore-conformance-cache", DB_RUNTIME_OPT_IGNORE_CONFORMANCE_CACHE,
         DB_CLI_RT_BOOL},
        {"--metrics-mode", DB_RUNTIME_OPT_METRICS_MODE, DB_CLI_RT_METRICS_MODE},
        {"--present-buffer-mode", DB_RUNTIME_OPT_PRESENT_BUFFER_MODE,
         DB_CLI_RT_PRESENT_BUFFER_MODE},
        {"--random-seed", DB_RUNTIME_OPT_RANDOM_SEED, DB_CLI_RT_RANDOM_SEED},
        {"--resize-at-frame", DB_RUNTIME_OPT_RESIZE_AT_FRAME,
         DB_CLI_RT_RESIZE_AT_FRAME},
        {"--rerun-conformance-probe", DB_RUNTIME_OPT_RERUN_CONFORMANCE_PROBE,
         DB_CLI_RT_BOOL},
        {"--dump-gradient-divergence", DB_RUNTIME_OPT_DUMP_GRADIENT_DIVERGENCE,
         DB_CLI_RT_PATH},
        {"--vk-allow-cpu-workers", DB_RUNTIME_OPT_VK_ALLOW_CPU_WORKERS,
         DB_CLI_RT_VK_ALLOW_CPU_WORKERS},
        {"--vk-multi-device-policy", DB_RUNTIME_OPT_VK_MULTI_DEVICE_POLICY,
         DB_CLI_RT_VK_MULTI_DEVICE_POLICY},
        {"--vk-no-present", DB_RUNTIME_OPT_VK_NO_PRESENT,
         DB_CLI_RT_VK_NO_PRESENT},
        {"--vk-gradient", DB_RUNTIME_OPT_VK_GRADIENT, DB_CLI_RT_VK_GRADIENT},
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
        if (value == NULL) {
            (void)db_cli_validation_set_error(error, error_size,
                                              "Missing value for %s",
                                              mappings[map_index].cli_option);
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
        case DB_CLI_RT_GL1_TARGET:
            if (!db_string_is(value, "auto") &&
                !db_string_is(value, "direct-window") &&
                !db_string_is(value, "persistent-fbo") &&
                !db_string_is(value, "cpu-upload")) {
                (void)db_cli_validation_set_error(
                    error, error_size,
                    "Invalid GL1 target: %s (expected "
                    "auto|direct-window|persistent-fbo|cpu-upload)",
                    value);
                return -1;
            }
            db_runtime_option_set(mappings[map_index].runtime_option, value);
            break;
        case DB_CLI_RT_GL1_GRADIENT:
            if (!db_string_is(value, "auto") &&
                !db_string_is(value, "interpolated") &&
                !db_string_is(value, "row-fill") &&
                !db_string_is(value, "cpu")) {
                (void)db_cli_validation_set_error(
                    error, error_size,
                    "Invalid GL1 gradient: %s (expected "
                    "auto|interpolated|row-fill|cpu)",
                    value);
                return -1;
            }
            db_runtime_option_set(mappings[map_index].runtime_option, value);
            break;
        case DB_CLI_RT_GL3_GRADIENT:
        case DB_CLI_RT_VK_GRADIENT:
            if (!db_string_is(value, "auto") &&
                !db_string_is(value, "semantic") &&
                !db_string_is(value, "row-fill")) {
                (void)db_cli_validation_set_error(
                    error, error_size,
                    "Invalid gradient strategy for %s: %s "
                    "(expected auto|semantic|row-fill)",
                    mappings[map_index].cli_option, value);
                return -1;
            }
            db_runtime_option_set(mappings[map_index].runtime_option, value);
            break;
        case DB_CLI_RT_GL1_REPLAY_CAPACITY: {
            int capacity = 0;
            if ((db_parse_int_text(value, &capacity) == 0) || (capacity < 1) ||
                (capacity > 8)) {
                (void)db_cli_validation_set_error(
                    error, error_size,
                    "Invalid GL1 replay capacity: %s (expected 1..8)", value);
                return -1;
            }
            db_runtime_option_set(mappings[map_index].runtime_option, value);
            break;
        }
        case DB_CLI_RT_PATH:
            if (value[0] == '\0') {
                (void)db_cli_validation_set_error(
                    error, error_size, "Empty path for %s",
                    mappings[map_index].cli_option);
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
