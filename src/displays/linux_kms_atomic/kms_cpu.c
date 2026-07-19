#include "../../core/db_frame_contracts.h"
#include "../../core/db_frame_plan.h"
#include "../../core/db_qualification_contracts.h"
#include "../../core/db_run_session.h"
#include "core/db_format_contract.h"
#include "core/db_render_result.h"
#include "kms_internal.h"

#include <gbm.h>
#include <stddef.h>
#include <stdint.h>

#include "../../core/db_geometry.h"
#include "../../core/db_log.h"
#include "../../core/db_numeric.h"
#include "../../core/db_render_types.h"
#include "../../renderers/cpu_renderer/cpu_renderer.h"
#include "../display_cpu_present_common.h"
#include "../display_frame_loop_common.h"
#include "../display_presentation_policy.h"

static struct fb *
db_cpu_create_scanout_fb(struct gbm_device *gbm, int fd, uint32_t width,
                         uint32_t height,
                         db_native_output_format_t native_format) {
    uint32_t bo_flags = GBM_BO_USE_SCANOUT;
#ifdef GBM_BO_USE_WRITE
    bo_flags |= GBM_BO_USE_WRITE;
#else
    bo_flags |= GBM_BO_USE_RENDERING;
#endif
    struct gbm_bo *bo = gbm_bo_create(
        gbm, width, height,
        db_kms_atomic_gbm_format_or_fail(BACKEND_NAME, native_format),
        bo_flags);
    if (bo == NULL) {
        runtime_failf("gbm_bo_create failed for CPU scanout buffer");
    }

    return fb_from_bo(fd, bo, 0);
}

static uint64_t
db_cpu_update_scanout_slot(db_kms_atomic_cpu_frame_producer_t *producer,
                           db_kms_cpu_scanout_slot_t *slot,
                           db_pixel_block_view_t damage) {
    uint32_t map_stride_bytes = 0U;
    void *map_data = NULL;
    uint8_t *const map_ptr =
        gbm_bo_map(slot->fb->bo, 0, 0, producer->width, producer->height,
                   GBM_BO_TRANSFER_WRITE, &map_stride_bytes, &map_data);
    if ((map_ptr == NULL) || (map_data == NULL)) {
        runtime_failf("gbm_bo_map failed for CPU scanout buffer");
    }
    uint64_t bytes_written = 0U;
    for (size_t index = 0U; index < damage.count; index++) {
        bytes_written += db_display_scale_surface_region_to_native(
            producer->backend, &producer->surface, map_ptr, producer->width,
            producer->height, map_stride_bytes, &damage.blocks[index],
            producer->native_output_format);
    }
    gbm_bo_unmap(slot->fb->bo, map_data);
    return bytes_written;
}

