#include "driverbench_cli.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config/benchmark_config.h"
#include "core/db_core.h"
#include "displays/display_dispatch.h"
#include "renderers/renderer_benchmark_common.h"

static int db_string_is(const char *value, const char *expected) {
    return (value != NULL) && (expected != NULL) &&
           (strcmp(value, expected) == 0);
}

static void db_usage(void) {
#ifdef DB_HAS_OPENGL_DESKTOP
    const char *renderer_usage = "auto|gl1_5_gles1_1|gl3_3";
#else
    const char *renderer_usage = "auto|gl1_5_gles1_1";
#endif
    fputs("Usage: driverbench [dispatch options] [runtime options]\n"
          "\nDispatch options:\n"
          "  --api <auto|cpu|opengl|vulkan>\n"
          "  --renderer <",
          stderr);
    fputs(renderer_usage, stderr);
    fputs(">\n"
          "  --display <offscreen|glfw_window|linux_kms_atomic>  (required)\n"
          "  --kms-card <path>\n"
          "\nRuntime options:\n"
          "  --allow-remote-display <0|1>\n"
          "  --benchmark-mode "
          "<gradient_sweep|bands|snake_grid|gradient_fill|rect_snake>\n"
          "  --bench-speed <value>\n"
          "  --fps-cap <value>\n"
          "  --hash <none|state|pixel|both>\n"
          "  --frame-limit <value>\n"
          "  --hash-report <final|aggregate|both>\n"
          "  --offscreen <0|1>\n"
          "  --random-seed <value>\n"
          "  --vsync <0|1|on|off|true|false>\n"
          "  --help\n",
          stderr);
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
    if (db_string_is(value, DB_BENCHMARK_MODE_RECT_SNAKE)) {
        return DB_BENCHMARK_MODE_RECT_SNAKE;
    }
    return NULL;
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
    DB_CLI_RT_OFFSCREEN = 8,
    DB_CLI_RT_VSYNC = 9,
};

#define DB_CLI_RUNTIME_TEXT_LEN 64U
#define DB_CLI_RUNTIME_TEXT_SLOTS 32U

static struct {
    char slots[DB_CLI_RUNTIME_TEXT_SLOTS][DB_CLI_RUNTIME_TEXT_LEN];
    size_t used;
} g_cli_runtime_text_pool = {0};

static const char *db_cli_store_runtime_text_or_exit(const char *value) {
    if (g_cli_runtime_text_pool.used >= DB_CLI_RUNTIME_TEXT_SLOTS) {
        db_failf("driverbench_cli", "CLI runtime text pool exhausted");
    }
    char *slot = g_cli_runtime_text_pool.slots[g_cli_runtime_text_pool.used];
    const int written = db_snprintf(slot, DB_CLI_RUNTIME_TEXT_LEN, "%s", value);
    if ((written < 0) || ((size_t)written >= DB_CLI_RUNTIME_TEXT_LEN)) {
        db_failf("driverbench_cli", "CLI runtime value too long: %s", value);
    }
    g_cli_runtime_text_pool.used++;
    return slot;
}

static void db_cli_set_runtime_bool_or_exit(const char *runtime_option,
                                            const char *cli_option,
                                            const char *raw_value) {
    int parsed = 0;
    if (db_parse_bool_text(raw_value, &parsed) == 0) {
        db_failf("driverbench_cli", "invalid value for %s: %s (expected bool)",
                 cli_option, raw_value);
    }
    db_runtime_option_set(runtime_option, (parsed != 0) ? "1" : "0");
}

static uint32_t db_cli_parse_frame_limit_or_exit(const char *cli_option,
                                                 const char *raw_value) {
    char *end = NULL;
    const unsigned long parsed = strtoul(raw_value, &end, 10);
    if ((end == raw_value) || (end == NULL) || (*end != '\0') ||
        (parsed > UINT32_MAX)) {
        db_failf("driverbench_cli", "invalid value for %s: %s", cli_option,
                 raw_value);
    }
    return (uint32_t)parsed;
}

