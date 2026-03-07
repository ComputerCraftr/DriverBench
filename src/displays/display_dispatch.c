#include "display_dispatch.h"
#include <stddef.h>

#include "../core/db_core.h"
#include "../driverbench_config.h"
#include "display_types.h"

int db_dispatch_format_display_capabilities(db_display_t display, char *buffer,
                                            size_t buffer_size) {
    const db_display_backend_capabilities_t caps =
        db_dispatch_display_capabilities(display);
    const char *const cpu_text = (caps.supports_cpu != 0) ? "yes" : "no";
    const char *const opengl_text = (caps.supports_opengl != 0) ? "yes" : "no";
    const char *const vulkan_text = (caps.supports_vulkan != 0) ? "yes" : "no";
    const char *const gl1_text = (caps.supports_gl1 != 0) ? "yes" : "no";
    const char *const gl3_text = (caps.supports_gl3 != 0) ? "yes" : "no";
    return db_snprintf(buffer, buffer_size,
                       "compiled=%s cpu=%s opengl=%s vulkan=%s gl1=%s gl3=%s",
                       (caps.compiled != 0) ? "yes" : "no", cpu_text,
                       opengl_text, vulkan_text, gl1_text, gl3_text);
}

void db_dispatch_log_display_capabilities(const char *backend,
                                          db_display_t display) {
    char caps_text[DB_DISPLAY_CAPABILITY_TEXT_MAX] = {0};
    (void)db_dispatch_format_display_capabilities(display, caps_text,
                                                  sizeof(caps_text));
    db_infof((backend != NULL) ? backend : "display_dispatch",
             "display capabilities: display=%d %s", (int)display, caps_text);
}

void db_dispatch_validate_backend_or_fail(const char *backend,
                                          db_display_t display, db_api_t api,
                                          db_gl_renderer_t renderer) {
    char caps_text[DB_DISPLAY_CAPABILITY_TEXT_MAX] = {0};
    (void)db_dispatch_format_display_capabilities(display, caps_text,
                                                  sizeof(caps_text));
    if (db_dispatch_display_supports_backend(display, api, renderer) == 0) {
        db_failf((backend != NULL) ? backend : "display_dispatch",
                 "requested display/api/renderer combination is unavailable "
                 "in this build (display=%d api=%d renderer=%s %s)",
                 (int)display, (int)api, db_dispatch_gl_renderer_name(renderer),
                 caps_text);
    }
}

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

    return db_run_display(display,
                          db_dispatch_display_preferred_auto_api(display),
                          renderer, kms_card_path, cfg);
}

int db_run_display(db_display_t display, db_api_t api,
                   db_gl_renderer_t renderer, const char *kms_card_path,
                   const db_cli_config_t *cfg) {
#ifndef DB_HAS_LINUX_KMS_ATOMIC
    (void)kms_card_path;
#endif
    char caps_text[DB_DISPLAY_CAPABILITY_TEXT_MAX] = {0};
    (void)db_dispatch_format_display_capabilities(display, caps_text,
                                                  sizeof(caps_text));
    if (db_dispatch_display_is_compiled(display) == 0) {
        db_failf("display_dispatch",
                 "requested display is unavailable in this build "
                 "(display=%d %s)",
                 (int)display, caps_text);
    }

    db_dispatch_validate_backend_or_fail("display_dispatch", display, api,
                                         renderer);
    db_dispatch_log_display_capabilities("display_dispatch", display);

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
