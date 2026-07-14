#include "db_conformance_service.h"

#include "db_byte_codec.h"
#include "db_conformance.h"
#include "db_conformance_cache.h"
#include "db_core.h"
#include "db_hash.h"
#include "db_poll_policy.h"
#include "db_probe_process.h"
#include "db_probe_protocol.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#define DB_CONFORMANCE_KEY_DOMAIN UINT32_C(0x514B4559)

enum {
    DB_CONFORMANCE_KEY_SCHEMA = 1U,
    DB_CONFORMANCE_KEY_FIXED_BYTES = 128U,
    DB_CONFORMANCE_KEY_WIRE_SCHEMA_OFFSET = 4U,
    DB_CONFORMANCE_KEY_SCHEMA_OFFSET = 8U,
    DB_CONFORMANCE_KEY_EVALUATOR_OFFSET = 12U,
    DB_CONFORMANCE_KEY_DOMAIN_OFFSET = 16U,
    DB_CONFORMANCE_KEY_BUILD_OFFSET = 20U,
    DB_CONFORMANCE_KEY_BACKEND_OFFSET = 24U,
    DB_CONFORMANCE_KEY_IMPLEMENTATION_OFFSET = 28U,
    DB_CONFORMANCE_KEY_FORMAT_OFFSET = 32U,
    DB_CONFORMANCE_KEY_VENDOR_OFFSET = 36U,
    DB_CONFORMANCE_KEY_DEVICE_OFFSET = 40U,
    DB_CONFORMANCE_KEY_DRIVER_OFFSET = 44U,
    DB_CONFORMANCE_KEY_API_OFFSET = 48U,
    DB_CONFORMANCE_KEY_WIDTH_OFFSET = 52U,
    DB_CONFORMANCE_KEY_HEIGHT_OFFSET = 56U,
    DB_CONFORMANCE_KEY_GRADIENT_WINDOW_OFFSET = 60U,
    DB_CONFORMANCE_KEY_IMPLEMENTATION_HASH_OFFSET = 64U,
    DB_CONFORMANCE_KEY_FLOAT_CONTROL_OFFSET = 72U,
    DB_CONFORMANCE_KEY_PALETTE_HASH_OFFSET = 80U,
    DB_CONFORMANCE_KEY_UUID_OFFSET = 88U,
};

static int copy_text(uint8_t *output, const char *text) {
    const size_t length = (text != NULL) ? strlen(text) : 0U;
    if (length >= DB_CONFORMANCE_KEY_TEXT_BYTES) {
        return 0;
    }
    memset(output, 0, DB_CONFORMANCE_KEY_TEXT_BYTES);
    for (size_t index = 0U; index < length; index++) {
        output[index] = (uint8_t)text[index];
    }
    return 1;
}

