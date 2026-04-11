#include "core/db_frame_source.h"
#include "core/db_frame_plan.h"
#include "core/db_render_result.h"

#include "db_benchmark_core.h"

#include <stdint.h>
#include <stdlib.h>

int db_frame_source_init(db_frame_source_t *source,
                         const db_frame_source_config_t *configuration) {
    if ((source == NULL) || (configuration == NULL) ||
        (configuration->benchmark_configuration == NULL)) {
        return 0;
    }
    db_benchmark_core_t *const core = calloc(1U, sizeof(*core));
    if (core == NULL) {
        return 0;
    }
    db_benchmark_core_init(core, configuration->benchmark_configuration,
                           configuration->working_format);
    source->context = core;
    return 1;
}

void db_frame_source_generate(db_frame_source_t *source, uint32_t frame_index,
                              const db_frame_plan_request_t *request,
                              db_frame_plan_t *plan) {
    if ((source == NULL) || (source->context == NULL) || (plan == NULL)) {
        return;
    }
    db_benchmark_core_generate_plan(source->context, frame_index, request,
                                    plan);
}

void db_frame_source_commit(db_frame_source_t *source,
                            const db_frame_plan_t *plan,
                            const db_render_result_t *result) {
    if ((source == NULL) || (source->context == NULL) || (plan == NULL) ||
        (result == NULL)) {
        return;
    }
    db_benchmark_core_apply_plan(source->context, plan, result);
}

void db_frame_source_shutdown(db_frame_source_t *source) {
    if ((source == NULL) || (source->context == NULL)) {
        return;
    }
    db_benchmark_core_shutdown(source->context);
    free(source->context);
    source->context = NULL;
}
