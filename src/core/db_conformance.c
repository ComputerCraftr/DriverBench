#include "db_conformance.h"

#include "db_conformance_cache.h"

#include <stddef.h>
#include <stdint.h>

const char *
db_gradient_implementation_name(db_gradient_implementation_t implementation) {
    switch (implementation) {
    case DB_GRADIENT_IMPLEMENTATION_ROW_INSTANCES:
        return "row_instances";
    case DB_GRADIENT_IMPLEMENTATION_EXACT_LOOKUP:
        return "exact_lookup";
    case DB_GRADIENT_IMPLEMENTATION_SEMANTIC:
        return "semantic_gradient";
    }
    return "unknown";
}

const char *db_qualification_source_name(db_qualification_source_t source) {
    switch (source) {
    case DB_QUALIFICATION_SOURCE_NONE:
        return "none";
    case DB_QUALIFICATION_SOURCE_BASELINE:
        return "baseline";
    case DB_QUALIFICATION_SOURCE_CACHE:
        return "cache";
    case DB_QUALIFICATION_SOURCE_HELPER:
        return "helper";
    case DB_QUALIFICATION_SOURCE_DIAGNOSTIC:
        return "diagnostic";
    }
    return "unknown";
}

db_conformance_result_t db_lane_qualification_for_implementation(
    const db_lane_qualification_t *lane,
    db_gradient_implementation_t implementation) {
    if (lane == NULL) {
        return DB_CONFORMANCE_UNTESTED;
    }
    switch (implementation) {
    case DB_GRADIENT_IMPLEMENTATION_ROW_INSTANCES:
        return lane->row_instances;
    case DB_GRADIENT_IMPLEMENTATION_EXACT_LOOKUP:
        return lane->exact_lookup;
    case DB_GRADIENT_IMPLEMENTATION_SEMANTIC:
        return lane->semantic;
    }
    return DB_CONFORMANCE_UNTESTED;
}

static size_t count_conforming(const db_lane_qualification_t *lanes,
                               size_t lane_count, uint32_t retained_lane_mask,
                               db_gradient_implementation_t implementation) {
    size_t count = 0U;
    for (size_t index = 0U; index < lane_count; index++) {
        if ((retained_lane_mask & (UINT32_C(1) << index)) != 0U) {
            count += db_lane_qualification_for_implementation(&lanes[index],
                                                              implementation) ==
                     DB_CONFORMANCE_CONFORMING;
        }
    }
    return count;
}

static size_t lane_mask_count(uint32_t mask, size_t lane_count) {
    size_t count = 0U;
    for (size_t index = 0U; index < lane_count; index++) {
        count += (mask & (UINT32_C(1) << index)) != 0U;
    }
    return count;
}

static db_topology_qualification_t
reduce_masked(const db_lane_qualification_t *lanes, size_t original_lane_count,
              uint32_t retained_lane_mask, const char *fallback_reason) {
    const size_t retained_count =
        lane_mask_count(retained_lane_mask, original_lane_count);
    const db_gradient_implementation_t implementations[] = {
        DB_GRADIENT_IMPLEMENTATION_SEMANTIC,
        DB_GRADIENT_IMPLEMENTATION_EXACT_LOOKUP,
        DB_GRADIENT_IMPLEMENTATION_ROW_INSTANCES,
    };
    const int lanes_removed = retained_count != original_lane_count;
    const char *const reasons[] = {
        (lanes_removed != 0) ? "nonconforming_lanes_removed_semantic"
                             : "all_lanes_semantic",
        (lanes_removed != 0) ? "nonconforming_lanes_removed_exact_lookup"
                             : "semantic_rejected_exact_lookup",
        fallback_reason,
    };
    for (size_t index = 0U;
         index < sizeof(implementations) / sizeof(implementations[0]);
         index++) {
        const size_t conforming =
            count_conforming(lanes, original_lane_count, retained_lane_mask,
                             implementations[index]);
        if ((retained_count != 0U) && (conforming == retained_count)) {
            return (db_topology_qualification_t){
                .implementation = implementations[index],
                .retained_lane_mask = retained_lane_mask,
                .original_lane_count = original_lane_count,
                .lane_count = retained_count,
                .removed_lane_count = original_lane_count - retained_count,
                .conforming_lane_count = conforming,
                .qualified = 1,
                .reason = reasons[index],
            };
        }
    }
    return (db_topology_qualification_t){
        .implementation = DB_GRADIENT_IMPLEMENTATION_ROW_INSTANCES,
        .retained_lane_mask = retained_lane_mask,
        .original_lane_count = original_lane_count,
        .lane_count = retained_count,
        .removed_lane_count = original_lane_count - retained_count,
        .conforming_lane_count = 0U,
        .reason = "primary_qualification_unavailable",
    };
}

db_topology_qualification_t
db_topology_qualification_reduce(const db_lane_qualification_t *lanes,
                                 size_t lane_count) {
    if ((lanes == NULL) || (lane_count == 0U)) {
        return (db_topology_qualification_t){
            .implementation = DB_GRADIENT_IMPLEMENTATION_ROW_INSTANCES,
            .reason = "qualification_unavailable",
        };
    }
    if (lane_count > 32U) {
        return (db_topology_qualification_t){
            .implementation = DB_GRADIENT_IMPLEMENTATION_ROW_INSTANCES,
            .original_lane_count = lane_count,
            .reason = "qualification_capacity",
        };
    }
    const uint32_t all_lanes = (lane_count == 32U)
                                   ? UINT32_MAX
                                   : (UINT32_C(1) << lane_count) - UINT32_C(1);
    uint32_t primary_mask = 0U;
    for (size_t index = 0U; index < lane_count; index++) {
        if (lanes[index].is_primary != 0) {
            primary_mask |= UINT32_C(1) << index;
        }
    }
    if (primary_mask == 0U) {
        primary_mask = UINT32_C(1);
    }
    if ((primary_mask & (primary_mask - UINT32_C(1))) != 0U) {
        return (db_topology_qualification_t){
            .implementation = DB_GRADIENT_IMPLEMENTATION_ROW_INSTANCES,
            .original_lane_count = lane_count,
            .reason = "primary_identity_invalid",
        };
    }
    db_topology_qualification_t result = reduce_masked(
        lanes, lane_count, all_lanes, "exact_lookup_rejected_row_instances");
    if (result.qualified != 0) {
        return result;
    }
    uint32_t row_conforming_mask = 0U;
    for (size_t index = 0U; index < lane_count; index++) {
        if (lanes[index].row_instances == DB_CONFORMANCE_CONFORMING) {
            row_conforming_mask |= UINT32_C(1) << index;
        }
    }
    if ((row_conforming_mask & primary_mask) == 0U) {
        return (db_topology_qualification_t){
            .implementation = DB_GRADIENT_IMPLEMENTATION_ROW_INSTANCES,
            .retained_lane_mask = primary_mask,
            .original_lane_count = lane_count,
            .lane_count = 1U,
            .removed_lane_count = lane_count - 1U,
            .reason = "primary_qualification_unavailable",
        };
    }
    return reduce_masked(lanes, lane_count, row_conforming_mask,
                         "nonconforming_lanes_removed");
}