static void db_cli_set_runtime_random_seed_or_exit(const char *raw_value) {
    char *end = NULL;
    const unsigned long parsed = strtoul(raw_value, &end, 0);
    if ((end == raw_value) || (end == NULL) || (*end != '\0') ||
        (parsed > UINT32_MAX)) {
        db_failf("driverbench_cli", "invalid value for --random-seed: %s",
                 raw_value);
    }

    char normalized[32];
    (void)db_snprintf(normalized, sizeof(normalized), "%lu", parsed);
    db_runtime_option_set(DB_RUNTIME_OPT_RANDOM_SEED,
                          db_cli_store_runtime_text_or_exit(normalized));
}

static double db_cli_parse_fps_cap_or_exit(const char *raw_value) {
    double parsed = 0.0;
    if (db_parse_fps_cap_text(raw_value, &parsed) == 0) {
        db_failf("driverbench_cli", "invalid value for --fps-cap: %s",
                 raw_value);
    }
    if (parsed <= 0.0) {
        return 0.0;
    }
    return parsed;
}

static void db_cli_set_runtime_bench_speed_or_exit(const char *raw_value) {
    char *end = NULL;
    const double parsed = strtod(raw_value, &end);
    if ((end == raw_value) || (end == NULL) || (*end != '\0') ||
        !isfinite(parsed) || (parsed <= 0.0)) {
        db_failf("driverbench_cli", "invalid value for --bench-speed: %s",
                 raw_value);
    }
    if (parsed > (double)DB_BENCH_SPEED_STEP_MAX) {
        db_failf("driverbench_cli",
                 "invalid value for --bench-speed: %s (max: %u)", raw_value,
                 DB_BENCH_SPEED_STEP_MAX);
    }

    char normalized[32];
    (void)db_snprintf(normalized, sizeof(normalized), "%.9g", parsed);
    db_runtime_option_set(DB_RUNTIME_OPT_BENCH_SPEED,
                          db_cli_store_runtime_text_or_exit(normalized));
}

static void db_cli_set_runtime_mode_or_exit(const char *raw_value) {
    const char *normalized = db_cli_mode_normalized_or_null(raw_value);
    if (normalized == NULL) {
        db_failf("driverbench_cli",
                 "invalid value for --benchmark-mode: %s "
                 "(expected: %s|%s|%s|%s|%s)",
                 raw_value, DB_BENCHMARK_MODE_GRADIENT_SWEEP,
                 DB_BENCHMARK_MODE_BANDS, DB_BENCHMARK_MODE_SNAKE_GRID,
                 DB_BENCHMARK_MODE_GRADIENT_FILL, DB_BENCHMARK_MODE_RECT_SNAKE);
    }
    db_runtime_option_set(DB_RUNTIME_OPT_BENCHMARK_MODE, normalized);
}

static const char *db_cli_parse_hash_report_or_exit(const char *raw_value) {
    if (db_string_is(raw_value, "final") ||
        db_string_is(raw_value, "aggregate") ||
        db_string_is(raw_value, "both")) {
        return raw_value;
    }
    db_failf("driverbench_cli",
             "invalid value for --hash-report: %s "
             "(expected: final|aggregate|both)",
             raw_value);
    return "both";
}

static const char *db_cli_parse_hash_mode_or_exit(const char *raw_value) {
    if (db_string_is(raw_value, "none") || db_string_is(raw_value, "state") ||
        db_string_is(raw_value, "pixel") || db_string_is(raw_value, "both")) {
        return raw_value;
    }
    db_failf("driverbench_cli",
             "invalid value for --hash: %s "
             "(expected: none|state|pixel|both)",
             raw_value);
    return "none";
}

static const char *db_expect_value(int argc, char **argv, int *index) {
    if ((*index + 1) >= argc) {
        db_failf("driverbench_cli", "missing value for option: %s",
                 argv[*index]);
    }
    (*index)++;
    return argv[*index];
}

