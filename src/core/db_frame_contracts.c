#include "db_frame_contracts.h"

#include "db_conformance.h"
#include "db_frame_plan.h"
#include "db_qualification_contracts.h"
#include "db_render_result.h"
#include "db_renderer_diagnostics.h"

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
        return (implementation == DB_GRADIENT_IMPLEMENTATION_SEMANTIC)
                   ? DB_RENDER_OPERATION_GL3_SEMANTIC_GRADIENT
                   : DB_RENDER_OPERATION_GL3_ROW_FILL;
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

static db_render_target_strategy_t
gl1_window_strategy(db_gl1_target_request_t request,
                    const db_presenter_facts_t *presenter,
                    const db_qualification_snapshot_t *qualification) {
    switch (request) {
    case DB_GL1_TARGET_DIRECT_WINDOW:
        return DB_RENDER_TARGET_GL1_DIRECT_WINDOW;
    case DB_GL1_TARGET_PERSISTENT_FBO:
        return DB_RENDER_TARGET_GL1_PERSISTENT_FBO;
    case DB_GL1_TARGET_CPU_UPLOAD:
        return DB_RENDER_TARGET_GL1_CPU_UPLOAD;
    case DB_GL1_TARGET_AUTO:
        if ((presenter->buffer_age_valid != 0) &&
            (presenter->conversion_required == 0) && (qualification != NULL) &&
            (qualification->production_qualified != 0)) {
            return DB_RENDER_TARGET_GL1_DIRECT_WINDOW;
        }
        return DB_RENDER_TARGET_GL1_PERSISTENT_FBO;
    }
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
    switch (input->profile) {
    case DB_RENDERER_PREFLIGHT_CPU:
        strategy = DB_RENDER_TARGET_CPU_SURFACE;
        break;
    case DB_RENDERER_PREFLIGHT_GL1_WINDOW:
        strategy = gl1_window_strategy(input->gl1_target_request, presenter,
                                       qualification);
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
    const int requested_rebuild =
        (input->rebuild_required != 0) &&
        ((input->rebuild_direct_window_only == 0) ||
         (strategy == DB_RENDER_TARGET_GL1_DIRECT_WINDOW));
    *preflight = (db_renderer_preflight_t){
        .plan_request = input->plan_request,
        .target_strategy = strategy,
        .gradient_path =
            db_qualification_gradient_path(qualification, strategy),
        .strategy_generation = (qualification != NULL)
                                   ? qualification->generation
                                   : presenter->generation,
        .rebuild_required = requested_rebuild || direct_repair,
        .rebuild_reason =
            direct_repair ? DB_FRAME_REBUILD_EXPLICIT : input->rebuild_reason,
    };
    return 1;
}
