#include "db_renderer_diagnostics.h"

#include "config/runtime_options.h"
#include "core/db_core.h"

#include <stdint.h>
#include <string.h>

static int text_is(const char *value, const char *expected) {
    return (value != NULL) && (strcmp(value, expected) == 0);
}

const char *db_gl1_target_request_name(db_gl1_target_request_t value) {
    switch (value) {
    case DB_GL1_TARGET_AUTO:
        return "auto";
    case DB_GL1_TARGET_DIRECT_WINDOW:
        return "direct_window";
    case DB_GL1_TARGET_PERSISTENT_FBO:
        return "persistent_fbo";
    case DB_GL1_TARGET_CPU_UPLOAD:
        return "cpu_upload";
    }
    return "unknown";
}

const char *db_gl1_gradient_request_name(db_gl1_gradient_request_t value) {
    switch (value) {
    case DB_GL1_GRADIENT_AUTO:
        return "auto";
    case DB_GL1_GRADIENT_INTERPOLATED:
        return "interpolated";
    case DB_GL1_GRADIENT_ROW_FILL:
        return "row_fill";
    case DB_GL1_GRADIENT_CPU:
        return "cpu";
    }
    return "unknown";
}

const char *db_gl3_gradient_request_name(db_gl3_gradient_request_t value) {
    switch (value) {
    case DB_GL3_GRADIENT_AUTO:
        return "auto";
    case DB_GL3_GRADIENT_SEMANTIC:
        return "semantic";
    case DB_GL3_GRADIENT_EXACT_LOOKUP:
        return "exact_lookup";
    case DB_GL3_GRADIENT_ROW_FILL:
        return "row_fill";
    }
    return "unknown";
}

const char *db_vk_gradient_request_name(db_vk_gradient_request_t value) {
    switch (value) {
    case DB_VK_GRADIENT_AUTO:
        return "auto";
    case DB_VK_GRADIENT_SEMANTIC:
        return "semantic";
    case DB_VK_GRADIENT_ROW_FILL:
        return "row_fill";
    }
    return "unknown";
}

db_renderer_diagnostic_config_t db_renderer_diagnostic_config_resolve(void) {
    db_renderer_diagnostic_config_t result = {
        .gl1_replay_capacity = DB_GL1_REPLAY_CAPACITY_MAX,
    };
    const char *const gl1_target =
        db_runtime_option_get(DB_RUNTIME_OPT_GL1_TARGET);
    const char *const gl1_gradient =
        db_runtime_option_get(DB_RUNTIME_OPT_GL1_GRADIENT);
    const char *const gl3_gradient =
        db_runtime_option_get(DB_RUNTIME_OPT_GL3_GRADIENT);
    const char *const vk_gradient =
        db_runtime_option_get(DB_RUNTIME_OPT_VK_GRADIENT);
    if (text_is(gl1_target, "direct-window")) {
        result.gl1_target = DB_GL1_TARGET_DIRECT_WINDOW;
    } else if (text_is(gl1_target, "persistent-fbo")) {
        result.gl1_target = DB_GL1_TARGET_PERSISTENT_FBO;
    } else if (text_is(gl1_target, "cpu-upload")) {
        result.gl1_target = DB_GL1_TARGET_CPU_UPLOAD;
    }
    if (text_is(gl1_gradient, "interpolated")) {
        result.gl1_gradient = DB_GL1_GRADIENT_INTERPOLATED;
    } else if (text_is(gl1_gradient, "row-fill")) {
        result.gl1_gradient = DB_GL1_GRADIENT_ROW_FILL;
    } else if (text_is(gl1_gradient, "cpu")) {
        result.gl1_gradient = DB_GL1_GRADIENT_CPU;
    }
    if (text_is(gl3_gradient, "semantic")) {
        result.gl3_gradient = DB_GL3_GRADIENT_SEMANTIC;
    } else if (text_is(gl3_gradient, "exact-lookup")) {
        result.gl3_gradient = DB_GL3_GRADIENT_EXACT_LOOKUP;
    } else if (text_is(gl3_gradient, "row-fill")) {
        result.gl3_gradient = DB_GL3_GRADIENT_ROW_FILL;
    }
    if (text_is(vk_gradient, "semantic")) {
        result.vk_gradient = DB_VK_GRADIENT_SEMANTIC;
    } else if (text_is(vk_gradient, "row-fill")) {
        result.vk_gradient = DB_VK_GRADIENT_ROW_FILL;
    }
    int replay_capacity = 0;
    if (db_parse_int_text(
            db_runtime_option_get(DB_RUNTIME_OPT_GL1_REPLAY_CAPACITY),
            &replay_capacity) != 0) {
        result.gl1_replay_capacity = (uint32_t)replay_capacity;
    }
    result.ignore_conformance_cache = text_is(
        db_runtime_option_get(DB_RUNTIME_OPT_IGNORE_CONFORMANCE_CACHE), "1");
    result.rerun_conformance_probe = text_is(
        db_runtime_option_get(DB_RUNTIME_OPT_RERUN_CONFORMANCE_PROBE), "1");
    result.gradient_divergence_path =
        db_runtime_option_get(DB_RUNTIME_OPT_DUMP_GRADIENT_DIVERGENCE);
    return result;
}
