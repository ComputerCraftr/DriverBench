#include "vk_state_internal.h"

#include "core/db_conformance.h"
#include "core/db_conformance_cache.h"
#include "core/db_conformance_service.h"
#include "core/db_core.h"
#include "core/db_hash.h"
#include "core/db_log.h"
#include "core/db_probe_protocol.h"
#include "core/db_renderer_diagnostics.h"
#include "db_embedded_shaders.h"
#include "vk_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define BACKEND_NAME "renderer_vulkan_1_2_multi_gpu"
#define DB_VK_PROBE_SHADER_DOMAIN UINT32_C(0x56535056)
#define DB_VK_QUALIFICATION_AGGREGATE_NS UINT64_C(60000000000)

enum {
    DB_VK_PROBE_SCHEMA_VERSION = 1U,
    DB_VK_PROBE_EVALUATOR_VERSION = 3U,
    DB_VK_PROBE_DOMAIN_VERSION = 1U,
    DB_VK_PROBE_BUILD_VERSION = 1U,
    DB_VK_PROBE_GRADIENT_WINDOW_ROWS = 32U,
    DB_VK_QUALIFICATIONS_PER_LANE = 3U,
    DB_VK_MAX_QUALIFICATION_KEYS =
        MAX_GPU_COUNT * DB_VK_QUALIFICATIONS_PER_LANE,
};

static const db_vk_physical_device_info_t *physical_info(uint32_t index) {
    return (index < g_state.device.selection.phys_count)
               ? &g_state.device.selection.phys_info[index]
               : NULL;
}

static db_conformance_key_t
lane_qualification_key(const db_vk_physical_device_info_t *info,
                       db_gradient_implementation_t implementation) {
    const uint64_t shader_hash =
        db_fnv1a64_tree(db_vk_ir_execute_frag_spv,
                        db_vk_ir_execute_frag_spv_word_count *
                            sizeof(db_vk_ir_execute_frag_spv[0]),
                        DB_VK_PROBE_SHADER_DOMAIN, DB_FNV1A64_OFFSET);
    db_conformance_key_t key = {
        .schema_version = DB_VK_PROBE_SCHEMA_VERSION,
        .evaluator_version = DB_VK_PROBE_EVALUATOR_VERSION,
        .domain_version = DB_VK_PROBE_DOMAIN_VERSION,
        .build_version = DB_VK_PROBE_BUILD_VERSION,
        .backend = DB_PROBE_BACKEND_VULKAN,
        .implementation = implementation,
        .working_format = g_state.backing.pixel_format,
        .vendor_id = info->properties.vendorID,
        .device_id = info->properties.deviceID,
        .driver_id = (uint32_t)info->driver_properties.driverID,
        .api_version = info->properties.apiVersion,
        .logical_width = g_state.backing.extent.width,
        .logical_height = g_state.backing.extent.height,
        .gradient_window_rows = DB_VK_PROBE_GRADIENT_WINDOW_ROWS,
        .implementation_hash = shader_hash,
        .provider = "vulkan_offscreen",
        .strategy = "instanced_command_ranges",
    };
    memcpy(key.device_uuid, info->device_uuid, sizeof(key.device_uuid));
    (void)db_snprintf(key.driver_name, sizeof(key.driver_name), "%s",
                      info->driver_properties.driverName);
    (void)db_snprintf(key.driver_info, sizeof(key.driver_info), "%s",
                      info->driver_properties.driverInfo);
    return key;
}

