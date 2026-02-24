#ifndef DRIVERBENCH_CLI_H
#define DRIVERBENCH_CLI_H

#include <stdint.h>

#include "displays/display_dispatch.h"

typedef struct db_cli_config {
    db_api_t api;
    db_display_t display;
    db_gl_renderer_t renderer;
    const char *kms_card;
    const char *hash_mode;
    const char *hash_report;
    double fps_cap;
    uint32_t frame_limit;
    int offscreen_enabled;
    int vsync_enabled;
    int api_is_auto;
    int display_is_set;
    int renderer_is_auto;
} db_cli_config_t;

void db_cli_parse_or_exit(int argc, char **argv, db_cli_config_t *out_cfg);

#endif
