#ifndef DRIVERBENCH_CLI_H
#define DRIVERBENCH_CLI_H

#include <stddef.h>

#include "driverbench_config.h"

void db_cli_parse_or_exit(size_t argc, const char *const *argv,
                          db_cli_config_t *out_cfg);

#endif
