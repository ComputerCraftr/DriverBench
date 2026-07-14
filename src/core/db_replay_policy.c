#include "db_replay_policy.h"

#include <stddef.h>
#include <stdint.h>

#include "db_numeric.h"

void db_replay_policy_init(db_replay_policy_t *policy, uint32_t capacity) {
    if (policy == NULL) {
        return;
    }
    const uint32_t replay_capacity =
        (capacity >= 1U) ? DB_MIN(capacity, DB_REPLAY_CAPACITY_MAX)
                         : DB_REPLAY_CAPACITY_MAX;
    *policy = (db_replay_policy_t){.replay_capacity = replay_capacity};
}

db_replay_resolution_t db_replay_policy_resolve(db_replay_policy_t *policy,
                                                uint32_t age, int age_valid,
                                                int history_compatible) {
    if (policy == NULL) {
        return (db_replay_resolution_t){
            .use_rebuild = 1,
            .reason = "policy_unavailable",
        };
    }
    if (age > policy->maximum_observed_age) {
        policy->maximum_observed_age = age;
    }
    if ((age_valid == 0) || (age == 0U)) {
        return (db_replay_resolution_t){
            .use_rebuild = 1,
            .reason = "buffer_age_unavailable",
        };
    }
    const uint32_t required = age - 1U;
    if ((history_compatible == 0) || (required > policy->replay_capacity) ||
        (required > policy->retained_count)) {
        policy->rebuilds_due_to_insufficient_history++;
        return (db_replay_resolution_t){
            .use_rebuild = 1,
            .reason = (history_compatible == 0) ? "history_incompatible"
                                                : "history_insufficient",
        };
    }
    return (db_replay_resolution_t){
        .history_stream_count = required,
        .reason = "none",
    };
}

void db_replay_policy_commit(db_replay_policy_t *policy) {
    if ((policy != NULL) &&
        (policy->retained_count < policy->replay_capacity)) {
        policy->retained_count++;
    }
}

void db_replay_policy_reset(db_replay_policy_t *policy) {
    if (policy != NULL) {
        policy->retained_count = 0U;
    }
}
