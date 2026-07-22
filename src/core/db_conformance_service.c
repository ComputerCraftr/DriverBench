#include "db_conformance_service.h"

#include "db_byte_codec.h"
#include "db_conformance.h"
#include "db_conformance_cache.h"
#include "db_core.h"
#include "db_hash.h"
#include "db_probe_process.h"
#include "db_probe_protocol.h"
#include "db_progress_policy.h"
#include "db_qualification_contracts.h"
#include "db_render_result.h"

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
#define DB_QUALIFICATION_DESCRIPTOR_DOMAIN UINT32_C(0x51444553)

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
    DB_QUALIFICATION_COMMON_SCHEMA_VERSION = 1U,
    DB_QUALIFICATION_COMMON_EVALUATOR_VERSION = 3U,
    DB_QUALIFICATION_COMMON_DOMAIN_VERSION = 1U,
    DB_QUALIFICATION_COMMON_BUILD_VERSION = 1U,
    DB_QUALIFICATION_COMMON_GRADIENT_WINDOW_ROWS = 32U,
    DB_CONFORMANCE_BATCH_DEDUP_SLOTS = 64U,
};

typedef struct {
    uint64_t hash;
    size_t key_index;
    int occupied;
} db_conformance_dedup_slot_t;

_Static_assert((DB_CONFORMANCE_BATCH_DEDUP_SLOTS &
                (DB_CONFORMANCE_BATCH_DEDUP_SLOTS - 1U)) == 0U,
               "conformance dedup table size must be a power of two");
