#include "db_frame_contracts.h"

#include "db_conformance.h"
#include "db_format_contract.h"
#include "db_frame_plan.h"
#include "db_qualification_contracts.h"
#include "db_render_result.h"
#include "db_render_types.h"
#include "db_renderer_diagnostics.h"

#include <stdint.h>

db_render_operation_path_t
db_qualification_gradient_path(const db_qualification_snapshot_t *snapshot,
                               db_render_target_strategy_t target_strategy) {
    const db_gradient_implementation_t implementation =
        ((snapshot != NULL) && ((snapshot->production_qualified != 0) ||
                                (snapshot->diagnostic_forced != 0)))
            ? snapshot->implementation
            : DB_GRADIENT_IMPLEMENTATION_ROW_INSTANCES;
    switch (target_strategy) {
    case DB_RENDER_TARGET_NONE:
        return DB_RENDER_OPERATION_NONE;
    case DB_RENDER_TARGET_GL1_DIRECT_WINDOW:
    case DB_RENDER_TARGET_GL1_PERSISTENT_FBO:
        return (implementation == DB_GRADIENT_IMPLEMENTATION_SEMANTIC)
                   ? DB_RENDER_OPERATION_GL1_INTERPOLATED_GRADIENT
                   : DB_RENDER_OPERATION_GL1_ROW_FILL;
    case DB_RENDER_TARGET_GL3_PERSISTENT_FBO:
        switch (implementation) {
        case DB_GRADIENT_IMPLEMENTATION_SEMANTIC:
            return DB_RENDER_OPERATION_GL3_SEMANTIC_GRADIENT;
        case DB_GRADIENT_IMPLEMENTATION_EXACT_LOOKUP:
            return DB_RENDER_OPERATION_GL3_EXACT_LOOKUP;
        case DB_GRADIENT_IMPLEMENTATION_ROW_INSTANCES:
            return DB_RENDER_OPERATION_GL3_ROW_FILL;
        }
        return DB_RENDER_OPERATION_GL3_ROW_FILL;
    case DB_RENDER_TARGET_VULKAN_PERSISTENT_IMAGE:
        switch (implementation) {
        case DB_GRADIENT_IMPLEMENTATION_SEMANTIC:
            return DB_RENDER_OPERATION_VULKAN_SEMANTIC_GRADIENT;
        case DB_GRADIENT_IMPLEMENTATION_EXACT_LOOKUP:
            return DB_RENDER_OPERATION_VULKAN_EXACT_LOOKUP;
        case DB_GRADIENT_IMPLEMENTATION_ROW_INSTANCES:
            return DB_RENDER_OPERATION_VULKAN_ROW_FILL;
        }
        return DB_RENDER_OPERATION_VULKAN_ROW_FILL;
    case DB_RENDER_TARGET_CPU_SURFACE:
    case DB_RENDER_TARGET_GL1_CPU_UPLOAD:
        return DB_RENDER_OPERATION_CPU_NATIVE;
    }
    return DB_RENDER_OPERATION_NONE;
}

const char *
db_renderer_strategy_reason_name(db_renderer_strategy_reason_t reason) {
    switch (reason) {
    case DB_RENDERER_STRATEGY_REASON_NONE:
        return "none";
    case DB_RENDERER_STRATEGY_REASON_DIRECT_WINDOW_ELIGIBLE:
        return "direct_window_eligible";
    case DB_RENDERER_STRATEGY_REASON_BUFFER_AGE_PENDING:
        return "buffer_age_pending";
    case DB_RENDERER_STRATEGY_REASON_PRESENTER_FORMAT_MISMATCH:
        return "presenter_format_mismatch";
    case DB_RENDERER_STRATEGY_REASON_PRESENTATION_CONVERSION_REQUIRED:
        return "presentation_conversion_required";
    case DB_RENDERER_STRATEGY_REASON_DIRECT_WINDOW_CAPABILITY_MISSING:
        return "direct_window_capability_missing";
    case DB_RENDERER_STRATEGY_REASON_DIRECT_WINDOW_LINEAGE_UNINITIALIZED:
        return "direct_window_lineage_uninitialized";
    case DB_RENDERER_STRATEGY_REASON_STRATEGY_TRANSITION_REBUILD:
        return "strategy_transition_rebuild";
    case DB_RENDERER_STRATEGY_REASON_EXPLICIT_REQUEST:
        return "explicit_request";
    }
    return "unknown";
}

