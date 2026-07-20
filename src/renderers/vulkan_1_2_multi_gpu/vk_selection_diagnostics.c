#include "vk_selection_diagnostics.h"

#include "core/db_log.h"
#include "vk_init_internal.h"
#include "vk_internal.h"

#include <stdint.h>

void db_vk_log_physical_devices(const DeviceSelectionState *selection) {
    if (selection == NULL) {
        return;
    }
    for (uint32_t index = 0U; index < selection->phys_count; index++) {
        const db_vk_physical_device_info_t *const info =
            &selection->phys_info[index];
        const char *const driver_name =
            (info->driver_properties.driverName[0] != '\0')
                ? info->driver_properties.driverName
                : "unavailable";
        const char *const driver_info =
            (info->driver_properties.driverInfo[0] != '\0')
                ? info->driver_properties.driverInfo
                : "unavailable";
        const db_log_field_t fields[] = {
            DB_LOG_U64("physical_index", index),
            DB_LOG_STRING("device_name", info->properties.deviceName),
            DB_LOG_U64("driver_id", (uint32_t)info->driver_properties.driverID),
            DB_LOG_STRING("driver_name", driver_name),
            DB_LOG_STRING("driver_info", driver_info),
            DB_LOG_HEX64("driver_version", info->properties.driverVersion),
            DB_LOG_HEX64("api_version", info->properties.apiVersion),
            DB_LOG_BOOL("graphics_supported", info->supports_graphics),
            DB_LOG_BOOL("present_supported", info->supports_present),
            DB_LOG_BOOL("hdr10_format_supported", info->supports_hdr10_format),
            DB_LOG_BOOL("hdr10_colorspace_supported",
                        info->supports_hdr10_colorspace),
            DB_LOG_BOOL("hdr10_surface_pair_supported",
                        info->supports_hdr10_surface_pair),
            DB_LOG_BOOL("hdr_metadata_supported", info->supports_hdr_metadata),
            DB_LOG_BOOL("selected_primary",
                        index == selection->primary_phys_index),
        };
        db_log_info(BACKEND_NAME, "vk_physical_device", fields,
                    DB_LOG_FIELD_COUNT(fields));
    }
}

void db_vk_log_execution_plan(const DeviceSelectionState *selection) {
    if (selection == NULL) {
        return;
    }
    uint32_t independent_candidate_count = 0U;
    for (uint32_t index = 0U; index < selection->lane_count; index++) {
        const db_vk_device_lane_t *const lane = &selection->lanes[index];
        if ((lane->backend == DB_VK_LANE_BACKEND_INDEPENDENT) &&
            (lane->can_compose_to_primary != 0)) {
            independent_candidate_count++;
        }
    }
    const db_log_field_t topology_fields[] = {
        DB_LOG_BOOL("device_group_available", selection->have_group),
        DB_LOG_U64("independent_candidate_count", independent_candidate_count),
        DB_LOG_BOOL("independent_topology_available",
                    independent_candidate_count > 0U),
    };
    db_log_info(BACKEND_NAME, "vk_topology_capability", topology_fields,
                DB_LOG_FIELD_COUNT(topology_fields));
    const db_log_field_t plan_fields[] = {
        DB_LOG_TOKEN("execution_mode", db_vk_scheduler_mode_name_effective(
                                           selection->execution_mode,
                                           selection->active_lane_count)),
        DB_LOG_U64("primary_lane", selection->primary_lane_index),
        DB_LOG_U64("active_lanes", selection->active_lane_count),
        DB_LOG_U64("discovered_lanes", selection->lane_count),
    };
    db_log_info(BACKEND_NAME, "vk_execution_plan", plan_fields,
                DB_LOG_FIELD_COUNT(plan_fields));
    for (uint32_t index = 0U; index < selection->lane_count; index++) {
        const db_vk_device_lane_t *lane = &selection->lanes[index];
        const char *backend_name = "primary";
        if (lane->backend == DB_VK_LANE_BACKEND_GROUP) {
            backend_name = "group_lane";
        } else if (lane->backend == DB_VK_LANE_BACKEND_INDEPENDENT) {
            backend_name = "independent_lane";
        }
        const db_log_field_t lane_fields[] = {
            DB_LOG_U64("lane", index),
            DB_LOG_TOKEN("backend", backend_name),
            DB_LOG_U64("physical_index", lane->physical_index),
            DB_LOG_BOOL("present", lane->can_present),
            DB_LOG_BOOL("compose", lane->can_compose_to_primary),
            DB_LOG_BOOL("active", lane->active_for_scheduler),
            DB_LOG_STRING("reason", (lane->inactive_reason[0] != '\0')
                                        ? lane->inactive_reason
                                        : "active"),
        };
        db_log_info(BACKEND_NAME, "vk_lane", lane_fields,
                    DB_LOG_FIELD_COUNT(lane_fields));
    }
}
