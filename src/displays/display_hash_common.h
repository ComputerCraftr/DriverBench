#ifndef DRIVERBENCH_DISPLAY_HASH_COMMON_H
#define DRIVERBENCH_DISPLAY_HASH_COMMON_H

#include <stdint.h>

#include "../core/db_core.h"

typedef struct {
    int enabled;
    int log_each_frame;
    const char *hash_key;
    uint64_t final_hash;
    uint64_t aggregate_hash;
} db_display_hash_tracker_t;

static inline db_display_hash_tracker_t
db_display_hash_tracker_create(int enabled, int log_each_frame,
                               const char *hash_key) {
    db_display_hash_tracker_t tracker = {0};
    tracker.enabled = enabled;
    tracker.log_each_frame = log_each_frame;
    tracker.hash_key = hash_key;
    tracker.final_hash = 0U;
    tracker.aggregate_hash = DB_FNV1A64_OFFSET;
    return tracker;
}

static inline void
db_display_hash_tracker_record(const char *backend,
                               db_display_hash_tracker_t *tracker,
                               uint64_t frame_index, uint64_t frame_hash) {
    if ((tracker == NULL) || (tracker->enabled == 0)) {
        return;
    }
    tracker->final_hash = frame_hash;
    tracker->aggregate_hash =
        db_fnv1a64_mix_u64(tracker->aggregate_hash, frame_hash);
    if (tracker->log_each_frame != 0) {
        db_infof(backend, "frame=%llu %s=0x%016llx",
                 (unsigned long long)frame_index,
                 (tracker->hash_key != NULL) ? tracker->hash_key : "hash",
                 (unsigned long long)frame_hash);
    }
}

static inline void
db_display_hash_tracker_log_final(const char *backend,
                                  const db_display_hash_tracker_t *tracker) {
    if ((tracker == NULL) || (tracker->enabled == 0)) {
        return;
    }
    const char *key = (tracker->hash_key != NULL) ? tracker->hash_key : "hash";
    db_infof(backend, "%s_final=0x%016llx %s_aggregate=0x%016llx", key,
             (unsigned long long)tracker->final_hash, key,
             (unsigned long long)tracker->aggregate_hash);
}

#endif