static int
gl1_direct_window_capable(const db_renderer_preflight_policy_input_t *input,
                          const db_presenter_facts_t *presenter,
                          const db_qualification_snapshot_t *qualification,
                          db_renderer_strategy_reason_t *reason) {
    if (presenter->buffer_age_valid == 0) {
        *reason = DB_RENDERER_STRATEGY_REASON_BUFFER_AGE_PENDING;
        return 0;
    }
    if ((presenter->gl.valid == 0) ||
        (input->working_format != DB_PIXEL_FORMAT_RGBA8) ||
        (presenter->gl.native_format != DB_NATIVE_OUTPUT_XRGB8888) ||
        (presenter->destination_width != presenter->gl.native_width) ||
        (presenter->destination_height != presenter->gl.native_height) ||
        (presenter->gl.channel_bits[0] != 8U) ||
        (presenter->gl.channel_bits[1] != 8U) ||
        (presenter->gl.channel_bits[2] != 8U) ||
        (presenter->gl.sample_count != 0U)) {
        *reason = DB_RENDERER_STRATEGY_REASON_PRESENTER_FORMAT_MISMATCH;
        return 0;
    }
    if (presenter->gl.platform_conversion_required != 0) {
        *reason = DB_RENDERER_STRATEGY_REASON_PRESENTATION_CONVERSION_REQUIRED;
        return 0;
    }
    if ((input->gl1_direct_window.can_control_dither == 0) ||
        (input->gl1_direct_window.can_control_srgb == 0) ||
        (input->gl1_direct_window.can_select_required_buffers == 0) ||
        (input->gl1_direct_window.pre_swap_readback_qualified == 0) ||
        (input->gl1_direct_window.fixed_function_raster_qualified == 0) ||
        (qualification == NULL) || (qualification->production_qualified == 0)) {
        *reason = DB_RENDERER_STRATEGY_REASON_DIRECT_WINDOW_CAPABILITY_MISSING;
        return 0;
    }
    *reason = DB_RENDERER_STRATEGY_REASON_DIRECT_WINDOW_ELIGIBLE;
    return 1;
}

static db_render_target_strategy_t
gl1_window_strategy(const db_renderer_preflight_policy_input_t *input,
                    const db_presenter_facts_t *presenter,
                    const db_qualification_snapshot_t *qualification,
                    db_renderer_strategy_reason_t *reason) {
    const db_gl1_target_request_t request = input->gl1_target_request;
    switch (request) {
    case DB_GL1_TARGET_DIRECT_WINDOW:
        *reason = DB_RENDERER_STRATEGY_REASON_EXPLICIT_REQUEST;
        return DB_RENDER_TARGET_GL1_DIRECT_WINDOW;
    case DB_GL1_TARGET_PERSISTENT_FBO:
        *reason = DB_RENDERER_STRATEGY_REASON_EXPLICIT_REQUEST;
        return DB_RENDER_TARGET_GL1_PERSISTENT_FBO;
    case DB_GL1_TARGET_CPU_UPLOAD:
        *reason = DB_RENDERER_STRATEGY_REASON_EXPLICIT_REQUEST;
        return DB_RENDER_TARGET_GL1_CPU_UPLOAD;
    case DB_GL1_TARGET_AUTO:
        if (gl1_direct_window_capable(input, presenter, qualification,
                                      reason) != 0) {
            return DB_RENDER_TARGET_GL1_DIRECT_WINDOW;
        }
        return DB_RENDER_TARGET_GL1_PERSISTENT_FBO;
    }
    *reason = DB_RENDERER_STRATEGY_REASON_NONE;
    return DB_RENDER_TARGET_GL1_PERSISTENT_FBO;
}

