#include "vk_state_internal.h"

#include "core/db_conformance.h"
#include "core/db_core.h"
#include "core/db_hash.h"
#include "core/db_log.h"
#include "core/db_probe_protocol.h"
#include "core/db_qualification_contracts.h"
#include "core/db_render_result.h"
#include "core/db_renderer_diagnostics.h"
#include "db_embedded_shaders.h"
#include "vk_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define BACKEND_NAME "renderer_vulkan_1_2_multi_gpu"
#define DB_VK_PROBE_SHADER_DOMAIN UINT32_C(0x56535056)

static const db_vk_physical_device_info_t *physical_info(uint32_t index) {
    return (index < g_state.device.selection.phys_count)
               ? &g_state.device.selection.phys_info[index]
               : NULL;
}

static uint64_t shader_hash(void) {
    return db_fnv1a64_tree(db_vk_ir_execute_frag_spv,
                           db_vk_ir_execute_frag_spv_word_count *
                               sizeof(db_vk_ir_execute_frag_spv[0]),
                           DB_VK_PROBE_SHADER_DOMAIN, DB_FNV1A64_OFFSET);
}

static int
append_lane_descriptor(db_renderer_qualification_descriptor_store_t *store,
                       const db_vk_physical_device_info_t *info,
                       uint32_t lane_index, int is_primary,
                       db_gradient_implementation_t implementation) {
    db_renderer_probe_descriptor_t descriptor = {
        .backend = DB_PROBE_BACKEND_VULKAN,
        .strategy = DB_RENDER_TARGET_VULKAN_PERSISTENT_IMAGE,
        .implementation = implementation,
        .lane_index = lane_index,
        .is_primary = is_primary,
        .device =
            {
                .vendor_id = info->properties.vendorID,
                .device_id = info->properties.deviceID,
            },
        .driver =
            {
                .driver_id = (uint32_t)info->driver_properties.driverID,
                .api_version = info->properties.apiVersion,
            },
        .working_format = g_state.backing.pixel_format,
        .implementation_hash = shader_hash(),
        .logical_width = g_state.backing.extent.width,
        .logical_height = g_state.backing.extent.height,
        .compatibility_validated =
            (is_primary != 0) &&
            (implementation == DB_GRADIENT_IMPLEMENTATION_ROW_INSTANCES),
    };
    memcpy(descriptor.device.uuid, info->device_uuid,
           sizeof(descriptor.device.uuid));
    (void)db_snprintf(descriptor.provider, sizeof(descriptor.provider), "%s",
                      "vulkan_offscreen");
    (void)db_snprintf(descriptor.driver.name, sizeof(descriptor.driver.name),
                      "%s", info->driver_properties.driverName);
    (void)db_snprintf(descriptor.driver.info, sizeof(descriptor.driver.info),
                      "%s", info->driver_properties.driverInfo);
    return db_qualification_descriptor_store_append(store, &descriptor);
}

static int vk_qualification_describe(
    void *renderer, db_renderer_qualification_descriptor_store_t *output) {
    (void)renderer;
    if (output == NULL) {
        return 0;
    }
    *output = (db_renderer_qualification_descriptor_store_t){
        .generation =
            {
                .device_generation = 1U,
                .implementation_generation = shader_hash(),
                .target_contract_generation =
                    ((uint64_t)g_state.backing.extent.width << 32U) |
                    g_state.backing.extent.height,
            },
    };
    const int diagnostic_forced =
        g_state.diagnostics.vk_gradient != DB_VK_GRADIENT_AUTO;
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
            return 0;
        }
        const int primary =
            lane_index == g_state.device.selection.primary_lane_index;
        if (diagnostic_forced != 0) {
            const db_gradient_implementation_t implementation =
                (g_state.diagnostics.vk_gradient == DB_VK_GRADIENT_SEMANTIC)
                    ? DB_GRADIENT_IMPLEMENTATION_SEMANTIC
                    : DB_GRADIENT_IMPLEMENTATION_ROW_INSTANCES;
            if (append_lane_descriptor(output, info, lane_index, primary,
                                       implementation) == 0) {
                return 0;
            }
            continue;
        }
        if ((append_lane_descriptor(output, info, lane_index, primary,
                                    DB_GRADIENT_IMPLEMENTATION_SEMANTIC) ==
             0) ||
            (append_lane_descriptor(output, info, lane_index, primary,
                                    DB_GRADIENT_IMPLEMENTATION_EXACT_LOOKUP) ==
             0) ||
            (append_lane_descriptor(output, info, lane_index, primary,
                                    DB_GRADIENT_IMPLEMENTATION_ROW_INSTANCES) ==
             0)) {
            return 0;
        }
    }
    return output->count != 0U;
}

