#ifndef DRIVERBENCH_CLI_RUNTIME_OPTIONS_H
#define DRIVERBENCH_CLI_RUNTIME_OPTIONS_H

#include "driverbench_config.h"

#include <stddef.h>

void db_cli_runtime_options_begin_parse(void);
int db_cli_try_expect_value(size_t argc, const char *const *argv, size_t *index,
                            const char **out_value, char *error,
                            size_t error_size);
int db_cli_try_parse_runtime_override_option(const char *arg, size_t argc,
                                             const char *const *argv,
                                             size_t *index,
                                             db_cli_config_t *cfg, char *error,
                                             size_t error_size);

#endif
