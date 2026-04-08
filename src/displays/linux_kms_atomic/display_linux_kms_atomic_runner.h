#ifndef DRIVERBENCH_DISPLAY_LINUX_KMS_ATOMIC_RUNNER_H
#define DRIVERBENCH_DISPLAY_LINUX_KMS_ATOMIC_RUNNER_H

#include <stdint.h>

#include "../../driverbench_config.h"
#include "../display_types.h"

typedef enum {
    DB_KMS_ATOMIC_CONTEXT_GL1_5_OR_GLES1_1 = 0,
    DB_KMS_ATOMIC_CONTEXT_GL3_3 = 1,
} db_kms_atomic_context_profile_t;

typedef struct {
    const char *(*capability_mode)(void);
    void (*draw_stats)(uint64_t *full_draw_frames, uint64_t *dirty_draw_frames);
    void (*init)(void);
    void (*render_frame)(uint32_t frame_index,
                         uint32_t preserved_framebuffer_count);
    void (*shutdown)(void);
    uint32_t (*work_unit_count)(void);
} db_kms_atomic_renderer_vtable_t;

int db_kms_atomic_run(const char *backend, const char *renderer_name,
                      const char *card, db_gl_renderer_t gl_renderer,
                      db_kms_atomic_context_profile_t context_profile,
                      const db_kms_atomic_renderer_vtable_t *renderer,
                      const db_cli_config_t *cfg);
int db_kms_atomic_run_cpu(const char *backend, const char *renderer_name,
                          const char *card, db_api_t api,
                          const db_cli_config_t *cfg);

#endif
