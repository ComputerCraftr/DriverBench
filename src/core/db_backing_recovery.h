#ifndef DRIVERBENCH_CORE_DB_BACKING_RECOVERY_H
#define DRIVERBENCH_CORE_DB_BACKING_RECOVERY_H

#include <stdint.h>

static inline uint32_t
db_backing_seed_frame_count(uint32_t preserved_framebuffer_count) {
    return preserved_framebuffer_count;
}

#endif
