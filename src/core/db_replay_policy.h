#ifndef DRIVERBENCH_CORE_DB_REPLAY_POLICY_H
#define DRIVERBENCH_CORE_DB_REPLAY_POLICY_H

#include <stdint.h>

enum { DB_REPLAY_CAPACITY_MAX = 8U };

typedef struct {
    uint32_t replay_capacity;
    uint32_t retained_count;
    uint32_t maximum_observed_age;
    uint32_t rebuilds_due_to_insufficient_history;
} db_replay_policy_t;

typedef struct {
    uint32_t history_stream_count;
    int use_rebuild;
    const char *reason;
} db_replay_resolution_t;

void db_replay_policy_init(db_replay_policy_t *policy, uint32_t capacity);
db_replay_resolution_t db_replay_policy_resolve(db_replay_policy_t *policy,
                                                uint32_t age, int age_valid,
                                                int history_compatible);
void db_replay_policy_commit(db_replay_policy_t *policy);
void db_replay_policy_reset(db_replay_policy_t *policy);

#endif
