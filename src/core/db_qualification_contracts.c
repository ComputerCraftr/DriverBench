#include "db_qualification_contracts.h"

#include "db_hash.h"

#include <stdint.h>

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

int db_qualification_descriptor_store_append(
    db_renderer_qualification_descriptor_store_t *store,
    const db_renderer_probe_descriptor_t *descriptor) {
    if ((store == NULL) || (descriptor == NULL) ||
        (store->count >= DB_QUALIFICATION_MAX_DESCRIPTORS)) {
        return 0;
    }
    store->descriptors[store->count] = *descriptor;
    store->count++;
    return 1;
}