static void db_parse_api_or_exit(const char *value, db_cli_config_t *cfg) {
    if (db_string_is(value, "auto")) {
        cfg->api_is_auto = 1;
        return;
    }
    cfg->api_is_auto = 0;
    if (db_string_is(value, "cpu")) {
        cfg->api = DB_API_CPU;
        return;
    }
    if (db_string_is(value, "opengl")) {
        cfg->api = DB_API_OPENGL;
        return;
    }
    if (db_string_is(value, "vulkan")) {
        cfg->api = DB_API_VULKAN;
        return;
    }
    db_failf("driverbench_cli", "Unsupported api: %s", value);
}

static void db_parse_display_or_exit(const char *value, db_cli_config_t *cfg) {
    if (db_string_is(value, "offscreen")) {
        cfg->display = DB_DISPLAY_OFFSCREEN;
        cfg->display_is_set = 1;
        return;
    }
    if (db_string_is(value, "glfw_window")) {
        cfg->display = DB_DISPLAY_GLFW_WINDOW;
        cfg->display_is_set = 1;
        return;
    }
    if (db_string_is(value, "linux_kms_atomic")) {
        cfg->display = DB_DISPLAY_LINUX_KMS_ATOMIC;
        cfg->display_is_set = 1;
        return;
    }
    db_failf("driverbench_cli", "Unsupported display: %s", value);
}

static void db_parse_renderer_or_exit(const char *value, db_cli_config_t *cfg) {
    if (db_string_is(value, "auto")) {
        cfg->renderer_is_auto = 1;
        return;
    }
    cfg->renderer_is_auto = 0;
    if (db_string_is(value, "gl1_5_gles1_1")) {
        cfg->renderer = DB_GL_RENDERER_GL1_5_GLES1_1;
        return;
    }
    if (db_string_is(value, "gl3_3")) {
#ifndef DB_HAS_OPENGL_DESKTOP
        db_failf("driverbench_cli",
                 "renderer gl3_3 is not compiled in this build");
#endif
        cfg->renderer = DB_GL_RENDERER_GL3_3;
        return;
    }
    db_failf("driverbench_cli", "Unsupported renderer: %s", value);
}

static int db_try_parse_runtime_override_option(const char *arg, int argc,
                                                char **argv, int *index,
                                                db_cli_config_t *cfg) {
    static const db_cli_runtime_option_map_t mappings[] = {
        {"--allow-remote-display", DB_RUNTIME_OPT_ALLOW_REMOTE_DISPLAY,
         DB_CLI_RT_BOOL},
        {"--bench-speed", DB_RUNTIME_OPT_BENCH_SPEED, DB_CLI_RT_BENCH_SPEED},
        {"--benchmark-mode", DB_RUNTIME_OPT_BENCHMARK_MODE, DB_CLI_RT_MODE},
        {"--fps-cap", DB_RUNTIME_OPT_FPS_CAP, DB_CLI_RT_FPS_CAP},
        {"--hash", DB_RUNTIME_OPT_HASH, DB_CLI_RT_HASH_MODE},
        {"--frame-limit", DB_RUNTIME_OPT_FRAME_LIMIT, DB_CLI_RT_FRAME_LIMIT},
        {"--hash-report", DB_RUNTIME_OPT_HASH_REPORT, DB_CLI_RT_HASH_REPORT},
        {"--offscreen", DB_RUNTIME_OPT_OFFSCREEN, DB_CLI_RT_OFFSCREEN},
        {"--random-seed", DB_RUNTIME_OPT_RANDOM_SEED, DB_CLI_RT_RANDOM_SEED},
        {"--vsync", DB_RUNTIME_OPT_VSYNC, DB_CLI_RT_VSYNC},
    };

    for (size_t map_index = 0;
         map_index < (sizeof(mappings) / sizeof(mappings[0])); map_index++) {
        if (db_string_is(arg, mappings[map_index].cli_option)) {
            const char *value = db_expect_value(argc, argv, index);
            if (mappings[map_index].kind == DB_CLI_RT_BOOL) {
                db_cli_set_runtime_bool_or_exit(
                    mappings[map_index].runtime_option,
                    mappings[map_index].cli_option, value);
            } else if (mappings[map_index].kind == DB_CLI_RT_FRAME_LIMIT) {
                cfg->frame_limit = db_cli_parse_frame_limit_or_exit(
                    mappings[map_index].cli_option, value);
            } else if (mappings[map_index].kind == DB_CLI_RT_MODE) {
                db_cli_set_runtime_mode_or_exit(value);
            } else if (mappings[map_index].kind == DB_CLI_RT_FPS_CAP) {
                cfg->fps_cap = db_cli_parse_fps_cap_or_exit(value);
            } else if (mappings[map_index].kind == DB_CLI_RT_RANDOM_SEED) {
                db_cli_set_runtime_random_seed_or_exit(value);
            } else if (mappings[map_index].kind == DB_CLI_RT_HASH_REPORT) {
                cfg->hash_report = db_cli_parse_hash_report_or_exit(value);
            } else if (mappings[map_index].kind == DB_CLI_RT_HASH_MODE) {
                cfg->hash_mode = db_cli_parse_hash_mode_or_exit(value);
            } else if (mappings[map_index].kind == DB_CLI_RT_BENCH_SPEED) {
                db_cli_set_runtime_bench_speed_or_exit(value);
            } else if (mappings[map_index].kind == DB_CLI_RT_OFFSCREEN) {
                int parsed = 0;
                if (db_parse_bool_text(value, &parsed) == 0) {
                    db_failf("driverbench_cli",
                             "invalid value for --offscreen: %s "
                             "(expected bool)",
                             value);
                }
                cfg->offscreen_enabled = (parsed != 0);
            } else if (mappings[map_index].kind == DB_CLI_RT_VSYNC) {
                int parsed = 0;
                if (db_parse_bool_text(value, &parsed) == 0) {
                    db_failf("driverbench_cli",
                             "invalid value for --vsync: %s (expected bool)",
                             value);
                }
                cfg->vsync_enabled = (parsed != 0);
            }
            return 1;
        }
    }

    return 0;
}

