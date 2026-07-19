#include "core/db_benchmark_model.h"

#include "core/db_frame_plan.h"
#include "core/db_render_result.h"
#include "db_benchmark_core.h"

#include <stdint.h>
#include <stdlib.h>

int db_benchmark_model_init(db_benchmark_model_t *model,
                            const db_benchmark_model_config_t *configuration) {
    if ((model == NULL) || (configuration == NULL) ||
        (configuration->benchmark_configuration == NULL)) {
        return 0;
    }
    db_benchmark_core_t *const core = calloc(1U, sizeof(*core));
    if (core == NULL) {
        return 0;
    }
    db_benchmark_core_init(core, configuration->benchmark_configuration,
                           configuration->working_format);
    model->context = core;
    return 1;
}

db_frame_plan_status_t
db_benchmark_model_probe(const db_benchmark_model_t *model,
                         uint32_t frame_index,
                         db_frame_requirements_t *requirements) {
    if ((model == NULL) || (model->context == NULL) || (requirements == NULL)) {
        return DB_FRAME_PLAN_INVALID;
    }
    return db_benchmark_core_probe_frame(model->context, frame_index,
                                         requirements);
}

db_frame_plan_status_t
db_benchmark_model_provision(db_benchmark_model_t *model,
                             const db_frame_requirements_t *requirements,
                             db_frame_checkpoint_binding_t *binding) {
    if ((model == NULL) || (model->context == NULL) || (requirements == NULL)) {
        return DB_FRAME_PLAN_INVALID;
    }
    return db_benchmark_core_provision_requirements(model->context,
                                                    requirements, binding);
}

db_frame_plan_status_t
db_benchmark_model_generate(db_benchmark_model_t *model, uint32_t frame_index,
                            const db_frame_plan_request_t *request,
                            db_frame_plan_t *plan) {
    if ((model == NULL) || (model->context == NULL) || (plan == NULL)) {
        return DB_FRAME_PLAN_INVALID;
    }
    return db_benchmark_core_generate_plan(model->context, frame_index, request,
                                           plan);
}

void db_benchmark_model_commit(db_benchmark_model_t *model,
                               const db_frame_plan_t *plan,
                               const db_render_result_t *result) {
    if ((model == NULL) || (model->context == NULL) || (plan == NULL) ||
        (result == NULL)) {
        return;
    }
    db_benchmark_core_apply_plan(model->context, plan, result);
}

void db_benchmark_model_abort(db_benchmark_model_t *model) {
    if ((model == NULL) || (model->context == NULL)) {
        return;
    }
    db_benchmark_core_abort_plan(model->context);
}

void db_benchmark_model_shutdown(db_benchmark_model_t *model) {
    if ((model == NULL) || (model->context == NULL)) {
        return;
    }
    db_benchmark_core_shutdown(model->context);
    free(model->context);
    model->context = NULL;
}

const db_benchmark_model_ops_t *db_benchmark_model_ops(void) {
    static const db_benchmark_model_ops_t operations = {
        .probe = db_benchmark_model_probe,
        .provision = db_benchmark_model_provision,
        .generate = db_benchmark_model_generate,
        .commit = db_benchmark_model_commit,
        .abort = db_benchmark_model_abort,
    };
    return &operations;
}