_Static_assert(DB_CONFORMANCE_BATCH_DEDUP_SLOTS > DB_CONFORMANCE_BATCH_MAX_KEYS,
               "conformance dedup table must retain an empty slot");

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
    const char *const configured = getenv(DB_PROBE_ENV_CACHE_DIR);
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
        decision.probe_status = db_probe_process_run_with_timeout(
            helper, &request_with_uuid, &result, query->timeout_ns, 0U);
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
    if ((keys == NULL) || (query == NULL) || (decisions == NULL) ||
        (key_count == 0U) || (key_count > DB_CONFORMANCE_BATCH_MAX_KEYS) ||
        (aggregate_timeout_ns == 0U)) {
        return 0;
    }
    const db_deadline_t deadline =
        db_deadline_after(db_now_ns_monotonic(), aggregate_timeout_ns);
    uint8_t current[DB_CONFORMANCE_KEY_WIRE_BYTES] = {0};
    uint8_t previous[DB_CONFORMANCE_KEY_WIRE_BYTES] = {0};
    db_conformance_dedup_slot_t dedup[DB_CONFORMANCE_BATCH_DEDUP_SLOTS] = {0};
    for (size_t index = 0U; index < key_count; index++) {
        if (db_conformance_key_serialize(&keys[index], current) == 0) {
            return 0;
        }
        const uint64_t key_hash = db_fnv1a64_bytes(current, sizeof(current));
        size_t slot =
            (size_t)(key_hash & (DB_CONFORMANCE_BATCH_DEDUP_SLOTS - 1U));
        int duplicate = 0;
        for (size_t probe = 0U; probe < DB_CONFORMANCE_BATCH_DEDUP_SLOTS;
             probe++) {
            db_conformance_dedup_slot_t *const entry = &dedup[slot];
            if (entry->occupied == 0) {
                *entry = (db_conformance_dedup_slot_t){
                    .hash = key_hash, .key_index = index, .occupied = 1};
                break;
            }
            if ((entry->hash == key_hash) &&
                (db_conformance_key_serialize(&keys[entry->key_index],
                                              previous) != 0) &&
                (memcmp(current, previous, sizeof(current)) == 0)) {
                decisions[index] = decisions[entry->key_index];
                duplicate = 1;
                break;
            }
            slot = (slot + 1U) & (DB_CONFORMANCE_BATCH_DEDUP_SLOTS - 1U);
        }
        if (duplicate != 0) {
            continue;
        }
        const uint64_t now_ns = db_now_ns_monotonic();
        const uint64_t remaining_ns =
            db_deadline_remaining_ns(&deadline, now_ns);
        if (db_progress_policy_allows_start(DB_PROGRESS_CONFORMANCE_HELPER,
                                            remaining_ns) == 0) {
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

int db_qualification_service_resolve_topology(
    const db_qualification_topology_request_t *request,
    const db_conformance_query_t *query, uint64_t aggregate_timeout_ns,
    db_qualification_service_workspace_t *workspace,
    db_qualification_topology_result_t *result) {
    if ((request == NULL) || (query == NULL) || (workspace == NULL) ||
        (result == NULL) || (request->lane_count == 0U) ||
        (request->lane_count > DB_CONFORMANCE_TOPOLOGY_MAX_LANES)) {
        return 0;
    }
    *workspace = (db_qualification_service_workspace_t){0};
    const uint32_t supported_mask =
        (request->supported_implementation_mask != 0U)
            ? request->supported_implementation_mask
            : DB_QUALIFICATION_ALL_IMPLEMENTATIONS_MASK;
    size_t compact_count = 0U;
    for (size_t lane_index = 0U; lane_index < request->lane_count;
         lane_index++) {
        for (size_t implementation_index = 0U;
             implementation_index < DB_CONFORMANCE_IMPLEMENTATIONS_PER_LANE;
             implementation_index++) {
            if ((supported_mask & DB_QUALIFICATION_IMPLEMENTATION_BIT(
                                      implementation_index)) == 0U) {
                continue;
            }
            workspace->keys[compact_count] =
                request->keys[lane_index][implementation_index];
            workspace->topology_lanes[compact_count] = lane_index;
            workspace->topology_implementations[compact_count] =
                implementation_index;
            compact_count++;
        }
    }
    if ((compact_count == 0U) ||
        (db_conformance_qualify_batch(workspace->keys, compact_count, query,
                                      aggregate_timeout_ns,
                                      workspace->compact) == 0)) {
        return 0;
    }
    *result = (db_qualification_topology_result_t){0};
    for (size_t compact_index = 0U; compact_index < compact_count;
         compact_index++) {
        result->decisions[workspace->topology_lanes[compact_index]]
                         [workspace->topology_implementations[compact_index]] =
            workspace->compact[compact_index];
    }
    result->source = DB_QUALIFICATION_SOURCE_NONE;
    result->cache_status = DB_CONFORMANCE_CACHE_MISS;
    for (size_t lane_index = 0U; lane_index < request->lane_count;
         lane_index++) {
        result->lanes[lane_index] = request->lanes[lane_index];
        const db_conformance_decision_t *const decisions =
            result->decisions[lane_index];
        result->lanes[lane_index].semantic =
            decisions[DB_QUALIFICATION_SEMANTIC_INDEX].result;
        result->lanes[lane_index].exact_lookup =
            decisions[DB_QUALIFICATION_EXACT_LOOKUP_INDEX].result;
        result->lanes[lane_index].row_instances =
            decisions[DB_QUALIFICATION_ROW_INSTANCES_INDEX].result;
        for (size_t implementation_index = 0U;
             implementation_index < DB_CONFORMANCE_IMPLEMENTATIONS_PER_LANE;
             implementation_index++) {
            const db_conformance_decision_t *const decision =
                &decisions[implementation_index];
            if (decision->source == DB_QUALIFICATION_SOURCE_HELPER) {
                result->source = DB_QUALIFICATION_SOURCE_HELPER;
            } else if ((result->source == DB_QUALIFICATION_SOURCE_NONE) &&
                       (decision->source == DB_QUALIFICATION_SOURCE_CACHE)) {
                result->source = DB_QUALIFICATION_SOURCE_CACHE;
            }
            if (decision->cache_status == DB_CONFORMANCE_CACHE_HIT) {
                result->cache_status = DB_CONFORMANCE_CACHE_HIT;
            }
        }
    }
    result->topology =
        db_topology_qualification_reduce(result->lanes, request->lane_count);
    return 1;
}

static db_conformance_key_t
descriptor_key(const db_renderer_probe_descriptor_t *descriptor) {
    db_conformance_key_t key = {
        .schema_version = DB_QUALIFICATION_COMMON_SCHEMA_VERSION,
        .evaluator_version = DB_QUALIFICATION_COMMON_EVALUATOR_VERSION,
        .domain_version = DB_QUALIFICATION_COMMON_DOMAIN_VERSION,
        .build_version = DB_QUALIFICATION_COMMON_BUILD_VERSION,
        .backend = descriptor->backend,
        .implementation = descriptor->implementation,
        .working_format = descriptor->working_format,
        .vendor_id = descriptor->device.vendor_id,
        .device_id = descriptor->device.device_id,
        .driver_id = descriptor->driver.driver_id,
        .api_version = descriptor->driver.api_version,
        .logical_width = descriptor->logical_width,
        .logical_height = descriptor->logical_height,
        .gradient_window_rows = DB_QUALIFICATION_COMMON_GRADIENT_WINDOW_ROWS,
        .implementation_hash = descriptor->implementation_hash,
        .float_control_signature = descriptor->float_controls.value,
        .palette_hash = descriptor->palette_hash,
    };
    memcpy(key.device_uuid, descriptor->device.uuid, sizeof(key.device_uuid));
    (void)db_snprintf(key.provider, sizeof(key.provider), "%s",
                      descriptor->provider);
    (void)db_snprintf(key.strategy, sizeof(key.strategy), "%s",
                      db_render_target_strategy_name(descriptor->strategy));
    (void)db_snprintf(key.driver_name, sizeof(key.driver_name), "%s",
                      descriptor->driver.name);
    (void)db_snprintf(key.driver_info, sizeof(key.driver_info), "%s",
                      descriptor->driver.info);
    return key;
}

static uint64_t
descriptor_identity(const db_renderer_qualification_descriptor_store_t *store) {
    uint64_t hash = db_qualification_generation_hash(store->generation);
    for (size_t index = 0U; index < store->count; index++) {
        const db_renderer_probe_descriptor_t *const descriptor =
            &store->descriptors[index];
        hash = db_fnv1a64_mix_u64(hash, (uint64_t)descriptor->backend);
        hash = db_fnv1a64_mix_u64(hash, (uint64_t)descriptor->strategy);
        hash = db_fnv1a64_mix_u64(hash, (uint64_t)descriptor->implementation);
        hash = db_fnv1a64_mix_u64(hash, descriptor->lane_index);
        hash = db_fnv1a64_mix_u64(hash, descriptor->implementation_hash);
        hash = db_fnv1a64_mix_u64(hash, descriptor->capability_hash);
    }
    return db_fnv1a64_mix_u64(hash, DB_QUALIFICATION_DESCRIPTOR_DOMAIN);
}

static uint64_t
qualification_candidate_id(db_render_target_strategy_t strategy,
                           db_gradient_implementation_t implementation) {
    return ((uint64_t)strategy * DB_CONFORMANCE_IMPLEMENTATIONS_PER_LANE) +
           (uint64_t)implementation;
}

static int candidate_is_unavailable(uint64_t candidate_id,
                                    uint64_t unavailable_mask) {
    return (candidate_id >= 64U) ||
           ((unavailable_mask & (UINT64_C(1) << candidate_id)) != 0U);
}

static uint32_t lane_mask_count(uint32_t mask) {
    uint32_t count = 0U;
    while (mask != 0U) {
        count += mask & UINT32_C(1);
        mask >>= 1U;
    }
    return count;
}

static const db_gradient_implementation_t gl1_policy[] = {
    DB_GRADIENT_IMPLEMENTATION_SEMANTIC,
    DB_GRADIENT_IMPLEMENTATION_ROW_INSTANCES,
};

static const db_gradient_implementation_t native_policy[] = {
    DB_GRADIENT_IMPLEMENTATION_SEMANTIC,
    DB_GRADIENT_IMPLEMENTATION_EXACT_LOOKUP,
    DB_GRADIENT_IMPLEMENTATION_ROW_INSTANCES,
};

static int
descriptor_policy(db_probe_backend_t backend,
                  const db_gradient_implementation_t **implementations,
                  size_t *count) {
    if ((implementations == NULL) || (count == NULL)) {
        return 0;
    }
    switch (backend) {
    case DB_PROBE_BACKEND_GL1:
        *implementations = gl1_policy;
        *count = sizeof(gl1_policy) / sizeof(gl1_policy[0]);
        return 1;
    case DB_PROBE_BACKEND_GL3:
    case DB_PROBE_BACKEND_VULKAN:
        *implementations = native_policy;
        *count = sizeof(native_policy) / sizeof(native_policy[0]);
        return 1;
    }
    return 0;
}

static void set_snapshot_reason(db_qualification_snapshot_t *snapshot,
                                const char *reason) {
    if ((snapshot != NULL) && (reason != NULL)) {
        (void)db_snprintf(snapshot->reason, sizeof(snapshot->reason), "%s",
                          reason);
    }
}

static void combine_decision_metadata(const db_conformance_decision_t *decision,
                                      db_qualification_snapshot_t *snapshot) {
    if ((decision == NULL) || (snapshot == NULL)) {
        return;
    }
    if (decision->source == DB_QUALIFICATION_SOURCE_HELPER) {
        snapshot->source = DB_QUALIFICATION_SOURCE_HELPER;
    } else if (snapshot->source == DB_QUALIFICATION_SOURCE_NONE) {
        if (decision->source == DB_QUALIFICATION_SOURCE_CACHE) {
            snapshot->source = DB_QUALIFICATION_SOURCE_CACHE;
        } else if (decision->source == DB_QUALIFICATION_SOURCE_BASELINE) {
            snapshot->source = DB_QUALIFICATION_SOURCE_BASELINE;
        }
    }
    if (decision->cache_status == DB_CONFORMANCE_CACHE_HIT) {
        snapshot->cache_status = DB_CONFORMANCE_CACHE_HIT;
    }
}

int db_qualification_service_resolve_descriptors(
    const db_renderer_qualification_descriptor_store_t *store,
    const db_conformance_query_t *query, uint64_t aggregate_timeout_ns,
    uint64_t unavailable_candidate_mask,
    db_qualification_service_workspace_t *workspace,
    db_qualification_snapshot_t *snapshot) {
    if ((store == NULL) || (query == NULL) || (workspace == NULL) ||
        (snapshot == NULL) || (store->count == 0U) ||
        (store->count > DB_QUALIFICATION_MAX_DESCRIPTORS)) {
        return 0;
    }
    for (size_t index = 0U; index < store->count; index++) {
        if (db_qualification_descriptor_validate(&store->descriptors[index]) ==
            0) {
            return 0;
        }
    }
    *snapshot = (db_qualification_snapshot_t){
        .generation = db_qualification_generation_hash(store->generation),
        .descriptor_identity = descriptor_identity(store),
        .outcome = DB_QUALIFICATION_OUTCOME_UNAVAILABLE,
        .source = DB_QUALIFICATION_SOURCE_NONE,
        .cache_status = DB_CONFORMANCE_CACHE_MISS,
    };
    if (query->diagnostic_forced != 0) {
        const db_renderer_probe_descriptor_t *const descriptor =
            &store->descriptors[0];
        snapshot->candidate_id = qualification_candidate_id(
            descriptor->strategy, descriptor->implementation);
        snapshot->implementation = descriptor->implementation;
        snapshot->retained_lanes = UINT32_C(1) << descriptor->lane_index;
        snapshot->lane_count = 1U;
        snapshot->strategy = descriptor->strategy;
        snapshot->source = DB_QUALIFICATION_SOURCE_DIAGNOSTIC;
        snapshot->diagnostic_forced = 1;
        set_snapshot_reason(snapshot, "diagnostic_forced");
        return 1;
    }

    *workspace = (db_qualification_service_workspace_t){0};
    size_t key_count = 0U;
    for (size_t index = 0U; index < store->count; index++) {
        const db_renderer_probe_descriptor_t *const descriptor =
            &store->descriptors[index];
        if (descriptor->compatibility_validated != 0) {
            workspace->decisions[index] = (db_conformance_decision_t){
                .outcome = DB_QUALIFICATION_OUTCOME_CONFORMING,
                .result = DB_CONFORMANCE_CONFORMING,
                .source = DB_QUALIFICATION_SOURCE_BASELINE,
                .cache_status = DB_CONFORMANCE_CACHE_MISS,
                .probe_status = DB_PROBE_STATUS_OK,
            };
            set_reason(&workspace->decisions[index], "validated_compatibility");
            continue;
        }
        workspace->keys[key_count] = descriptor_key(descriptor);
        workspace->decision_indices[key_count] = index;
        key_count++;
    }
    if (key_count != 0U) {
        if (db_conformance_qualify_batch(workspace->keys, key_count, query,
                                         aggregate_timeout_ns,
                                         workspace->compact) == 0) {
            set_snapshot_reason(snapshot, "qualification_batch_failed");
            return 1;
        }
        for (size_t index = 0U; index < key_count; index++) {
            workspace->decisions[workspace->decision_indices[index]] =
                workspace->compact[index];
        }
    }

    const db_gradient_implementation_t *policy = NULL;
    size_t policy_count = 0U;
    if (descriptor_policy(store->descriptors[0].backend, &policy,
                          &policy_count) == 0) {
        set_snapshot_reason(snapshot, "backend_policy_unavailable");
        return 1;
    }
    for (size_t policy_index = 0U; policy_index < policy_count;
         policy_index++) {
        const db_gradient_implementation_t implementation =
            policy[policy_index];
        for (size_t descriptor_index = 0U; descriptor_index < store->count;
             descriptor_index++) {
            const db_renderer_probe_descriptor_t *const first =
                &store->descriptors[descriptor_index];
            if (first->implementation != implementation) {
                continue;
            }
            const uint64_t candidate_id =
                qualification_candidate_id(first->strategy, implementation);
            if (candidate_is_unavailable(candidate_id,
                                         unavailable_candidate_mask)) {
                continue;
            }
            uint32_t retained_mask = 0U;
            uint32_t primary_mask = 0U;
            int all_conforming = 1;
            int primary_conforming = 0;
            for (size_t lane_index = 0U; lane_index < store->count;
                 lane_index++) {
                const db_renderer_probe_descriptor_t *const descriptor =
                    &store->descriptors[lane_index];
                if ((descriptor->strategy != first->strategy) ||
                    (descriptor->implementation != implementation)) {
                    continue;
                }
                const uint32_t lane_bit = UINT32_C(1) << descriptor->lane_index;
                if (descriptor->is_primary != 0) {
                    primary_mask |= lane_bit;
                }
                if ((workspace->decisions[lane_index].outcome ==
                     DB_QUALIFICATION_OUTCOME_CONFORMING) ||
                    (descriptor->compatibility_validated != 0)) {
                    retained_mask |= lane_bit;
                    if (descriptor->is_primary != 0) {
                        primary_conforming = 1;
                    }
                } else {
                    all_conforming = 0;
                }
                combine_decision_metadata(&workspace->decisions[lane_index],
                                          snapshot);
            }
            if ((retained_mask == 0U) || (primary_mask == 0U) ||
                (primary_conforming == 0)) {
                continue;
            }
            if ((all_conforming == 0) &&
                (implementation != DB_GRADIENT_IMPLEMENTATION_ROW_INSTANCES)) {
                continue;
            }
            snapshot->candidate_id = candidate_id;
            snapshot->outcome = DB_QUALIFICATION_OUTCOME_CONFORMING;
            snapshot->implementation = implementation;
            snapshot->retained_lanes = retained_mask;
            snapshot->lane_count = lane_mask_count(retained_mask);
            snapshot->strategy = first->strategy;
            snapshot->production_qualified = 1;
            set_snapshot_reason(snapshot, (all_conforming != 0)
                                              ? "all_lanes_conforming"
                                              : "secondary_lanes_removed");
            return 1;
        }
    }
    set_snapshot_reason(snapshot, "no_conforming_candidate");
    return 1;
}
