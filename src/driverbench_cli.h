#ifndef DRIVERBENCH_CLI_H
#define DRIVERBENCH_CLI_H

#include "displays/display_dispatch.h"

typedef struct {
    db_api_t api;
    db_display_t display;
    db_gl_renderer_t renderer;
    const char *kms_card;
    int api_is_auto;
    int display_is_set;
    int renderer_is_auto;
} db_cli_config_t;

void db_cli_parse_or_exit(int argc, char **argv, db_cli_config_t *out_cfg);

#endif
