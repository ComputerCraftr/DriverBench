#include "db_qualification_contracts.h"

#include "db_conformance.h"
#include "db_hash.h"
#include "db_numeric.h"
#include "db_probe_protocol.h"
#include "db_render_result.h"
#include "db_render_types.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

int db_qualification_generation_equal(
    db_qualification_identity_generation_t left,
    db_qualification_identity_generation_t right) {
    return (left.device_generation == right.device_generation) &&
           (left.implementation_generation ==
            right.implementation_generation) &&
           (left.target_contract_generation ==
            right.target_contract_generation);
}

uint64_t db_qualification_generation_hash(
    db_qualification_identity_generation_t generation) {
    uint64_t hash = DB_FNV1A64_OFFSET;
    hash = db_fnv1a64_mix_u64(hash, generation.device_generation);
    hash = db_fnv1a64_mix_u64(hash, generation.implementation_generation);
    return db_fnv1a64_mix_u64(hash, generation.target_contract_generation);
}

static int
descriptor_strategy_matches_backend(db_probe_backend_t backend,
                                    db_render_target_strategy_t strategy) {
    switch (backend) {
    case DB_PROBE_BACKEND_GL1:
        return DB_BOOL((strategy == DB_RENDER_TARGET_GL1_DIRECT_WINDOW) ||
                       (strategy == DB_RENDER_TARGET_GL1_PERSISTENT_FBO) ||
                       (strategy == DB_RENDER_TARGET_GL1_CPU_UPLOAD));
    case DB_PROBE_BACKEND_GL3:
        return DB_BOOL(strategy == DB_RENDER_TARGET_GL3_PERSISTENT_FBO);
    case DB_PROBE_BACKEND_VULKAN:
        return DB_BOOL(strategy == DB_RENDER_TARGET_VULKAN_PERSISTENT_IMAGE);
    }
    return 0;
}

int db_qualification_descriptor_validate(
    const db_renderer_probe_descriptor_t *descriptor) {
    if ((descriptor == NULL) ||
        (descriptor->backend > DB_PROBE_BACKEND_VULKAN) ||
        (descriptor->implementation > DB_GRADIENT_IMPLEMENTATION_SEMANTIC) ||
        (descriptor->lane_index >= DB_QUALIFICATION_MAX_LANES) ||
        (descriptor->is_primary < 0) || (descriptor->is_primary > 1) ||
        (descriptor->working_format > DB_PIXEL_FORMAT_RGBA16F) ||
        (descriptor->logical_width == 0U) ||
        (descriptor->logical_width > INT32_MAX) ||
        (descriptor->logical_height == 0U) ||
        (descriptor->logical_height > INT32_MAX) ||
        (descriptor->compatibility_validated < 0) ||
        (descriptor->compatibility_validated > 1) ||
        (descriptor_strategy_matches_backend(descriptor->backend,
                                             descriptor->strategy) == 0)) {
        return 0;
    }
    return DB_BOOL((memchr(descriptor->driver.name, '\0',
                           sizeof(descriptor->driver.name)) != NULL) &&
                   (memchr(descriptor->driver.info, '\0',
                           sizeof(descriptor->driver.info)) != NULL) &&
                   (memchr(descriptor->provider, '\0',
                           sizeof(descriptor->provider)) != NULL));
}

int db_qualification_descriptor_store_append(
    db_renderer_qualification_descriptor_store_t *store,
    const db_renderer_probe_descriptor_t *descriptor) {
    if ((store == NULL) ||
        (db_qualification_descriptor_validate(descriptor) == 0) ||
        (store->count >= DB_QUALIFICATION_MAX_DESCRIPTORS)) {
        return 0;
    }
    store->descriptors[store->count] = *descriptor;
    store->count++;
    return 1;
}