static db_renderer_execute_status_t
db_kms_cpu_execute(void *user_ctx, const db_frame_plan_t *plan,
                   const db_renderer_target_t *target,
                   db_renderer_frame_output_t *output) {
    db_kms_atomic_cpu_frame_producer_t *producer =
        (db_kms_atomic_cpu_frame_producer_t *)user_ctx;
    if ((producer == NULL) || (plan == NULL) || (target == NULL) ||
        (target->valid == 0) || (output == NULL)) {
        return DB_RENDER_FATAL;
    }
    (void)db_cpu_render_frame_to_surface(plan, &producer->surface, NULL);
    const uint64_t pending_serial = producer->present_serial + 1U;
    db_kms_cpu_scanout_slot_t *const slot =
        &producer->slots[producer->next_slot];
    const uint32_t slot_index = producer->next_slot;
    producer->next_slot =
        (producer->next_slot + 1U) % DB_KMS_CPU_SCANOUT_SLOT_COUNT;
    if (slot->fb == NULL) {
        slot->fb = db_cpu_create_scanout_fb(producer->gbm, producer->kms_fd,
                                            producer->width, producer->height,
                                            producer->native_output_format);
        slot->generation++;
        const db_log_field_t fields[] = {
            DB_LOG_U64("slot", slot_index),
            DB_LOG_U64("generation", slot->generation),
            DB_LOG_U64("width", producer->width),
            DB_LOG_U64("height", producer->height),
            DB_LOG_TOKEN("native_format", db_native_output_format_name(
                                              producer->native_output_format)),
        };
        db_log_info(producer->backend, "kms_scanout_slot_created", fields,
                    DB_LOG_FIELD_COUNT(fields));
    }

    const db_presentation_buffer_age_t age =
        db_presentation_buffer_age_from_serial(
            pending_serial, slot->last_present_serial, slot->valid,
            DB_PRESENTATION_DAMAGE_HISTORY_LENGTH);
    producer->pending_damage_history = producer->damage_history;
    int force_full = 1;
    const size_t logical_count = db_presentation_damage_history_resolve_ir(
        &producer->pending_damage_history, &age, &plan->update_ir,
        plan->update_metadata.damage_region, plan->grid_rows, plan->grid_cols,
        producer->logical_damage, DB_PRESENTATION_DAMAGE_RECTS_PER_FRAME,
        &force_full);
    int overflow = 0;
    size_t pixel_count = db_presentation_map_logical_damage(
        (db_grid_block_view_t){
            .blocks = producer->logical_damage,
            .count = logical_count,
        },
        plan->grid_rows, plan->grid_cols, producer->width, producer->height,
        producer->pixel_damage, DB_PRESENTATION_DAMAGE_RECTS_PER_FRAME,
        &overflow);
    if ((overflow != 0) || (pixel_count == 0U)) {
        producer->pixel_damage[0] = (db_damage_block_t){
            .row_count = producer->height,
            .col_count = producer->width,
        };
        pixel_count = 1U;
        force_full = 1;
    }
    const uint64_t bytes_written =
        db_cpu_update_scanout_slot(producer, slot,
                                   (db_pixel_block_view_t){
                                       .blocks = producer->pixel_damage,
                                       .count = pixel_count,
                                   });
    producer->pending_slot = slot_index;
    producer->pending_slot_valid = 1;
    producer->pending_damage_history_valid = 1;
    producer->transaction.pending_fb = slot->fb;
    if (force_full != 0) {
        const db_log_field_t fields[] = {
            DB_LOG_U64("slot", slot_index),
            DB_LOG_U64("frame", plan->frame_index),
            DB_LOG_U64("bytes_written", bytes_written),
            DB_LOG_TOKEN("reason", age.fallback_reason),
        };
        db_log_info(producer->backend, "kms_scanout_full_update", fields,
                    DB_LOG_FIELD_COUNT(fields));
    }
    output->result = db_render_result_success();
    db_cpu_execution_report(&output->result.execution);
    output->target_content = DB_TARGET_CONTENT_VALID_UNCOMMITTED;
    return DB_RENDER_EXECUTED;
}

static int db_kms_cpu_acquire(void *user_ctx, uint32_t frame_index,
                              db_presenter_facts_t *facts) {
    (void)frame_index;
    const db_kms_atomic_cpu_frame_producer_t *const producer =
        (const db_kms_atomic_cpu_frame_producer_t *)user_ctx;
    if ((producer == NULL) || (facts == NULL)) {
        return 0;
    }
    *facts = (db_presenter_facts_t){
        .destination_width = producer->width,
        .destination_height = producer->height,
        .generation = producer->transaction.generation,
        .valid = 1,
    };
    return 1;
}

static int db_kms_cpu_presenter_validate(void *user_ctx,
                                         const db_presenter_facts_t *facts) {
    const db_kms_atomic_cpu_frame_producer_t *const producer =
        (const db_kms_atomic_cpu_frame_producer_t *)user_ctx;
    return DB_BOOL((producer != NULL) && (facts != NULL) &&
                   (facts->generation == producer->transaction.generation));
}

static db_present_result_t
db_kms_cpu_present(void *user_ctx, const db_frame_plan_t *plan,
                   const db_renderer_frame_output_t *output) {
    (void)plan;
    db_kms_atomic_cpu_frame_producer_t *const producer =
        (db_kms_atomic_cpu_frame_producer_t *)user_ctx;
    return (producer != NULL)
               ? db_kms_present_pending(&producer->transaction, output)
               : DB_PRESENT_FATAL;
}

static int
db_kms_cpu_preflight(void *user_ctx, const db_presenter_facts_t *presenter,
                     const db_frame_requirements_t *requirements,
                     const db_qualification_snapshot_t *qualification,
                     db_renderer_preflight_t *preflight) {
    const db_kms_atomic_cpu_frame_producer_t *const producer =
        (const db_kms_atomic_cpu_frame_producer_t *)user_ctx;
    (void)requirements;
    if ((producer == NULL) || (preflight == NULL)) {
        return 0;
    }
    return db_renderer_preflight_policy_resolve(
        &(const db_renderer_preflight_policy_input_t){
            .profile = DB_RENDERER_PREFLIGHT_CPU,
            .plan_request =
                {
                    .pixel_width = producer->surface.pixel_width,
                    .pixel_height = producer->surface.pixel_height,
                },
            .rebuild_required =
                DB_BOOL(producer->transaction.initial_modeset != 0),
            .rebuild_reason = DB_FRAME_REBUILD_INITIAL_TARGET,
        },
        presenter, qualification, preflight);
}