static void
db_cli_validate_compiled_support_or_exit(const db_cli_config_t *cfg) {
    if (cfg == NULL) {
        db_failf("driverbench_cli", "config is null");
    }

    if (db_dispatch_display_is_compiled(cfg->display) == 0) {
        db_failf("driverbench_cli",
                 "requested display is not compiled in this build");
    }

    if (cfg->api_is_auto != 0) {
        if (db_dispatch_display_has_any_api(cfg->display) == 0) {
            db_failf("driverbench_cli",
                     "requested display has no compatible compiled API");
        }
        return;
    }

    if (db_dispatch_api_is_compiled(cfg->api) == 0) {
        db_failf("driverbench_cli",
                 "requested API is not compiled in this build");
    }

    if (db_dispatch_display_supports_api(cfg->display, cfg->api) == 0) {
        db_failf("driverbench_cli",
                 "requested display/API combination is unavailable in this "
                 "build");
    }
    if ((cfg->api == DB_API_OPENGL) && (cfg->renderer_is_auto == 0) &&
        (db_dispatch_renderer_is_compiled(cfg->renderer) == 0)) {
        db_failf("driverbench_cli",
                 "requested OpenGL renderer is not compiled in this build");
    }
}

static db_api_t
db_cli_resolve_effective_api_or_exit(const db_cli_config_t *cfg) {
    if (cfg->api_is_auto == 0) {
        return cfg->api;
    }
    if (db_dispatch_display_supports_api(cfg->display, DB_API_VULKAN) != 0) {
        return DB_API_VULKAN;
    }
    if (db_dispatch_display_supports_api(cfg->display, DB_API_OPENGL) != 0) {
        return DB_API_OPENGL;
    }
    if (db_dispatch_display_supports_api(cfg->display, DB_API_CPU) != 0) {
        return DB_API_CPU;
    }
    db_failf("driverbench_cli",
             "requested display has no compatible compiled API");
    return DB_API_CPU;
}

static int db_cli_hash_mode_requests_state(const char *hash_mode) {
    return db_string_is(hash_mode, "state") || db_string_is(hash_mode, "both");
}

static int db_cli_hash_mode_requests_pixel(const char *hash_mode) {
    return db_string_is(hash_mode, "pixel") || db_string_is(hash_mode, "both");
}

