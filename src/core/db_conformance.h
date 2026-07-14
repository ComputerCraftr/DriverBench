#ifndef DRIVERBENCH_CORE_DB_CONFORMANCE_H
#define DRIVERBENCH_CORE_DB_CONFORMANCE_H

#include "db_conformance_cache.h"

#include <stddef.h>
#include <stdint.h>

#define DB_CONFORMANCE_UUID_BYTES 16U

typedef enum {
    DB_GRADIENT_IMPLEMENTATION_ROW_INSTANCES = 0,
    DB_GRADIENT_IMPLEMENTATION_EXACT_LOOKUP,
    DB_GRADIENT_IMPLEMENTATION_SEMANTIC,
} db_gradient_implementation_t;

typedef enum {
    DB_QUALIFICATION_SOURCE_NONE = 0,
    DB_QUALIFICATION_SOURCE_BASELINE,
    DB_QUALIFICATION_SOURCE_CACHE,
    DB_QUALIFICATION_SOURCE_HELPER,
    DB_QUALIFICATION_SOURCE_DIAGNOSTIC,
} db_qualification_source_t;

typedef struct {
    uint8_t device_uuid[DB_CONFORMANCE_UUID_BYTES];
    uint32_t vendor_id;
    uint32_t device_id;
    uint32_t driver_id;
    int is_primary;
    db_conformance_result_t semantic;
    db_conformance_result_t exact_lookup;
    db_conformance_result_t row_instances;
} db_lane_qualification_t;

typedef struct {
    db_gradient_implementation_t implementation;
    uint32_t retained_lane_mask;
    size_t original_lane_count;
    size_t lane_count;
    size_t removed_lane_count;
    size_t conforming_lane_count;
    int qualified;
    const char *reason;
} db_topology_qualification_t;

const char *
db_gradient_implementation_name(db_gradient_implementation_t implementation);
const char *db_qualification_source_name(db_qualification_source_t source);
db_conformance_result_t db_lane_qualification_for_implementation(
    const db_lane_qualification_t *lane,
    db_gradient_implementation_t implementation);
db_topology_qualification_t
db_topology_qualification_reduce(const db_lane_qualification_t *lanes,
                                 size_t lane_count);

#endif
