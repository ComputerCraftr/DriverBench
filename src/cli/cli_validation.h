#ifndef DRIVERBENCH_CLI_VALIDATION_H
#define DRIVERBENCH_CLI_VALIDATION_H

#include <stddef.h>

#include "driverbench_config.h"

int db_cli_validation_set_error(char *error, size_t error_size, const char *fmt,
                                ...) __attribute__((format(printf, 3, 4)));
int db_cli_validate_config(const db_cli_config_t *cfg, char *error,
                           size_t error_size);

#endif
