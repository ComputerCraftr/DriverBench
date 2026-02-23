#include "driverbench_cli.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/db_core.h"
#include "displays/display_dispatch.h"
#include "renderers/renderer_benchmark_common.h"

static int db_string_is(const char *value, const char *expected) {
    return (value != NULL) && (expected != NULL) &&
           (strcmp(value, expected) == 0);
}

static void db_usage(void) {
    fputs("Usage: driverbench [dispatch options] [runtime options]\n"
          "\nDispatch options:\n"
          "  --api <auto|cpu|opengl|vulkan>\n"
          "  --renderer <auto|gl1_5_gles1_1|gl3_3>\n"
          "  --display <offscreen|glfw_window|linux_kms_atomic>  (required)\n"
          "  --kms-card <path>\n"
          "\nRuntime options:\n"
          "  --allow-remote-display <0|1>\n"
          "  --benchmark-mode "
          "<gradient_sweep|bands|snake_grid|gradient_fill|rect_snake>\n"
          "  --fps-cap <value>\n"
          "  --framebuffer-hash <0|1>\n"
          "  --frame-limit <value>\n"
          "  --hash-every-frame <0|1>\n"
          "  --offscreen <0|1>\n"
          "  --offscreen-frames <value>\n"
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
    DB_CLI_RT_U32_NONNEG = 4,
    DB_CLI_RT_U32_POSITIVE = 5,
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

static void db_cli_set_runtime_u32_or_exit(const char *runtime_option,
                                           const char *cli_option,
                                           const char *raw_value,
                                           int allow_zero) {
    char *end = NULL;
    const unsigned long parsed = strtoul(raw_value, &end, 10);
    if ((end == raw_value) || (end == NULL) || (*end != '\0') ||
        (parsed > UINT32_MAX) || ((allow_zero == 0) && (parsed == 0UL))) {
        db_failf("driverbench_cli", "invalid value for %s: %s", cli_option,
                 raw_value);
    }

    char normalized[32];
    (void)db_snprintf(normalized, sizeof(normalized), "%lu", parsed);
    db_runtime_option_set(runtime_option,
                          db_cli_store_runtime_text_or_exit(normalized));
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
    db_runtime_option_set(DB_RANDOM_SEED_ENV,
                          db_cli_store_runtime_text_or_exit(normalized));
}

static void db_cli_set_runtime_fps_cap_or_exit(const char *raw_value) {
    double parsed = 0.0;
    if (db_parse_fps_cap_text(raw_value, &parsed) == 0) {
        db_failf("driverbench_cli", "invalid value for --fps-cap: %s",
                 raw_value);
    }
    if (parsed <= 0.0) {
        db_runtime_option_set(DB_RUNTIME_OPT_FPS_CAP, "0");
        return;
    }

    char normalized[32];
    (void)db_snprintf(normalized, sizeof(normalized), "%.9g", parsed);
    db_runtime_option_set(DB_RUNTIME_OPT_FPS_CAP,
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
    db_runtime_option_set(DB_BENCHMARK_MODE_ENV, normalized);
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
        cfg->renderer = DB_GL_RENDERER_GL3_3;
        return;
    }
    db_failf("driverbench_cli", "Unsupported renderer: %s", value);
}

static int db_try_parse_runtime_override_option(const char *arg, int argc,
                                                char **argv, int *index) {
    static const db_cli_runtime_option_map_t mappings[] = {
        {"--allow-remote-display", DB_RUNTIME_OPT_ALLOW_REMOTE_DISPLAY,
         DB_CLI_RT_BOOL},
        {"--benchmark-mode", DB_BENCHMARK_MODE_ENV, DB_CLI_RT_MODE},
        {"--fps-cap", DB_RUNTIME_OPT_FPS_CAP, DB_CLI_RT_FPS_CAP},
        {"--framebuffer-hash", DB_RUNTIME_OPT_FRAMEBUFFER_HASH, DB_CLI_RT_BOOL},
        {"--frame-limit", DB_RUNTIME_OPT_FRAME_LIMIT, DB_CLI_RT_U32_NONNEG},
        {"--hash-every-frame", DB_RUNTIME_OPT_HASH_EVERY_FRAME, DB_CLI_RT_BOOL},
        {"--offscreen", DB_RUNTIME_OPT_OFFSCREEN, DB_CLI_RT_BOOL},
        {"--offscreen-frames", DB_RUNTIME_OPT_OFFSCREEN_FRAMES,
         DB_CLI_RT_U32_POSITIVE},
        {"--random-seed", DB_RANDOM_SEED_ENV, DB_CLI_RT_RANDOM_SEED},
        {"--vsync", DB_RUNTIME_OPT_VSYNC, DB_CLI_RT_BOOL},
    };

    for (size_t map_index = 0;
         map_index < (sizeof(mappings) / sizeof(mappings[0])); map_index++) {
        if (db_string_is(arg, mappings[map_index].cli_option)) {
            const char *value = db_expect_value(argc, argv, index);
            if (mappings[map_index].kind == DB_CLI_RT_BOOL) {
                db_cli_set_runtime_bool_or_exit(
                    mappings[map_index].runtime_option,
                    mappings[map_index].cli_option, value);
            } else if (mappings[map_index].kind == DB_CLI_RT_U32_NONNEG) {
                db_cli_set_runtime_u32_or_exit(
                    mappings[map_index].runtime_option,
                    mappings[map_index].cli_option, value, 1);
            } else if (mappings[map_index].kind == DB_CLI_RT_U32_POSITIVE) {
                db_cli_set_runtime_u32_or_exit(
                    mappings[map_index].runtime_option,
                    mappings[map_index].cli_option, value, 0);
            } else if (mappings[map_index].kind == DB_CLI_RT_MODE) {
                db_cli_set_runtime_mode_or_exit(value);
            } else if (mappings[map_index].kind == DB_CLI_RT_FPS_CAP) {
                db_cli_set_runtime_fps_cap_or_exit(value);
            } else if (mappings[map_index].kind == DB_CLI_RT_RANDOM_SEED) {
                db_cli_set_runtime_random_seed_or_exit(value);
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
}

void db_cli_parse_or_exit(int argc, char **argv, db_cli_config_t *out_cfg) {
    if (out_cfg == NULL) {
        db_failf("driverbench_cli", "output config is null");
    }
    g_cli_runtime_text_pool.used = 0U;
    *out_cfg = (db_cli_config_t){
        .api = DB_API_OPENGL,
        .display = DB_DISPLAY_OFFSCREEN,
        .renderer = DB_GL_RENDERER_GL3_3,
        .kms_card = "/dev/dri/card0",
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
        if (db_try_parse_runtime_override_option(arg, argc, argv, &i) != 0) {
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
}
