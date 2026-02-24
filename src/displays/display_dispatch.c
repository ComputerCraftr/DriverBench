#include "display_dispatch.h"
#include "../core/db_core.h"
#include "../driverbench_cli.h"

int db_run_display_auto(db_display_t display, db_gl_renderer_t renderer,
                        const char *kms_card_path, const db_cli_config_t *cfg) {
    if (db_dispatch_display_is_compiled(display) == 0) {
        db_failf("display_dispatch",
                 "requested display is unavailable in this build "
                 "(display=%d)",
                 (int)display);
    }

    if (db_dispatch_display_has_any_api(display) == 0) {
        db_failf("display_dispatch",
                 "no compatible api for selected display in this build "
                 "(display=%d)",
                 (int)display);
    }

    if (db_dispatch_display_supports_api(display, DB_API_VULKAN) != 0) {
        return db_run_display(display, DB_API_VULKAN, renderer, kms_card_path,
                              cfg);
    }
    if (db_dispatch_display_supports_api(display, DB_API_OPENGL) != 0) {
        return db_run_display(display, DB_API_OPENGL, renderer, kms_card_path,
                              cfg);
    }
    return db_run_display(display, DB_API_CPU, renderer, kms_card_path, cfg);
}

int db_run_display(db_display_t display, db_api_t api,
                   db_gl_renderer_t renderer, const char *kms_card_path,
                   const db_cli_config_t *cfg) {
    if (db_dispatch_display_is_compiled(display) == 0) {
        db_failf("display_dispatch",
                 "requested display is unavailable in this build "
                 "(display=%d)",
                 (int)display);
    }

    if (db_dispatch_display_supports_api(display, api) == 0) {
        db_failf("display_dispatch",
                 "requested display/api combination is unavailable in this "
                 "build (display=%d api=%d)",
                 (int)display, (int)api);
    }

    if (display == DB_DISPLAY_OFFSCREEN) {
        return db_run_offscreen(api, renderer, cfg);
    }

    if (display == DB_DISPLAY_GLFW_WINDOW) {
#ifdef DB_HAS_GLFW
        return db_run_glfw_window(api, renderer, cfg);
#else
        db_failf("display_dispatch",
                 "requested glfw_window display is unavailable in this build");
#endif
    }

    if (display == DB_DISPLAY_LINUX_KMS_ATOMIC) {
#ifdef DB_HAS_LINUX_KMS_ATOMIC
        return db_run_linux_kms_atomic(api, renderer, kms_card_path, cfg);
#else
        db_failf(
            "display_dispatch",
            "requested linux_kms_atomic display is unavailable in this build");
#endif
    }

    db_failf("display_dispatch", "unknown display selector: %d", (int)display);
}
