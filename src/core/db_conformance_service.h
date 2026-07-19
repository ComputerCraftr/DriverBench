#ifndef DRIVERBENCH_CORE_DB_CONFORMANCE_SERVICE_H
#define DRIVERBENCH_CORE_DB_CONFORMANCE_SERVICE_H

#include "db_conformance.h"
#include "db_probe_protocol.h"
#include "db_qualification_contracts.h"

#include <stddef.h>
#include <stdint.h>

#define DB_CONFORMANCE_KEY_TEXT_BYTES 96U
#define DB_CONFORMANCE_KEY_WIRE_BYTES 512U
#define DB_CONFORMANCE_PATH_BYTES 4096U
#define DB_CONFORMANCE_REASON_BYTES 48U
#define DB_CONFORMANCE_BATCH_MAX_KEYS 24U
#define DB_CONFORMANCE_TOPOLOGY_MAX_LANES 8U
#define DB_CONFORMANCE_IMPLEMENTATIONS_PER_LANE 3U

typedef enum {
    DB_QUALIFICATION_SEMANTIC_INDEX = 0,
    DB_QUALIFICATION_EXACT_LOOKUP_INDEX,
    DB_QUALIFICATION_ROW_INSTANCES_INDEX,
} db_qualification_implementation_index_t;

#define DB_QUALIFICATION_IMPLEMENTATION_BIT(index) (UINT32_C(1) << (index))
#define DB_QUALIFICATION_ALL_IMPLEMENTATIONS_MASK                              \
    ((UINT32_C(1) << DB_CONFORMANCE_IMPLEMENTATIONS_PER_LANE) - UINT32_C(1))

typedef struct {
    uint32_t schema_version;
    uint32_t evaluator_version;
    uint32_t domain_version;
    uint32_t build_version;
    db_probe_backend_t backend;
    db_gradient_implementation_t implementation;
    db_pixel_format_t working_format;
    uint32_t vendor_id;
    uint32_t device_id;
    uint32_t driver_id;
    uint32_t api_version;
    uint32_t logical_width;
    uint32_t logical_height;
    uint32_t gradient_window_rows;
    uint64_t implementation_hash;
    uint64_t float_control_signature;
    uint64_t palette_hash;
    uint8_t device_uuid[DB_CONFORMANCE_UUID_BYTES];
    char provider[DB_CONFORMANCE_KEY_TEXT_BYTES];
    char strategy[DB_CONFORMANCE_KEY_TEXT_BYTES];
    char driver_name[DB_CONFORMANCE_KEY_TEXT_BYTES];
    char driver_info[DB_CONFORMANCE_KEY_TEXT_BYTES];
} db_conformance_key_t;

typedef struct {
    const char *helper_path;
    uint64_t timeout_ns;
    int ignore_cache;
    int rerun_probe;
    int diagnostic_forced;
} db_conformance_query_t;

typedef struct {
    db_qualification_outcome_t outcome;
    db_conformance_result_t result;
    db_qualification_source_t source;
    db_conformance_cache_status_t cache_status;
    db_probe_status_t probe_status;
    uint64_t key_hash;
    char reason[DB_CONFORMANCE_REASON_BYTES];
} db_conformance_decision_t;

typedef struct {
    db_conformance_key_t keys[DB_CONFORMANCE_TOPOLOGY_MAX_LANES]
                             [DB_CONFORMANCE_IMPLEMENTATIONS_PER_LANE];
    db_lane_qualification_t lanes[DB_CONFORMANCE_TOPOLOGY_MAX_LANES];
    uint32_t supported_implementation_mask;
    size_t lane_count;
} db_qualification_topology_request_t;

typedef struct {
    db_conformance_decision_t
        decisions[DB_CONFORMANCE_TOPOLOGY_MAX_LANES]
                 [DB_CONFORMANCE_IMPLEMENTATIONS_PER_LANE];
    db_lane_qualification_t lanes[DB_CONFORMANCE_TOPOLOGY_MAX_LANES];
    db_topology_qualification_t topology;
    db_qualification_source_t source;
    db_conformance_cache_status_t cache_status;
} db_qualification_topology_result_t;

const char *db_qualification_outcome_name(db_qualification_outcome_t outcome);

int db_conformance_key_serialize(const db_conformance_key_t *key,
                                 uint8_t output[DB_CONFORMANCE_KEY_WIRE_BYTES]);
int db_conformance_cache_path(uint64_t key_hash, char *output,
                              size_t output_size);
int db_probe_helper_default_path(char *output, size_t output_size);
db_conformance_decision_t
db_conformance_qualify(const db_conformance_key_t *key,
                       const db_conformance_query_t *query);
int db_conformance_qualify_batch(const db_conformance_key_t *keys,
                                 size_t key_count,
                                 const db_conformance_query_t *query,
                                 uint64_t aggregate_timeout_ns,
                                 db_conformance_decision_t *decisions);
int db_qualification_service_resolve_topology(
    const db_qualification_topology_request_t *request,
    const db_conformance_query_t *query, uint64_t aggregate_timeout_ns,
    db_qualification_topology_result_t *result);
int db_qualification_service_resolve_descriptors(
    const db_renderer_qualification_descriptor_store_t *store,
    const db_conformance_query_t *query, uint64_t aggregate_timeout_ns,
    uint64_t unavailable_candidate_mask, db_qualification_snapshot_t *snapshot);

#endif