int db_conformance_key_serialize(
    const db_conformance_key_t *key,
    uint8_t output[DB_CONFORMANCE_KEY_WIRE_BYTES]) {
    if ((key == NULL) || (output == NULL)) {
        return 0;
    }
    memset(output, 0, DB_CONFORMANCE_KEY_WIRE_BYTES);
    output[0] = 'D';
    output[1] = 'B';
    output[2] = 'Q';
    output[3] = 'K';
    db_store_u32_le(&output[DB_CONFORMANCE_KEY_WIRE_SCHEMA_OFFSET],
                    DB_CONFORMANCE_KEY_SCHEMA);
    db_store_u32_le(&output[DB_CONFORMANCE_KEY_SCHEMA_OFFSET],
                    key->schema_version);
    db_store_u32_le(&output[DB_CONFORMANCE_KEY_EVALUATOR_OFFSET],
                    key->evaluator_version);
    db_store_u32_le(&output[DB_CONFORMANCE_KEY_DOMAIN_OFFSET],
                    key->domain_version);
    db_store_u32_le(&output[DB_CONFORMANCE_KEY_BUILD_OFFSET],
                    key->build_version);
    db_store_u32_le(&output[DB_CONFORMANCE_KEY_BACKEND_OFFSET],
                    (uint32_t)key->backend);
    db_store_u32_le(&output[DB_CONFORMANCE_KEY_IMPLEMENTATION_OFFSET],
                    (uint32_t)key->implementation);
    db_store_u32_le(&output[DB_CONFORMANCE_KEY_FORMAT_OFFSET],
                    (uint32_t)key->working_format);
    db_store_u32_le(&output[DB_CONFORMANCE_KEY_VENDOR_OFFSET], key->vendor_id);
    db_store_u32_le(&output[DB_CONFORMANCE_KEY_DEVICE_OFFSET], key->device_id);
    db_store_u32_le(&output[DB_CONFORMANCE_KEY_DRIVER_OFFSET], key->driver_id);
    db_store_u32_le(&output[DB_CONFORMANCE_KEY_API_OFFSET], key->api_version);
    db_store_u32_le(&output[DB_CONFORMANCE_KEY_WIDTH_OFFSET],
                    key->logical_width);
    db_store_u32_le(&output[DB_CONFORMANCE_KEY_HEIGHT_OFFSET],
                    key->logical_height);
    db_store_u32_le(&output[DB_CONFORMANCE_KEY_GRADIENT_WINDOW_OFFSET],
                    key->gradient_window_rows);
    db_store_u64_le(&output[DB_CONFORMANCE_KEY_IMPLEMENTATION_HASH_OFFSET],
                    key->implementation_hash);
    db_store_u64_le(&output[DB_CONFORMANCE_KEY_FLOAT_CONTROL_OFFSET],
                    key->float_control_signature);
    db_store_u64_le(&output[DB_CONFORMANCE_KEY_PALETTE_HASH_OFFSET],
                    key->palette_hash);
    memcpy(&output[DB_CONFORMANCE_KEY_UUID_OFFSET], key->device_uuid,
           DB_CONFORMANCE_UUID_BYTES);
    return copy_text(&output[DB_CONFORMANCE_KEY_FIXED_BYTES], key->provider) &&
           copy_text(&output[DB_CONFORMANCE_KEY_FIXED_BYTES +
                             DB_CONFORMANCE_KEY_TEXT_BYTES],
                     key->strategy) &&
           copy_text(&output[DB_CONFORMANCE_KEY_FIXED_BYTES +
                             (2U * DB_CONFORMANCE_KEY_TEXT_BYTES)],
                     key->driver_name) &&
           copy_text(&output[DB_CONFORMANCE_KEY_FIXED_BYTES +
                             (3U * DB_CONFORMANCE_KEY_TEXT_BYTES)],
                     key->driver_info);
}

static int make_directory(const char *path) {
    // NOLINTNEXTLINE(misc-include-cleaner) -- public <errno.h> ownership.
    if ((mkdir(path, S_IRWXU) == 0) || (errno == EEXIST)) {
        return 1;
    }
    return 0;
}

static int cache_directory(char *output, size_t output_size) {
    const char *const configured = getenv("DRIVERBENCH_PROBE_CACHE_DIR");
    if ((configured != NULL) && (configured[0] != '\0')) {
        return db_snprintf(output, output_size, "%s", configured) > 0;
    }
    const char *const home = getenv("HOME");
    if ((home == NULL) || (home[0] == '\0')) {
        return 0;
    }
#ifdef __APPLE__
    return db_snprintf(output, output_size,
                       "%s/Library/Caches/DriverBench/probes", home) > 0;
#else
    const char *const xdg = getenv("XDG_CACHE_HOME");
    if ((xdg != NULL) && (xdg[0] != '\0')) {
        return db_snprintf(output, output_size, "%s/driverbench/probes", xdg) >
               0;
    }
    return db_snprintf(output, output_size, "%s/.cache/driverbench/probes",
                       home) > 0;
#endif
}

static int make_directory_tree(char *path) {
    for (char *cursor = path + 1; *cursor != '\0'; cursor++) {
        if (*cursor != '/') {
            continue;
        }
        *cursor = '\0';
        const int created = make_directory(path);
        *cursor = '/';
        if (created == 0) {
            return 0;
        }
    }
    return make_directory(path);
}

int db_conformance_cache_path(uint64_t key_hash, char *output,
                              size_t output_size) {
    char directory[DB_CONFORMANCE_PATH_BYTES] = {0};
    if ((output == NULL) || (output_size == 0U) ||
        (cache_directory(directory, sizeof(directory)) == 0) ||
        (make_directory_tree(directory) == 0)) {
        return 0;
    }
    const int written = db_snprintf(output, output_size, "%s/%016llx.dbpc",
                                    directory, (unsigned long long)key_hash);
    return (written > 0) && ((size_t)written < output_size);
}

