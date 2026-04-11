#ifndef DRIVERBENCH_CORE_HASH_ORACLE_H
#define DRIVERBENCH_CORE_HASH_ORACLE_H

#include <stdint.h>

typedef struct {
    uint64_t plan_hash;
    uint64_t draw_geometry_hash;
    uint64_t shadow_repair_hash;
    uint64_t texture_upload_hash;
    uint64_t framebuffer_hash;
} db_stage_hashes_t;

#endif
