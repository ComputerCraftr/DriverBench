#ifndef DRIVERBENCH_GL3_TARGET_H
#define DRIVERBENCH_GL3_TARGET_H

#include "core/db_frame_plan.h"
#include "core/db_render_types.h"

#include <stdint.h>

typedef struct {
    int width;
    int height;
    unsigned int fbo;
    unsigned int texture;
    int valid;
    uint32_t generation;
    db_pixel_format_t format;
} gl3_persistent_target_t;

void db_gl3_target_destroy(gl3_persistent_target_t *target, const char *cause);
int db_gl3_target_ensure(gl3_persistent_target_t *target, int width,
                         int height);
void db_gl3_target_restore(gl3_persistent_target_t *target,
                           const db_frame_plan_t *plan);
unsigned int db_gl3_build_program(const char *vertex_source,
                                  const char *fragment_source);

#endif