static void db_cli_validate_hash_mode_or_exit(const db_cli_config_t *cfg) {
    const char *hash_mode = cfg->hash_mode;
    if ((hash_mode == NULL) || (hash_mode[0] == '\0') ||
        db_string_is(hash_mode, "none")) {
        return;
    }

    const db_api_t api = db_cli_resolve_effective_api_or_exit(cfg);
    const int needs_state = db_cli_hash_mode_requests_state(hash_mode);
    const int needs_pixel = db_cli_hash_mode_requests_pixel(hash_mode);

    int supports_state = 0;
    int supports_pixel = 0;
    if ((cfg->display == DB_DISPLAY_GLFW_WINDOW) ||
        (cfg->display == DB_DISPLAY_OFFSCREEN)) {
        if (api == DB_API_VULKAN) {
            supports_state = 1;
            supports_pixel = 0;
        } else if ((api == DB_API_OPENGL) || (api == DB_API_CPU)) {
            supports_state = 1;
            supports_pixel = 1;
        }
    } else if (cfg->display == DB_DISPLAY_LINUX_KMS_ATOMIC) {
        supports_state = 0;
        supports_pixel = 0;
    }

    if ((needs_state != 0) && (supports_state == 0)) {
        db_failf("driverbench_cli",
                 "hash mode '%s' is unsupported for display/API combination "
                 "(display=%d api=%d): state hash unavailable",
                 hash_mode, (int)cfg->display, (int)api);
    }
    if ((needs_pixel != 0) && (supports_pixel == 0)) {
        db_failf("driverbench_cli",
                 "hash mode '%s' is unsupported for display/API combination "
                 "(display=%d api=%d): pixel hash unavailable",
                 hash_mode, (int)cfg->display, (int)api);
    }
}

void db_cli_parse_or_exit(int argc, char **argv, db_cli_config_t *out_cfg) {
    if (out_cfg == NULL) {
        db_failf("driverbench_cli", "output config is null");
    }
    g_cli_runtime_text_pool.used = 0U;
    *out_cfg = (db_cli_config_t){
        .api = DB_API_OPENGL,
        .display = DB_DISPLAY_OFFSCREEN,
#ifdef DB_HAS_OPENGL_DESKTOP
        .renderer = DB_GL_RENDERER_GL3_3,
#else
        .renderer = DB_GL_RENDERER_GL1_5_GLES1_1,
#endif
        .kms_card = "/dev/dri/card0",
        .hash_mode = "none",
        .hash_report = "both",
        .fps_cap = BENCH_FPS_CAP_D,
        .frame_limit = 0U,
        .offscreen_enabled = 0,
        .vsync_enabled = (BENCH_GLFW_SWAP_INTERVAL != 0),
        .api_is_auto = 1,
        .display_is_set = 0,
        .renderer_is_auto = 1,
    };

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (db_string_is(arg, "--help")) {
            db_usage();
            exit(EXIT_SUCCESS);
        }
        if (db_string_is(arg, "--api")) {
            db_parse_api_or_exit(db_expect_value(argc, argv, &i), out_cfg);
            continue;
        }
        if (db_string_is(arg, "--renderer")) {
            db_parse_renderer_or_exit(db_expect_value(argc, argv, &i), out_cfg);
            continue;
        }
        if (db_string_is(arg, "--display")) {
            db_parse_display_or_exit(db_expect_value(argc, argv, &i), out_cfg);
            continue;
        }
        if (db_string_is(arg, "--kms-card")) {
            out_cfg->kms_card = db_expect_value(argc, argv, &i);
            continue;
        }
        if (db_try_parse_runtime_override_option(arg, argc, argv, &i,
                                                 out_cfg) != 0) {
            continue;
        }

        db_usage();
        db_failf("driverbench_cli", "unknown option: %s", arg);
    }

    if (out_cfg->display_is_set == 0) {
        db_usage();
        db_failf("driverbench_cli", "missing required option: --display "
                                    "<offscreen|glfw_window|linux_kms_atomic>");
    }

    db_cli_validate_compiled_support_or_exit(out_cfg);
    db_cli_validate_hash_mode_or_exit(out_cfg);
}