static int db_kms_cpu_provision(void *user_ctx,
                                const db_renderer_preflight_t *preflight,
                                db_renderer_target_t *target) {
    const db_kms_atomic_cpu_frame_producer_t *const producer =
        (const db_kms_atomic_cpu_frame_producer_t *)user_ctx;
    if ((producer == NULL) || (preflight == NULL) || (target == NULL)) {
        return 0;
    }
    *target = (db_renderer_target_t){
        .identity = 1U,
        .generation = producer->transaction.generation,
        .strategy = preflight->target_strategy,
        .valid = 1,
    };
    return 1;
}

static int db_kms_cpu_target_validate(void *user_ctx,
                                      const db_renderer_target_t *target) {
    const db_kms_atomic_cpu_frame_producer_t *const producer =
        (const db_kms_atomic_cpu_frame_producer_t *)user_ctx;
    return DB_BOOL((producer != NULL) && (target != NULL) &&
                   (target->valid != 0) &&
                   (target->generation == producer->transaction.generation));
}

static void db_kms_cpu_finalize(void *user_ctx, const db_frame_plan_t *plan,
                                const db_renderer_frame_output_t *output,
                                int commit) {
    (void)plan;
    (void)output;
    db_kms_atomic_cpu_frame_producer_t *const producer =
        (db_kms_atomic_cpu_frame_producer_t *)user_ctx;
    if ((producer == NULL) || (producer->pending_slot_valid == 0)) {
        return;
    }
    db_kms_cpu_scanout_slot_t *const slot =
        &producer->slots[producer->pending_slot];
    if (commit != 0) {
        producer->present_serial++;
        slot->valid = 1;
        slot->last_present_serial = producer->present_serial;
        if (producer->pending_damage_history_valid != 0) {
            producer->damage_history = producer->pending_damage_history;
        }
    } else {
        slot->valid = 0;
        producer->transaction.pending_fb = NULL;
    }
    producer->pending_slot_valid = 0;
    producer->pending_damage_history_valid = 0;
}

db_display_frame_loop_result_t db_kms_atomic_cpu_frame(void *user_ctx,
                                                       uint32_t frame_index) {
    db_kms_atomic_cpu_frame_producer_t *const producer =
        (db_kms_atomic_cpu_frame_producer_t *)user_ctx;
    if ((producer == NULL) || (producer->transaction.session == NULL)) {
        return DB_DISPLAY_FRAME_LOOP_STOP;
    }
    (void)frame_index;
    const db_run_step_result_t result =
        db_run_session_step(producer->transaction.session);
    return db_display_frame_loop_from_run_step(&result, NULL);
}

int db_kms_cpu_run_session_init(db_kms_atomic_cpu_frame_producer_t *producer) {
    if ((producer == NULL) || (producer->resolved_runtime == NULL)) {
        return 0;
    }
    producer->transaction.generation = 1U;
    return db_run_session_create(
               &(const db_run_session_config_t){
                   .benchmark =
                       {
                           .benchmark_configuration =
                               &producer->resolved_runtime->benchmark,
                           .working_format =
                               producer->resolved_runtime->renderer.format
                                   .surface_pixel_format,
                       },
                   .presenter_ops =
                       {
                           .acquire = db_kms_cpu_acquire,
                           .validate = db_kms_cpu_presenter_validate,
                           .present = db_kms_cpu_present,
                       },
                   .renderer_ops =
                       {
                           .preflight = db_kms_cpu_preflight,
                           .provision = db_kms_cpu_provision,
                           .validate = db_kms_cpu_target_validate,
                           .execute = db_kms_cpu_execute,
                           .finalize = db_kms_cpu_finalize,
                       },
                   .presenter_context = producer,
                   .renderer_context = producer,
                   .fps_cap = producer->resolved_runtime->display.fps_cap,
                   .frame_limit =
                       producer->resolved_runtime->display.frame_limit,
                   .initial_frame_index = 1U,
                   .recent_metrics_enabled = db_display_dual_metrics_enabled(),
               },
               &producer->transaction.session) == DB_RUN_SESSION_OK;
}

void db_kms_atomic_cpu_scanout_shutdown(
    db_kms_atomic_cpu_frame_producer_t *producer) {
    if (producer == NULL) {
        return;
    }
    for (uint32_t index = 0U; index < DB_KMS_CPU_SCANOUT_SLOT_COUNT; index++) {
        fb_release(producer->kms_fd, NULL, producer->slots[index].fb);
        producer->slots[index].fb = NULL;
        producer->slots[index].valid = 0;
    }
}