static db_renderer_prepare_status_t
vk_qualification_prepare(void *renderer,
                         const db_qualification_snapshot_t *snapshot,
                         db_renderer_selection_candidate_t *candidate) {
    (void)renderer;
    if ((snapshot == NULL) || (candidate == NULL) ||
        ((snapshot->production_qualified == 0) &&
         (snapshot->diagnostic_forced == 0)) ||
        (snapshot->retained_lanes == 0U)) {
        return DB_RENDERER_PREPARE_UNAVAILABLE;
    }
    *candidate = (db_renderer_selection_candidate_t){
        .snapshot = *snapshot,
        .renderer_generation = g_state.scheduler.scheduling_epoch,
        .prepared = 1,
    };
    return DB_RENDERER_PREPARE_OK;
}

static db_renderer_commit_status_t
vk_qualification_commit(void *renderer,
                        db_renderer_selection_candidate_t *candidate,
                        db_renderer_applied_selection_t *applied) {
    (void)renderer;
    if ((candidate == NULL) || (applied == NULL) ||
        (candidate->prepared == 0)) {
        return DB_RENDERER_COMMIT_FAILED;
    }
    if (candidate->renderer_generation != g_state.scheduler.scheduling_epoch) {
        return DB_RENDERER_COMMIT_STALE;
    }

    int removed_lane = 0;
    for (uint32_t lane_index = 0U;
         lane_index < g_state.device.selection.lane_count; lane_index++) {
        db_vk_device_lane_t *const lane =
            &g_state.device.selection.lanes[lane_index];
        if ((candidate->snapshot.retained_lanes &
             (UINT32_C(1) << lane_index)) != 0U) {
            continue;
        }
        if ((lane_index == g_state.device.selection.primary_lane_index) ||
            (lane->active_for_scheduler == 0)) {
            continue;
        }
        lane->active_for_scheduler = 0;
        if (g_state.device.selection.active_lane_count > 1U) {
            g_state.device.selection.active_lane_count--;
        }
        removed_lane = 1;
        const db_log_field_t fields[] = {
            DB_LOG_U64("lane", lane_index),
            DB_LOG_TOKEN("implementation",
                         db_gradient_implementation_name(
                             candidate->snapshot.implementation)),
            DB_LOG_TOKEN("reason", candidate->snapshot.reason),
            DB_LOG_U64("scheduling_epoch",
                       g_state.scheduler.scheduling_epoch + 1U),
        };
        db_log_info(BACKEND_NAME, "vk_qualification_lane_removed", fields,
                    DB_LOG_FIELD_COUNT(fields));
    }
    if (removed_lane != 0) {
        g_state.scheduler.scheduling_epoch++;
        g_state.calibration.state = (db_vk_calibration_state_t){
            .phase = DB_VK_MULTI_GPU_CLOSED,
        };
    }
    *applied = (db_renderer_applied_selection_t){
        .generation = candidate->snapshot.generation,
        .implementation = candidate->snapshot.implementation,
        .retained_lanes = candidate->snapshot.retained_lanes,
        .lane_count = candidate->snapshot.lane_count,
        .strategy = candidate->snapshot.strategy,
        .source = candidate->snapshot.source,
        .cache_status = candidate->snapshot.cache_status,
        .production_qualified = candidate->snapshot.production_qualified,
        .diagnostic_forced = candidate->snapshot.diagnostic_forced,
    };
    (void)db_snprintf(applied->reason, sizeof(applied->reason), "%s",
                      candidate->snapshot.reason);
    g_state.scheduler.gradient_applied = *applied;
    candidate->prepared = 0;
    return DB_RENDERER_COMMIT_OK;
}

static void
vk_qualification_abort(void *renderer,
                       db_renderer_selection_candidate_t *candidate) {
    (void)renderer;
    if (candidate != NULL) {
        *candidate = (db_renderer_selection_candidate_t){0};
    }
}

const db_renderer_qualification_ops_t *db_vk_qualification_ops(void) {
    static const db_renderer_qualification_ops_t operations = {
        .describe = vk_qualification_describe,
        .prepare_apply = vk_qualification_prepare,
        .commit_apply = vk_qualification_commit,
        .abort_apply = vk_qualification_abort,
    };
    return &operations;
}
