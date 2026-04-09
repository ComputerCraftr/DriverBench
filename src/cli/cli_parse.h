#ifndef DRIVERBENCH_CLI_PARSE_H
#define DRIVERBENCH_CLI_PARSE_H

#include <stddef.h>

#include "driverbench_config.h"

int db_cli_try_parse(int argc, char **argv, db_cli_config_t *out_cfg,
                     int *out_show_help, int *out_print_usage, char *error,
                     size_t error_size);

#endif
