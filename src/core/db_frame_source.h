#ifndef DRIVERBENCH_CORE_FRAME_SOURCE_H
#define DRIVERBENCH_CORE_FRAME_SOURCE_H

#include "db_frame_plan.h"
#include "db_render_result.h"
#include "db_renderer_runtime_contract.h"

#include <stdint.h>

typedef struct {
    void *context;
} db_frame_source_t;

typedef struct {
    const void *benchmark_configuration;
    db_pixel_format_t working_format;
} db_frame_source_config_t;

int db_frame_source_init(db_frame_source_t *source,
                         const db_frame_source_config_t *configuration);
void db_frame_source_generate(db_frame_source_t *source, uint32_t frame_index,
                              const db_frame_plan_request_t *request,
                              db_frame_plan_t *plan);
void db_frame_source_commit(db_frame_source_t *source,
                            const db_frame_plan_t *plan,
                            const db_render_result_t *result);
static inline void db_frame_source_commit_success(db_frame_source_t *source,
                                                  const db_frame_plan_t *plan) {
    const db_render_result_t result = db_render_result_success();
    db_frame_source_commit(source, plan, &result);
}
static inline void
db_frame_source_commit_success_with_hash(db_frame_source_t *source,
                                         const db_frame_plan_t *plan,
                                         uint64_t working_hash) {
    const db_render_result_t result = {
        .success = 1,
        .working_hash = working_hash,
        .working_hash_valid = 1,
    };
    db_frame_source_commit(source, plan, &result);
}
void db_frame_source_shutdown(db_frame_source_t *source);

#endif