void db_vk_resolve_gradient_qualification(void) {
    if (g_state.scheduler.gradient_qualification_resolved != 0) {
        return;
    }
    size_t qualified_lane_count = 0U;
    db_conformance_key_t keys[DB_VK_MAX_QUALIFICATION_KEYS] = {};
    db_conformance_decision_t decisions[DB_VK_MAX_QUALIFICATION_KEYS] = {};
    db_qualification_source_t source = DB_QUALIFICATION_SOURCE_NONE;
    db_conformance_cache_status_t cache_status = DB_CONFORMANCE_CACHE_MISS;
    for (uint32_t lane_index = 0U;
         lane_index < g_state.device.selection.lane_count; lane_index++) {
        const db_vk_device_lane_t *const lane =
            &g_state.device.selection.lanes[lane_index];
        if ((lane->active_for_scheduler == 0) &&
            (lane->can_compose_to_primary == 0)) {
            continue;
        }
        const db_vk_physical_device_info_t *const info =
            physical_info(lane->physical_index);
        if (info == NULL) {
            continue;
        }
        db_lane_qualification_t *const result =
            &g_state.scheduler.gradient_lanes[qualified_lane_count];
        g_state.scheduler.gradient_lane_indices[qualified_lane_count] =
            lane_index;
        qualified_lane_count++;
        memcpy(result->device_uuid, info->device_uuid,
               sizeof(result->device_uuid));
        result->vendor_id = info->properties.vendorID;
        result->device_id = info->properties.deviceID;
        result->driver_id = (uint32_t)info->driver_properties.driverID;
        result->is_primary =
            lane_index == g_state.device.selection.primary_lane_index;
        const size_t key_first =
            (qualified_lane_count - 1U) * DB_VK_QUALIFICATIONS_PER_LANE;
        keys[key_first] =
            lane_qualification_key(info, DB_GRADIENT_IMPLEMENTATION_SEMANTIC);
        keys[key_first + 1U] = lane_qualification_key(
            info, DB_GRADIENT_IMPLEMENTATION_EXACT_LOOKUP);
        keys[key_first + 2U] = lane_qualification_key(
            info, DB_GRADIENT_IMPLEMENTATION_ROW_INSTANCES);
    }
    const size_t key_count =
        qualified_lane_count * DB_VK_QUALIFICATIONS_PER_LANE;
    const db_conformance_query_t query = {
        .ignore_cache = g_state.diagnostics.ignore_conformance_cache,
        .rerun_probe = g_state.diagnostics.rerun_conformance_probe,
    };
    if ((key_count != 0U) &&
        (db_conformance_qualify_batch(keys, key_count, &query,
                                      DB_VK_QUALIFICATION_AGGREGATE_NS,
                                      decisions) == 0)) {
        qualified_lane_count = 0U;
    }
    for (size_t lane_result_index = 0U;
         lane_result_index < qualified_lane_count; lane_result_index++) {
        db_lane_qualification_t *const result =
            &g_state.scheduler.gradient_lanes[lane_result_index];
        const size_t key_first =
            lane_result_index * DB_VK_QUALIFICATIONS_PER_LANE;
        const db_conformance_decision_t semantic = decisions[key_first];
        const db_conformance_decision_t exact = decisions[key_first + 1U];
        const db_conformance_decision_t rows = decisions[key_first + 2U];
        result->semantic = semantic.result;
        result->exact_lookup = exact.result;
        result->row_instances = rows.result;
        if ((semantic.source == DB_QUALIFICATION_SOURCE_HELPER) ||
            (exact.source == DB_QUALIFICATION_SOURCE_HELPER) ||
            (rows.source == DB_QUALIFICATION_SOURCE_HELPER)) {
            source = DB_QUALIFICATION_SOURCE_HELPER;
        } else if ((source == DB_QUALIFICATION_SOURCE_NONE) &&
                   ((semantic.source == DB_QUALIFICATION_SOURCE_CACHE) ||
                    (exact.source == DB_QUALIFICATION_SOURCE_CACHE) ||
                    (rows.source == DB_QUALIFICATION_SOURCE_CACHE))) {
            source = DB_QUALIFICATION_SOURCE_CACHE;
        }
        if ((semantic.cache_status == DB_CONFORMANCE_CACHE_HIT) ||
            (exact.cache_status == DB_CONFORMANCE_CACHE_HIT) ||
            (rows.cache_status == DB_CONFORMANCE_CACHE_HIT)) {
            cache_status = DB_CONFORMANCE_CACHE_HIT;
        }
    }
    g_state.scheduler.gradient_topology = db_topology_qualification_reduce(
        g_state.scheduler.gradient_lanes, qualified_lane_count);
    int removed_lane = 0;
    for (size_t qualification_index = 0U;
         qualification_index < qualified_lane_count; qualification_index++) {
        if ((g_state.scheduler.gradient_topology.retained_lane_mask &
             (UINT32_C(1) << qualification_index)) != 0U) {
            continue;
        }
        const uint32_t lane_index =
            g_state.scheduler.gradient_lane_indices[qualification_index];
        db_vk_device_lane_t *const lane =
            &g_state.device.selection.lanes[lane_index];
        if (lane_index == g_state.device.selection.primary_lane_index) {
            continue;
        }
        if (lane->active_for_scheduler != 0) {
            lane->active_for_scheduler = 0;
            if (g_state.device.selection.active_lane_count > 1U) {
                g_state.device.selection.active_lane_count--;
            }
            removed_lane = 1;
            const db_log_field_t fields[] = {
                DB_LOG_U64("lane", lane_index),
                DB_LOG_U64("vendor_id",
                           g_state.scheduler.gradient_lanes[qualification_index]
                               .vendor_id),
                DB_LOG_U64("device_id",
                           g_state.scheduler.gradient_lanes[qualification_index]
                               .device_id),
                DB_LOG_TOKEN("implementation", "row_instances"),
                DB_LOG_TOKEN("reason", "row_instances_nonconforming"),
                DB_LOG_U64("scheduling_epoch",
                           g_state.scheduler.scheduling_epoch + 1U),
            };
            db_log_info(BACKEND_NAME, "vk_qualification_lane_removed", fields,
                        DB_LOG_FIELD_COUNT(fields));
        }
    }
    if (removed_lane != 0) {
        g_state.scheduler.scheduling_epoch++;
        g_state.calibration.state = (db_vk_calibration_state_t){
            .phase = DB_VK_MULTI_GPU_CLOSED,
        };
    }
    g_state.scheduler.gradient_qualification_source = source;
    g_state.scheduler.gradient_cache_status = cache_status;
    g_state.scheduler.gradient_qualification_resolved = 1;
}
