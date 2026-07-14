#include "core/db_frame_source.h"
#include "core/db_frame_plan.h"
#include "core/db_log.h"
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

db_frame_plan_status_t
db_frame_source_generate(db_frame_source_t *source, uint32_t frame_index,
                         const db_frame_plan_request_t *request,
                         db_frame_plan_t *plan) {
    if ((source == NULL) || (source->context == NULL) || (plan == NULL)) {
        return DB_FRAME_PLAN_INVALID;
    }
    db_frame_requirements_t requirements = {0};
    db_frame_plan_status_t status = db_benchmark_core_probe_frame(
        source->context, frame_index, request, &requirements);
    if ((status == DB_FRAME_PLAN_CHECKPOINT_REQUIRED) ||
        ((status == DB_FRAME_PLAN_OK) &&
         (requirements.checkpoint_required != 0))) {
        db_frame_checkpoint_binding_t binding = {0};
        status = db_benchmark_core_provision_requirements(
            source->context, &requirements, &binding);
    }
    if (status == DB_FRAME_PLAN_OK) {
        status = db_benchmark_core_generate_plan(source->context, frame_index,
                                                 request, plan);
    }
    if (status != DB_FRAME_PLAN_OK) {
        const db_log_field_t fields[] = {
            DB_LOG_TOKEN("code", "frame_plan_rejected"),
            DB_LOG_TOKEN("status", db_frame_plan_status_name(status)),
            DB_LOG_U64("frame", frame_index),
        };
        db_log_error("benchmark", "frame_plan_error", fields,
                     DB_LOG_FIELD_COUNT(fields));
    }
    return status;
}

db_frame_plan_status_t db_frame_source_generate_prepared(
    db_frame_source_t *source, uint32_t frame_index,
    const db_frame_plan_request_t *request, db_frame_plan_t *plan) {
    if ((source == NULL) || (source->context == NULL) || (plan == NULL)) {
        return DB_FRAME_PLAN_INVALID;
    }
    return db_benchmark_core_generate_plan(source->context, frame_index,
                                           request, plan);
}

db_frame_plan_status_t
db_frame_source_probe(const db_frame_source_t *source, uint32_t frame_index,
                      const db_frame_plan_request_t *request,
                      db_frame_requirements_t *requirements) {
    if ((source == NULL) || (source->context == NULL) ||
        (requirements == NULL)) {
        return DB_FRAME_PLAN_INVALID;
    }
    return db_benchmark_core_probe_frame(source->context, frame_index, request,
                                         requirements);
}

db_frame_plan_status_t
db_frame_source_provision(db_frame_source_t *source,
                          const db_frame_requirements_t *requirements,
                          db_frame_checkpoint_binding_t *binding) {
    if ((source == NULL) || (source->context == NULL) ||
        (requirements == NULL)) {
        return DB_FRAME_PLAN_INVALID;
    }
    return db_benchmark_core_provision_requirements(source->context,
                                                    requirements, binding);
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
