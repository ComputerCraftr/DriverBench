#ifndef DRIVERBENCH_CONFIG_H
#define DRIVERBENCH_CONFIG_H

#include <stdint.h>

#include "displays/display_types.h"

typedef struct db_cli_config {
    db_api_t api;
    db_display_t display;
    db_gl_renderer_t renderer;
    const char *kms_card;
    const char *hash_mode;
    const char *hash_report;
    double fps_cap;
    uint32_t frame_limit;
    int backbuffer_draw_full;
    int backbuffer_draw_mode_explicit;
    int glfw_window_hidden;
    int debug_clear_default_framebuffer;
    int vsync_enabled;
    int api_is_auto;
    int display_is_set;
    int renderer_is_auto;
} db_cli_config_t;

#endif