int db_renderer_preflight_policy_resolve(
    const db_renderer_preflight_policy_input_t *input,
    const db_presenter_facts_t *presenter,
    const db_qualification_snapshot_t *qualification,
    db_renderer_preflight_t *preflight) {
    if ((input == NULL) || (presenter == NULL) || (presenter->valid == 0) ||
        (preflight == NULL)) {
        return 0;
    }
    db_render_target_strategy_t strategy = DB_RENDER_TARGET_NONE;
    db_renderer_strategy_reason_t strategy_reason =
        DB_RENDERER_STRATEGY_REASON_NONE;
    switch (input->profile) {
    case DB_RENDERER_PREFLIGHT_CPU:
        strategy = DB_RENDER_TARGET_CPU_SURFACE;
        break;
    case DB_RENDERER_PREFLIGHT_GL1_WINDOW:
        strategy = gl1_window_strategy(input, presenter, qualification,
                                       &strategy_reason);
        break;
    case DB_RENDERER_PREFLIGHT_GL1_PERSISTENT:
        strategy = DB_RENDER_TARGET_GL1_PERSISTENT_FBO;
        break;
    case DB_RENDERER_PREFLIGHT_GL3_PERSISTENT:
        strategy = DB_RENDER_TARGET_GL3_PERSISTENT_FBO;
        break;
    case DB_RENDERER_PREFLIGHT_VULKAN_PERSISTENT:
        strategy = DB_RENDER_TARGET_VULKAN_PERSISTENT_IMAGE;
        break;
    }
    if (strategy == DB_RENDER_TARGET_NONE) {
        return 0;
    }
    const int direct_repair =
        (strategy == DB_RENDER_TARGET_GL1_DIRECT_WINDOW) &&
        (presenter->buffer_age_valid == 0);
    const int strategy_transition =
        (strategy == DB_RENDER_TARGET_GL1_DIRECT_WINDOW) &&
        ((input->previous_strategy != DB_RENDER_TARGET_GL1_DIRECT_WINDOW) ||
         (input->previous_target_generation != presenter->generation) ||
         (input->direct_window_lineage_valid == 0));
    const int presenter_rebuild =
        (presenter->target_recreated != 0) ||
        (presenter->extent_changed != 0) ||
        (presenter->native_format_changed != 0) ||
        (presenter->conversion_contract_changed != 0) ||
        (presenter->prior_content_state == DB_TARGET_CONTENT_LOST) ||
        (presenter->prior_content_state ==
         DB_TARGET_CONTENT_PARTIALLY_MODIFIED);
    db_frame_rebuild_reason_t rebuild_reason = DB_FRAME_REBUILD_EXPLICIT;
    if ((strategy_transition == 0) && (presenter_rebuild != 0)) {
        rebuild_reason = DB_FRAME_REBUILD_INITIAL_TARGET;
    } else if ((strategy_transition == 0) && (presenter_rebuild == 0) &&
               (direct_repair == 0)) {
        rebuild_reason = DB_FRAME_REBUILD_NONE;
    }
    *preflight = (db_renderer_preflight_t){
        .plan_request = input->plan_request,
        .target_strategy = strategy,
        .gradient_path =
            db_qualification_gradient_path(qualification, strategy),
        .strategy_generation = (qualification != NULL)
                                   ? qualification->generation
                                   : presenter->generation,
        .qualification_generation =
            (qualification != NULL) ? qualification->generation : 0U,
        .target_generation = presenter->generation,
        .strategy_reason =
            strategy_transition
                ? DB_RENDERER_STRATEGY_REASON_STRATEGY_TRANSITION_REBUILD
                : strategy_reason,
        .rebuild_required =
            presenter_rebuild || direct_repair || strategy_transition,
        .rebuild_reason = rebuild_reason,
    };
    return 1;
}

db_renderer_target_t
db_renderer_target_from_preflight(const db_renderer_preflight_t *preflight,
                                  uint64_t identity,
                                  uint64_t resource_generation) {
    if (preflight == NULL) {
        return (db_renderer_target_t){0};
    }
    return (db_renderer_target_t){
        .identity = identity,
        .generation = resource_generation,
        .strategy = preflight->target_strategy,
        .gradient_path = preflight->gradient_path,
        .strategy_generation = preflight->strategy_generation,
        .qualification_generation = preflight->qualification_generation,
        .target_generation = preflight->target_generation,
        .strategy_reason = preflight->strategy_reason,
        .valid = 1,
    };
}
