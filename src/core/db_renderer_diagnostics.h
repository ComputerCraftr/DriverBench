#ifndef DRIVERBENCH_CORE_DB_RENDERER_DIAGNOSTICS_H
#define DRIVERBENCH_CORE_DB_RENDERER_DIAGNOSTICS_H

#include <stdint.h>

enum { DB_GL1_REPLAY_CAPACITY_MAX = 8U };

typedef enum {
    DB_GL1_TARGET_AUTO = 0,
    DB_GL1_TARGET_DIRECT_WINDOW,
    DB_GL1_TARGET_PERSISTENT_FBO,
    DB_GL1_TARGET_CPU_UPLOAD,
} db_gl1_target_request_t;

typedef enum {
    DB_GL1_GRADIENT_AUTO = 0,
    DB_GL1_GRADIENT_INTERPOLATED,
    DB_GL1_GRADIENT_ROW_FILL,
    DB_GL1_GRADIENT_CPU,
} db_gl1_gradient_request_t;

typedef enum {
    DB_GL3_GRADIENT_AUTO = 0,
    DB_GL3_GRADIENT_SEMANTIC,
    DB_GL3_GRADIENT_EXACT_LOOKUP,
    DB_GL3_GRADIENT_ROW_FILL,
} db_gl3_gradient_request_t;

typedef enum {
    DB_VK_GRADIENT_AUTO = 0,
    DB_VK_GRADIENT_SEMANTIC,
    DB_VK_GRADIENT_ROW_FILL,
} db_vk_gradient_request_t;

typedef struct {
    db_gl1_target_request_t gl1_target;
    db_gl1_gradient_request_t gl1_gradient;
    db_gl3_gradient_request_t gl3_gradient;
    db_vk_gradient_request_t vk_gradient;
    const char *gradient_divergence_path;
    uint32_t gl1_replay_capacity;
    int ignore_conformance_cache;
    int rerun_conformance_probe;
} db_renderer_diagnostic_config_t;

const char *db_gl1_target_request_name(db_gl1_target_request_t value);
const char *db_gl1_gradient_request_name(db_gl1_gradient_request_t value);
const char *db_gl3_gradient_request_name(db_gl3_gradient_request_t value);
const char *db_vk_gradient_request_name(db_vk_gradient_request_t value);
db_renderer_diagnostic_config_t db_renderer_diagnostic_config_resolve(void);

#endif
