#ifndef DRIVERBENCH_CORE_BENCHMARK_MODEL_H
#define DRIVERBENCH_CORE_BENCHMARK_MODEL_H

#include "db_frame_plan.h"
#include "db_render_result.h"
#include "db_renderer_runtime_contract.h"

#include <stdint.h>

typedef struct {
    void *context;
} db_benchmark_model_t;

typedef struct {
    const void *benchmark_configuration;
    db_pixel_format_t working_format;
} db_benchmark_model_config_t;

typedef struct {
    db_frame_plan_status_t (*probe)(const db_benchmark_model_t *model,
                                    uint32_t frame_index,
                                    db_frame_requirements_t *requirements);
    db_frame_plan_status_t (*provision)(
        db_benchmark_model_t *model,
        const db_frame_requirements_t *requirements,
        db_frame_checkpoint_binding_t *binding);
    db_frame_plan_status_t (*generate)(db_benchmark_model_t *model,
                                       uint32_t frame_index,
                                       const db_frame_plan_request_t *request,
                                       db_frame_plan_t *plan);
    void (*commit)(db_benchmark_model_t *model, const db_frame_plan_t *plan,
                   const db_render_result_t *result);
    void (*abort)(db_benchmark_model_t *model);
} db_benchmark_model_ops_t;

int db_benchmark_model_init(db_benchmark_model_t *model,
                            const db_benchmark_model_config_t *configuration);
db_frame_plan_status_t
db_benchmark_model_probe(const db_benchmark_model_t *model,
                         uint32_t frame_index,
                         db_frame_requirements_t *requirements);
db_frame_plan_status_t
db_benchmark_model_provision(db_benchmark_model_t *model,
                             const db_frame_requirements_t *requirements,
                             db_frame_checkpoint_binding_t *binding);
db_frame_plan_status_t
db_benchmark_model_generate(db_benchmark_model_t *model, uint32_t frame_index,
                            const db_frame_plan_request_t *request,
                            db_frame_plan_t *plan);
void db_benchmark_model_commit(db_benchmark_model_t *model,
                               const db_frame_plan_t *plan,
                               const db_render_result_t *result);
void db_benchmark_model_abort(db_benchmark_model_t *model);
void db_benchmark_model_shutdown(db_benchmark_model_t *model);
const db_benchmark_model_ops_t *db_benchmark_model_ops(void);

#endif
