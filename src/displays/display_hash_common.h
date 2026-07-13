#ifndef DRIVERBENCH_DISPLAY_HASH_COMMON_H
#define DRIVERBENCH_DISPLAY_HASH_COMMON_H

#include <stdint.h>
#include <string.h>

#include "../config/runtime_options.h"
#include "../core/db_core.h"
#include "../core/db_hash.h"

typedef struct {
    uint64_t aggregate_hash;
    int enabled;
    uint64_t final_hash;
    const char *hash_key;
    const char *hash_algorithm;
    int report_aggregate;
    int report_final;
} db_display_hash_tracker_t;

typedef struct {
    int state_hash_enabled;
    int output_hash_enabled;
} db_display_hash_settings_t;

#define DB_DISPLAY_HASH_KEY_STATE "state_hash"
#define DB_DISPLAY_HASH_KEY_FBO "fbo_hash"
#define DB_DISPLAY_HASH_KEY_FRAMEBUFFER "framebuffer_hash"
#define DB_DISPLAY_HASH_KEY_BO "bo_hash"
#define DB_DISPLAY_HASH_KEY_WORKING "working_hash"

static inline db_display_hash_settings_t
db_display_resolve_hash_settings(int default_state_hash_enabled,
                                 int default_output_hash_enabled,
                                 const char *hash_mode) {
    int state_hash_enabled = DB_BOOL(default_state_hash_enabled);
    int output_hash_enabled = DB_BOOL(default_output_hash_enabled);
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
        DB_RUNTIME_STATUS("display_hash_common",
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
                               const char *hash_key,
                               const char *hash_report_mode) {
    db_display_hash_tracker_t tracker = {0};
    tracker.enabled = enabled;
    tracker.hash_key = hash_key;
    tracker.hash_algorithm =
        ((hash_key != NULL) &&
         (strcmp(hash_key, DB_DISPLAY_HASH_KEY_STATE) == 0))
            ? DB_FNV1A64_SERIAL_ALGORITHM
            : DB_FNV1A64_TREE_ALGORITHM;
    tracker.final_hash = 0U;
    tracker.aggregate_hash = DB_FNV1A64_OFFSET;
    tracker.report_final = 1;
    tracker.report_aggregate = 1;

    const char *report_mode = hash_report_mode;
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
        DB_RUNTIME_STATUS(backend,
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
    char final_key[64] = {0};
    char aggregate_key[64] = {0};
    (void)db_snprintf(final_key, sizeof(final_key), "%s_final", key);
    (void)db_snprintf(aggregate_key, sizeof(aggregate_key), "%s_aggregate",
                      key);
    db_log_field_t fields[5] = {
        DB_LOG_TOKEN("kind", key),
        DB_LOG_TOKEN("hash_algorithm", tracker->hash_algorithm),
        DB_LOG_TOKEN("aggregate_algorithm", DB_FNV1A64_SERIAL_ALGORITHM),
    };
    size_t field_count = 3U;
    if (tracker->report_final != 0) {
        fields[field_count++] = DB_LOG_HEX64(final_key, tracker->final_hash);
    }
    if (tracker->report_aggregate != 0) {
        fields[field_count++] =
            DB_LOG_HEX64(aggregate_key, tracker->aggregate_hash);
    }
    db_log_info(backend, "hash_result", fields, field_count);
}

#endif
