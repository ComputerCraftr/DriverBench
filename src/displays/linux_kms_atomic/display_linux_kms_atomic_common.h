#ifndef DRIVERBENCH_DISPLAY_LINUX_KMS_ATOMIC_COMMON_H
#define DRIVERBENCH_DISPLAY_LINUX_KMS_ATOMIC_COMMON_H

#include <stdint.h>

#include "../display_dispatch.h"

typedef enum {
    DB_KMS_ATOMIC_CONTEXT_GL1_5_OR_GLES1_1 = 0,
    DB_KMS_ATOMIC_CONTEXT_GL3_3 = 1,
} db_kms_atomic_context_profile_t;

typedef struct {
    const char *(*capability_mode)(void);
    void (*init)(void);
    void (*render_frame)(double time_s);
    void (*shutdown)(void);
    uint32_t (*work_unit_count)(void);
} db_kms_atomic_renderer_vtable_t;

typedef void (*db_kms_atomic_runtime_check_fn_t)(const char *backend,
                                                 const char *runtime_version,
                                                 int runtime_is_gles);

int db_kms_atomic_run(const char *backend, const char *renderer_name,
                      const char *card,
                      db_kms_atomic_context_profile_t context_profile,
                      const db_kms_atomic_renderer_vtable_t *renderer,
                      db_kms_atomic_runtime_check_fn_t runtime_check);
int db_kms_atomic_run_cpu(const char *backend, const char *renderer_name,
                          const char *card, db_api_t api);

#endif