int db_probe_helper_default_path(char *output, size_t output_size) {
    if ((output == NULL) || (output_size == 0U)) {
        return 0;
    }
    char executable[DB_CONFORMANCE_PATH_BYTES] = {0};
#ifdef __APPLE__
    uint32_t size = (uint32_t)sizeof(executable);
    if (_NSGetExecutablePath(executable, &size) != 0) {
        return 0;
    }
#else
    const ssize_t size =
        readlink("/proc/self/exe", executable, sizeof(executable) - 1U);
    if (size <= 0) {
        return 0;
    }
    executable[(size_t)size] = '\0';
#endif
    char *const separator = strrchr(executable, '/');
    if (separator == NULL) {
        return 0;
    }
    separator[1] = '\0';
    const int written = db_snprintf(output, output_size,
                                    "%sdriverbench_probe_helper", executable);
    return (written > 0) && ((size_t)written < output_size);
}

static void set_reason(db_conformance_decision_t *decision,
                       const char *reason) {
    if ((decision != NULL) && (reason != NULL)) {
        (void)db_snprintf(decision->reason, sizeof(decision->reason), "%s",
                          reason);
    }
}

const char *db_qualification_outcome_name(db_qualification_outcome_t outcome) {
    switch (outcome) {
    case DB_QUALIFICATION_OUTCOME_CONFORMING:
        return "conforming";
    case DB_QUALIFICATION_OUTCOME_NONCONFORMING:
        return "nonconforming";
    case DB_QUALIFICATION_OUTCOME_UNAVAILABLE:
        return "unavailable";
    case DB_QUALIFICATION_OUTCOME_INTERNAL_ERROR:
        return "internal_error";
    }
    return "unknown";
}

static db_qualification_outcome_t
qualification_outcome(db_probe_status_t status,
                      db_conformance_result_t result) {
    if (status == DB_PROBE_STATUS_UNAVAILABLE) {
        return DB_QUALIFICATION_OUTCOME_UNAVAILABLE;
    }
    if (status != DB_PROBE_STATUS_OK) {
        return DB_QUALIFICATION_OUTCOME_INTERNAL_ERROR;
    }
    if (result == DB_CONFORMANCE_CONFORMING) {
        return DB_QUALIFICATION_OUTCOME_CONFORMING;
    }
    if (result == DB_CONFORMANCE_NONCONFORMING) {
        return DB_QUALIFICATION_OUTCOME_NONCONFORMING;
    }
    return DB_QUALIFICATION_OUTCOME_INTERNAL_ERROR;
}

db_conformance_decision_t
db_conformance_qualify(const db_conformance_key_t *key,
                       const db_conformance_query_t *query) {
    db_conformance_decision_t decision = {
        .outcome = DB_QUALIFICATION_OUTCOME_INTERNAL_ERROR,
        .result = DB_CONFORMANCE_UNTESTED,
        .source = DB_QUALIFICATION_SOURCE_NONE,
        .cache_status = DB_CONFORMANCE_CACHE_MISS,
        .probe_status = DB_PROBE_STATUS_UNAVAILABLE,
    };
    uint8_t serialized[DB_CONFORMANCE_KEY_WIRE_BYTES] = {0};
    if ((key == NULL) || (query == NULL) ||
        (db_conformance_key_serialize(key, serialized) == 0)) {
        set_reason(&decision, "invalid_key");
        return decision;
    }
    decision.key_hash =
        db_fnv1a64_tree(serialized, sizeof(serialized),
                        DB_CONFORMANCE_KEY_DOMAIN, DB_FNV1A64_OFFSET);
    if (query->diagnostic_forced != 0) {
        decision.source = DB_QUALIFICATION_SOURCE_DIAGNOSTIC;
        set_reason(&decision, "diagnostic_forced");
        return decision;
    }
    char cache_path[DB_CONFORMANCE_PATH_BYTES] = {0};
    const int have_cache = db_conformance_cache_path(
        decision.key_hash, cache_path, sizeof(cache_path));
    if ((query->ignore_cache == 0) && (query->rerun_probe == 0) && have_cache) {
        decision.cache_status = db_conformance_cache_read(
            cache_path, serialized, sizeof(serialized), &decision.result);
        if (decision.cache_status == DB_CONFORMANCE_CACHE_HIT) {
            decision.source = DB_QUALIFICATION_SOURCE_CACHE;
            decision.probe_status = DB_PROBE_STATUS_OK;
            decision.outcome =
                qualification_outcome(decision.probe_status, decision.result);
            set_reason(&decision, "cache_hit");
            return decision;
        }
    }
    char default_helper[DB_CONFORMANCE_PATH_BYTES] = {0};
    const char *helper = query->helper_path;
    if ((helper == NULL) || (helper[0] == '\0')) {
        if (db_probe_helper_default_path(default_helper,
                                         sizeof(default_helper)) == 0) {
            set_reason(&decision, "helper_path_unavailable");
            return decision;
        }
        helper = default_helper;
    }
    const db_probe_request_t request = {
        .request_id = decision.key_hash,
        .identity_hash = decision.key_hash,
        .backend = key->backend,
        .implementation = key->implementation,
        .working_format = key->working_format,
    };
    db_probe_request_t request_with_uuid = request;
    memcpy(request_with_uuid.device_uuid, key->device_uuid,
           DB_CONFORMANCE_UUID_BYTES);
    db_probe_result_t result = {0};
    if (query->timeout_ns != 0U) {
        const db_poll_policy_t *const reap =
            db_progress_policy_get(DB_PROGRESS_CONFORMANCE_REAP);
        decision.probe_status =
            (reap != NULL)
                ? db_probe_process_run_with_timeout(helper, &request_with_uuid,
                                                    &result, query->timeout_ns,
                                                    reap->total_timeout_ns)
                : DB_PROBE_STATUS_IO_ERROR;
    } else {
        decision.probe_status =
            db_probe_process_run(helper, &request_with_uuid, &result);
    }
    decision.outcome =
        qualification_outcome(decision.probe_status, result.result);
    if (decision.probe_status != DB_PROBE_STATUS_OK) {
        set_reason(&decision, db_probe_status_name(decision.probe_status));
        return decision;
    }
    decision.result = result.result;
    decision.outcome =
        qualification_outcome(decision.probe_status, decision.result);
    decision.source = DB_QUALIFICATION_SOURCE_HELPER;
    set_reason(&decision, "helper_result");
    if ((query->ignore_cache == 0) && have_cache &&
        ((decision.outcome == DB_QUALIFICATION_OUTCOME_CONFORMING) ||
         (decision.outcome == DB_QUALIFICATION_OUTCOME_NONCONFORMING))) {
        decision.cache_status = db_conformance_cache_write(
            cache_path, serialized, sizeof(serialized), decision.result);
    }
    return decision;
}

