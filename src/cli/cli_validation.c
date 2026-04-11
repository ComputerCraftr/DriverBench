#include "cli/cli_validation.h"
#include "core/db_numeric.h"

#include <stdint.h>
#include <string.h>

#include "config/runtime_options.h"
#include "displays/display_dispatch.h"
#include "displays/display_types.h"
#include "driverbench_config.h"
#include "renderers/gl_common.h"

static int db_cli_validation_string_is(const char *value,
                                       const char *expected) {
    return (value != NULL) && (expected != NULL) &&
           (strcmp(value, expected) == 0);
}

static int db_cli_validate_compiled_support(const db_cli_config_t *cfg,
                                            char *error, size_t error_size) {
    if (cfg == NULL) {
        return db_cli_validation_set_error(error, error_size, "config is null");
    }
    if (db_dispatch_display_capabilities(cfg->display).compiled == 0) {
        char caps_text[DB_DISPLAY_CAPABILITY_TEXT_MAX] = {0};
        (void)db_dispatch_format_display_capabilities(cfg->display, caps_text,
                                                      sizeof(caps_text));
        return db_cli_validation_set_error(
            error, error_size,
            "requested display is unavailable in this build (%s)", caps_text);
    }
    if (cfg->api_is_auto != 0) {
        if (db_dispatch_display_has_any_api(cfg->display) == 0) {
            char caps_text[DB_DISPLAY_CAPABILITY_TEXT_MAX] = {0};
            (void)db_dispatch_format_display_capabilities(
                cfg->display, caps_text, sizeof(caps_text));
            return db_cli_validation_set_error(
                error, error_size,
                "requested display has no compatible API (%s)", caps_text);
        }
        return 1;
    }
    if (db_dispatch_api_is_compiled(cfg->api) == 0) {
        return db_cli_validation_set_error(
            error, error_size, "requested API is unavailable in this build");
    }
    if (db_dispatch_display_supports_api(cfg->display, cfg->api) == 0) {
        char caps_text[DB_DISPLAY_CAPABILITY_TEXT_MAX] = {0};
        (void)db_dispatch_format_display_capabilities(cfg->display, caps_text,
                                                      sizeof(caps_text));
        return db_cli_validation_set_error(
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
        return db_cli_validation_set_error(error, error_size, "config is null");
    }
    if (cfg->api_is_auto == 0) {
        *out_api = cfg->api;
        return 1;
    }
    if (db_dispatch_display_has_any_api(cfg->display) == 0) {
        char caps_text[DB_DISPLAY_CAPABILITY_TEXT_MAX] = {0};
        (void)db_dispatch_format_display_capabilities(cfg->display, caps_text,
                                                      sizeof(caps_text));
        return db_cli_validation_set_error(
            error, error_size, "requested display has no compatible API (%s)",
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
        return db_cli_validation_set_error(
            error, error_size,
            "--renderer requires an explicit API; set --api opengl");
    }
    db_api_t effective_api = DB_API_OPENGL;
    if (db_cli_resolve_effective_api(cfg, &effective_api, error, error_size) ==
        0) {
        return 0;
    }
    if (effective_api != DB_API_OPENGL) {
        return db_cli_validation_set_error(
            error, error_size,
            "--renderer requires effective API OpenGL, but "
            "effective API is %s; "
            "set --api opengl",
            db_dispatch_api_name(effective_api));
    }
    if (db_dispatch_display_supports_backend(cfg->display, DB_API_OPENGL,
                                             cfg->renderer) == 0) {
        return db_cli_validation_set_error(
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
        db_cli_validation_string_is(hash_mode, "none")) {
        return 1;
    }
    db_api_t api = DB_API_OPENGL;
    if (db_cli_resolve_effective_api(cfg, &api, error, error_size) == 0) {
        return 0;
    }
    const int needs_state = db_cli_validation_string_is(hash_mode, "state") ||
                            db_cli_validation_string_is(hash_mode, "both");
    const int needs_pixel = db_cli_validation_string_is(hash_mode, "pixel") ||
                            db_cli_validation_string_is(hash_mode, "both");
    int supports_state = 0;
    int supports_pixel = 0;
    if ((cfg->display == DB_GLFW_WINDOW_DISPLAY) ||
        (cfg->display == DB_OFFSCREEN_DISPLAY)) {
        if (api == DB_API_VULKAN) {
            supports_state = 1;
            supports_pixel = DB_BOOL(cfg->display == DB_GLFW_WINDOW_DISPLAY);
        } else if ((api == DB_API_OPENGL) || (api == DB_API_CPU)) {
            supports_state = 1;
            supports_pixel = 1;
        }
    }
    if ((needs_state != 0) && (supports_state == 0)) {
        return db_cli_validation_set_error(
            error, error_size,
            "hash mode '%s' is unsupported for display/API combination "
            "(display=%d api=%d): state hash unavailable",
            hash_mode, (int)cfg->display, (int)api);
    }
    if ((needs_pixel != 0) && (supports_pixel == 0)) {
        return db_cli_validation_set_error(
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
    if (cfg->display != DB_GLFW_WINDOW_DISPLAY) {
        return db_cli_validation_set_error(
            error, error_size,
            "--glfw-hidden-window requires --display glfw_window");
    }
    return 1;
}

static int db_cli_validate_resize_schedule(const db_cli_config_t *cfg,
                                           char *error, size_t error_size) {
    if ((cfg == NULL) ||
        (db_runtime_option_get(DB_RUNTIME_OPT_RESIZE_AT_FRAME) == NULL)) {
        return 1;
    }
    if (cfg->display != DB_GLFW_WINDOW_DISPLAY) {
        return db_cli_validation_set_error(
            error, error_size,
            "--resize-at-frame requires --display glfw_window");
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
        return db_cli_validation_set_error(error, error_size,
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
        return db_cli_validation_set_error(
            error, error_size,
            "--present-buffer-mode is unsupported for Vulkan");
    }

    if (api == DB_API_CPU) {
        if (cfg->display != DB_GLFW_WINDOW_DISPLAY) {
            return db_cli_validation_set_error(
                error, error_size,
                "--present-buffer-mode is only supported "
                "for CPU with --display glfw_window");
        }
        if (present_mode != DB_GL_PRESENT_BUFFER_MODE_REPLACE) {
            return db_cli_validation_set_error(
                error, error_size,
                "--present-buffer-mode for CPU GLFW must be replace");
        }
        return 1;
    }

    if (cfg->display != DB_GLFW_WINDOW_DISPLAY) {
        return db_cli_validation_set_error(
            error, error_size,
            "--present-buffer-mode is only supported for "
            "--display glfw_window");
    }
    if (cfg->renderer != DB_GL_RENDERER_GL1_5_GLES1_1) {
        return db_cli_validation_set_error(
            error, error_size,
            "--present-buffer-mode is only supported for "
            "--renderer gl1_5_gles1_1");
    }
    if (present_mode == DB_GL_PRESENT_BUFFER_MODE_REPLACE) {
        if (cfg->backbuffer_draw_full == 0) {
            return db_cli_validation_set_error(
                error, error_size,
                "--present-buffer-mode replace requires "
                "--backbuffer-draw-mode full");
        }
        return db_cli_validation_set_error(
            error, error_size,
            "--present-buffer-mode replace is incompatible "
            "with GL1 preserved full-present");
    }
    return 1;
}

int db_cli_validate_config(const db_cli_config_t *cfg, char *error,
                           size_t error_size) {
    return db_cli_validate_compiled_support(cfg, error, error_size) &&
           db_cli_validate_renderer_selection(cfg, error, error_size) &&
           db_cli_validate_glfw_hidden_window(cfg, error, error_size) &&
           db_cli_validate_resize_schedule(cfg, error, error_size) &&
           db_cli_validate_present_buffer_mode(cfg, error, error_size) &&
           db_cli_validate_hash_mode(cfg, error, error_size);
}
