#include "db_frame_preparation.h"

#include "core/db_frame_plan.h"
#include "core/db_hash.h"

#include <stdint.h>

uint64_t db_frame_preparation_token(const db_frame_preparation_t *preparation) {
    if (preparation == NULL) {
        return 0U;
    }
    uint64_t hash = DB_FNV1A64_OFFSET;
    hash = db_fnv1a64_mix_u64(hash, preparation->framebuffer_width);
    hash = db_fnv1a64_mix_u64(hash, preparation->framebuffer_height);
    hash = db_fnv1a64_mix_u64(hash, preparation->framebuffer_format);
    hash = db_fnv1a64_mix_u64(hash, preparation->framebuffer_generation);
    hash = db_fnv1a64_mix_u64(hash, preparation->raw_buffer_age);
    hash = db_fnv1a64_mix_u64(hash, preparation->replay_depth);
    hash = db_fnv1a64_mix_u64(hash, preparation->requirements_token);
    hash = db_fnv1a64_mix_u64(hash, preparation->checkpoint_binding_token);
    hash = db_fnv1a64_mix_u64(hash, (uint64_t)preparation->target_strategy);
    hash = db_fnv1a64_mix_u64(hash, (uint64_t)preparation->rebuild_reason);
    hash = db_fnv1a64_mix_u64(hash, (uint64_t)preparation->buffer_age_valid);
    hash = db_fnv1a64_mix_u64(hash, (uint64_t)preparation->conversion_required);
    return db_fnv1a64_mix_u64(hash, (uint64_t)preparation->force_rebuild);
}

int db_frame_preparation_matches(const db_frame_plan_t *plan,
                                 const db_frame_preparation_t *preparation) {
    return (plan != NULL) &&
           (plan->checkpoint_binding_token ==
            preparation->checkpoint_binding_token) &&
           (plan->preparation_token == db_frame_preparation_token(preparation));
}
