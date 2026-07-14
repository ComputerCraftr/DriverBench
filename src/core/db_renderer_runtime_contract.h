#ifndef DRIVERBENCH_CORE_DB_RENDERER_RUNTIME_CONTRACT_H
#define DRIVERBENCH_CORE_DB_RENDERER_RUNTIME_CONTRACT_H

#include <stdint.h>

#include "core/db_format_contract.h"
#include "core/db_renderer_diagnostics.h"
#include "core/db_renderer_support.h"

typedef struct {
    db_renderer_execution_config_t execution;
    db_display_resolved_format_config_t format;
    db_render_format_contract_t format_contract;
    db_renderer_diagnostic_config_t diagnostics;
    uint32_t preserved_framebuffer_count;
} db_renderer_runtime_contract_t;

#endif
