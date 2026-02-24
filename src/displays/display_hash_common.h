#ifndef DRIVERBENCH_DISPLAY_HASH_COMMON_H
#define DRIVERBENCH_DISPLAY_HASH_COMMON_H

#include <stdint.h>
#include <string.h>

#include "../core/db_core.h"
#include "../core/db_hash.h"

typedef struct {
    uint64_t aggregate_hash;
    int enabled;
    uint64_t final_hash;
    const char *hash_key;
    int report_aggregate;
    int report_final;
} db_display_hash_tracker_t;

typedef struct {
    int state_hash_enabled;
    int output_hash_enabled;
} db_display_hash_settings_t;

static inline db_display_hash_settings_t
db_display_resolve_hash_settings(int default_state_hash_enabled,
                                 int default_output_hash_enabled) {
    int state_hash_enabled = default_state_hash_enabled != 0;
    int output_hash_enabled = default_output_hash_enabled != 0;
    const char *hash_mode = db_runtime_option_get(DB_RUNTIME_OPT_HASH);
    if ((hash_mode == NULL) || (hash_mode[0] == '\0') ||
        (strcmp(hash_mode, "none") == 0)) {
        return (db_display_hash_settings_t){
            .state_hash_enabled = state_hash_enabled,
            .output_hash_enabled = output_hash_enabled,
        };
    }
    if (strcmp(hash_mode, "state") == 0) {
        state_hash_enabled = 1;
    } else if ((strcmp(hash_mode, "pixel") == 0) ||
               (strcmp(hash_mode, "output") == 0) ||
               (strcmp(hash_mode, "framebuffer") == 0)) {
        output_hash_enabled = 1;
    } else if (strcmp(hash_mode, "both") == 0) {
        state_hash_enabled = 1;
        output_hash_enabled = 1;
    } else {
        db_infof("display_hash_common",
                 "Invalid %s='%s'; using defaults (expected: "
                 "none|state|pixel|both)",
                 DB_RUNTIME_OPT_HASH, hash_mode);
    }
    return (db_display_hash_settings_t){
        .state_hash_enabled = state_hash_enabled,
        .output_hash_enabled = output_hash_enabled,
    };
}

static inline db_display_hash_tracker_t
db_display_hash_tracker_create(const char *backend, int enabled,
                               const char *hash_key) {
    db_display_hash_tracker_t tracker = {0};
    tracker.enabled = enabled;
    tracker.hash_key = hash_key;
    tracker.final_hash = 0U;
    tracker.aggregate_hash = DB_FNV1A64_OFFSET;
    tracker.report_final = 1;
    tracker.report_aggregate = 1;

    const char *report_mode = db_runtime_option_get(DB_RUNTIME_OPT_HASH_REPORT);
    if ((report_mode == NULL) || (report_mode[0] == '\0') ||
        (strcmp(report_mode, "both") == 0)) {
        return tracker;
    }
    if (strcmp(report_mode, "final") == 0) {
        tracker.report_aggregate = 0;
        return tracker;
    }
    if (strcmp(report_mode, "aggregate") == 0) {
        tracker.report_final = 0;
        return tracker;
    }
    if (backend != NULL) {
        db_infof(backend,
                 "Invalid %s='%s'; using hash report mode 'both' "
                 "(expected: final|aggregate|both)",
                 DB_RUNTIME_OPT_HASH_REPORT, report_mode);
    }
    return tracker;
}

static inline void
db_display_hash_tracker_record(db_display_hash_tracker_t *tracker,
                               uint64_t state_hash) {
    if ((tracker == NULL) || (tracker->enabled == 0)) {
        return;
    }
    tracker->final_hash = state_hash;
    tracker->aggregate_hash =
        db_fnv1a64_mix_u64(tracker->aggregate_hash, state_hash);
}

static inline void
db_display_hash_tracker_log_final(const char *backend,
                                  const db_display_hash_tracker_t *tracker) {
    if ((tracker == NULL) || (tracker->enabled == 0)) {
        return;
    }
    const char *key = (tracker->hash_key != NULL) ? tracker->hash_key : "hash";
    if ((tracker->report_final != 0) && (tracker->report_aggregate != 0)) {
        db_infof(backend, "%s_final=0x%016llx %s_aggregate=0x%016llx", key,
                 (unsigned long long)tracker->final_hash, key,
                 (unsigned long long)tracker->aggregate_hash);
        return;
    }
    if (tracker->report_final != 0) {
        db_infof(backend, "%s_final=0x%016llx", key,
                 (unsigned long long)tracker->final_hash);
    }
    if (tracker->report_aggregate != 0) {
        db_infof(backend, "%s_aggregate=0x%016llx", key,
                 (unsigned long long)tracker->aggregate_hash);
    }
}

#endif