int db_conformance_qualify_batch(const db_conformance_key_t *keys,
                                 size_t key_count,
                                 const db_conformance_query_t *query,
                                 uint64_t aggregate_timeout_ns,
                                 db_conformance_decision_t *decisions) {
    const db_poll_policy_t *const helper_policy =
        db_progress_policy_get(DB_PROGRESS_CONFORMANCE_HELPER);
    if ((keys == NULL) || (query == NULL) || (decisions == NULL) ||
        (key_count == 0U) || (key_count > DB_CONFORMANCE_BATCH_MAX_KEYS) ||
        (aggregate_timeout_ns == 0U) || (helper_policy == NULL) ||
        (helper_policy->attempt_timeout_ns == 0U)) {
        return 0;
    }
    const db_deadline_t deadline =
        db_deadline_after(db_now_ns_monotonic(), aggregate_timeout_ns);
    uint8_t current[DB_CONFORMANCE_KEY_WIRE_BYTES] = {0};
    uint8_t previous[DB_CONFORMANCE_KEY_WIRE_BYTES] = {0};
    for (size_t index = 0U; index < key_count; index++) {
        if (db_conformance_key_serialize(&keys[index], current) == 0) {
            return 0;
        }
        int duplicate = 0;
        for (size_t prior = 0U; prior < index; prior++) {
            if ((db_conformance_key_serialize(&keys[prior], previous) != 0) &&
                (memcmp(current, previous, sizeof(current)) == 0)) {
                decisions[index] = decisions[prior];
                duplicate = 1;
                break;
            }
        }
        if (duplicate != 0) {
            continue;
        }
        const uint64_t now_ns = db_now_ns_monotonic();
        const uint64_t remaining_ns =
            db_deadline_remaining_ns(&deadline, now_ns);
        if (remaining_ns < helper_policy->attempt_timeout_ns) {
            decisions[index] = (db_conformance_decision_t){
                .outcome = DB_QUALIFICATION_OUTCOME_UNAVAILABLE,
                .result = DB_CONFORMANCE_UNTESTED,
                .probe_status = DB_PROBE_STATUS_UNAVAILABLE,
                .cache_status = DB_CONFORMANCE_CACHE_MISS,
            };
            set_reason(&decisions[index], "aggregate_deadline");
            continue;
        }
        db_conformance_query_t bounded_query = *query;
        bounded_query.timeout_ns = remaining_ns;
        decisions[index] = db_conformance_qualify(&keys[index], &bounded_query);
    }
    return 1;
}
