#ifndef DRIVERBENCH_CORE_FRAME_CONTRACTS_H
#define DRIVERBENCH_CORE_FRAME_CONTRACTS_H

#include "core/db_render_types.h"
#include "db_format_contract.h"
#include "db_frame_plan.h"
#include "db_qualification_contracts.h"
#include "db_render_result.h"
#include "db_renderer_diagnostics.h"

#include <stdint.h>

typedef enum {
    DB_PRESENT_ACCEPTED = 0,
    DB_PRESENT_RETRYABLE,
    DB_PRESENT_TARGET_LOST,
    DB_PRESENT_FATAL,
} db_present_result_t;

typedef enum {
    DB_RENDER_EXECUTED = 0,
    DB_RENDER_RETRYABLE,
    DB_RENDER_FATAL,
} db_renderer_execute_status_t;

typedef enum {
    DB_TARGET_CONTENT_UNCHANGED = 0,
    DB_TARGET_CONTENT_VALID_UNCOMMITTED,
    DB_TARGET_CONTENT_PARTIALLY_MODIFIED,
    DB_TARGET_CONTENT_LOST,
} db_target_content_state_t;

typedef struct {
    uint32_t native_width;
    uint32_t native_height;
    db_native_output_format_t native_format;
    uint32_t channel_bits[4];
    uint32_t sample_count;
    uint32_t buffer_age;
    uint64_t generation;
    int platform_conversion_required;
    int valid;
} db_gl_presenter_facts_t;

typedef struct {
    int can_control_dither;
    int can_control_srgb;
    int can_select_required_buffers;
    int pre_swap_readback_qualified;
    int fixed_function_raster_qualified;
} db_gl1_direct_window_capabilities_t;

typedef struct {
    uint32_t destination_width;
    uint32_t destination_height;
    db_pixel_format_t native_hash_format;
    db_gl_presenter_facts_t gl;
    uint64_t generation;
    uint32_t raw_buffer_age;
    uint32_t replay_depth;
    int buffer_age_valid;
    db_target_content_state_t prior_content_state;
    int target_recreated;
    int extent_changed;
    int native_format_changed;
    int conversion_contract_changed;
    int valid;
} db_presenter_facts_t;

typedef enum {
    DB_RENDERER_STRATEGY_REASON_NONE = 0,
    DB_RENDERER_STRATEGY_REASON_DIRECT_WINDOW_ELIGIBLE,
    DB_RENDERER_STRATEGY_REASON_BUFFER_AGE_PENDING,
    DB_RENDERER_STRATEGY_REASON_PRESENTER_FORMAT_MISMATCH,
    DB_RENDERER_STRATEGY_REASON_PRESENTATION_CONVERSION_REQUIRED,
    DB_RENDERER_STRATEGY_REASON_DIRECT_WINDOW_CAPABILITY_MISSING,
    DB_RENDERER_STRATEGY_REASON_DIRECT_WINDOW_LINEAGE_UNINITIALIZED,
    DB_RENDERER_STRATEGY_REASON_STRATEGY_TRANSITION_REBUILD,
    DB_RENDERER_STRATEGY_REASON_EXPLICIT_REQUEST,
} db_renderer_strategy_reason_t;

typedef struct {
    db_frame_plan_request_t plan_request;
    db_render_target_strategy_t target_strategy;
    db_render_operation_path_t gradient_path;
    uint64_t strategy_generation;
    uint64_t qualification_generation;
    uint64_t target_generation;
    db_renderer_strategy_reason_t strategy_reason;
    int rebuild_required;
    db_frame_rebuild_reason_t rebuild_reason;
} db_renderer_preflight_t;

typedef enum {
    DB_RENDERER_PREFLIGHT_CPU = 0,
    DB_RENDERER_PREFLIGHT_GL1_WINDOW,
    DB_RENDERER_PREFLIGHT_GL1_PERSISTENT,
    DB_RENDERER_PREFLIGHT_GL3_PERSISTENT,
    DB_RENDERER_PREFLIGHT_VULKAN_PERSISTENT,
} db_renderer_preflight_profile_t;

typedef struct {
    db_renderer_preflight_profile_t profile;
    db_gl1_target_request_t gl1_target_request;
    db_pixel_format_t working_format;
    db_gl1_direct_window_capabilities_t gl1_direct_window;
    db_render_target_strategy_t previous_strategy;
    uint64_t previous_target_generation;
    int direct_window_lineage_valid;
    db_frame_plan_request_t plan_request;
} db_renderer_preflight_policy_input_t;

typedef struct {
    uint64_t identity;
    uint64_t generation;
    db_render_target_strategy_t strategy;
    db_render_operation_path_t gradient_path;
    uint64_t strategy_generation;
    uint64_t qualification_generation;
    uint64_t target_generation;
    db_renderer_strategy_reason_t strategy_reason;
    int valid;
} db_renderer_target_t;

typedef struct {
    db_render_result_t result;
    db_target_content_state_t target_content;
    double critical_path_ms;
} db_renderer_frame_output_t;

db_render_operation_path_t
db_qualification_gradient_path(const db_qualification_snapshot_t *snapshot,
                               db_render_target_strategy_t target_strategy);
const char *
db_renderer_strategy_reason_name(db_renderer_strategy_reason_t reason);
int db_renderer_preflight_policy_resolve(
    const db_renderer_preflight_policy_input_t *input,
    const db_presenter_facts_t *presenter,
    const db_qualification_snapshot_t *qualification,
    db_renderer_preflight_t *preflight);
db_renderer_target_t
db_renderer_target_from_preflight(const db_renderer_preflight_t *preflight,
                                  uint64_t identity,
                                  uint64_t resource_generation);

typedef int (*db_presenter_acquire_fn_t)(void *context, uint32_t frame_index,
                                         db_presenter_facts_t *facts);
typedef int (*db_presenter_validate_fn_t)(void *context,
                                          const db_presenter_facts_t *facts);
typedef db_present_result_t (*db_presenter_present_fn_t)(
    void *context, const db_frame_plan_t *plan,
    const db_renderer_frame_output_t *output);

typedef int (*db_renderer_preflight_fn_t)(
    void *context, const db_presenter_facts_t *presenter,
    const db_frame_requirements_t *requirements,
    const db_qualification_snapshot_t *qualification,
    db_renderer_preflight_t *preflight);
typedef int (*db_renderer_provision_fn_t)(
    void *context, const db_renderer_preflight_t *preflight,
    db_renderer_target_t *target);
typedef int (*db_renderer_validate_fn_t)(void *context,
                                         const db_renderer_target_t *target);
typedef db_renderer_execute_status_t (*db_renderer_execute_fn_t)(
    void *context, const db_frame_plan_t *plan,
    const db_renderer_target_t *target, db_renderer_frame_output_t *output);
typedef void (*db_renderer_finalize_fn_t)(
    void *context, const db_frame_plan_t *plan,
    const db_renderer_frame_output_t *output, int commit);

typedef struct {
    db_presenter_acquire_fn_t acquire;
    db_presenter_validate_fn_t validate;
    db_presenter_present_fn_t present;
} db_presenter_frame_ops_t;

typedef struct {
    db_renderer_preflight_fn_t preflight;
    db_renderer_provision_fn_t provision;
    db_renderer_validate_fn_t validate;
    db_renderer_execute_fn_t execute;
    db_renderer_finalize_fn_t finalize;
} db_renderer_frame_ops_t;

#endif
