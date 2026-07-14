#include "cli/cli_parse.h"
#include "cli/cli_runtime_options.h"
#include "cli/cli_validation.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config/benchmark_config.h"
#include "config/runtime_options.h"
#include "core/db_core.h"
#include "displays/display_types.h"
#include "driverbench_config.h"
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

    db_cli_runtime_options_begin_parse();
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
