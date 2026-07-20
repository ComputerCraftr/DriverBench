#include "kms_internal.h"

#include "../../core/db_core.h"
#include "../../core/db_frame_contracts.h"
#include "../../core/db_frame_plan.h"
#include "../../core/db_geometry.h"
#include "../../core/db_numeric.h"
#include "../../core/db_qualification_contracts.h"
#include "../../core/db_render_result.h"
#include "../../core/db_renderer_diagnostics.h"
#include "../../core/db_run_session.h"
#include "../display_frame_loop_common.h"
#include "../display_presentation_policy.h"
#include "../display_types.h"
#include "../gl_display_runtime.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglplatform.h>

#include <gbm.h>
#include <stddef.h>
#include <stdint.h>

static db_renderer_execute_status_t
db_kms_gl_execute(void *user_ctx, const db_frame_plan_t *plan,
                  const db_renderer_target_t *target,
                  db_renderer_frame_output_t *output) {
    db_kms_atomic_gl_frame_producer_t *producer =
        (db_kms_atomic_gl_frame_producer_t *)user_ctx;
    if ((producer == NULL) || (plan == NULL) || (target == NULL) ||
        (target->valid == 0) || (output == NULL)) {
        return DB_RENDER_FATAL;
    }
    db_display_gl_debug_clear_default_framebuffer_if_enabled(
        producer->debug_clear_default_framebuffer);
    producer->pending_presentation = producer->presentation;
    db_presentation_buffer_age_t age = db_presentation_buffer_age_resolve(
        DB_PRESENTATION_BUFFER_AGE_UNAVAILABLE, 0U,
        DB_PRESENTATION_DAMAGE_HISTORY_LENGTH);
    if (producer->presentation.buffer_age_supported != 0) {
        EGLint raw_age = 0;
        if (eglQuerySurface(producer->dpy, producer->surf, EGL_BUFFER_AGE_EXT,
                            &raw_age) == EGL_TRUE) {
            age = db_presentation_buffer_age_resolve(
                DB_PRESENTATION_BUFFER_AGE_EGL,
                db_nonnegative_int_to_u32_or_zero(raw_age),
                DB_PRESENTATION_DAMAGE_HISTORY_LENGTH);
        }
    }
    if ((producer->presentation.last_age_valid == 0) ||
        (producer->presentation.last_age.provider != age.provider) ||
        (producer->presentation.last_age.raw_age != age.raw_age) ||
        (producer->presentation.last_age.force_full_repair !=
         age.force_full_repair)) {
        db_presentation_log_buffer_age(producer->backend, &age);
        producer->presentation.last_age = age;
        producer->presentation.last_age_valid = 1;
    }
    int force_full = 1;
    const size_t logical_count = db_presentation_damage_history_resolve_ir(
        &producer->pending_presentation.damage_history, &age, &plan->update_ir,
        plan->update_metadata.damage_region, plan->grid_rows, plan->grid_cols,
        producer->pending_presentation.logical_damage,
        DB_PRESENTATION_DAMAGE_RECTS_PER_FRAME, &force_full);
    int map_overflow = 0;
    size_t pixel_count = db_presentation_map_logical_damage(
        (db_grid_block_view_t){
            .blocks = producer->pending_presentation.logical_damage,
            .count = logical_count,
        },
        plan->grid_rows, plan->grid_cols, producer->destination_width,
        producer->destination_height,
        producer->pending_presentation.pixel_damage,
        DB_PRESENTATION_DAMAGE_RECTS_PER_FRAME, &map_overflow);
    if (map_overflow != 0) {
        producer->pending_presentation.pixel_damage[0] = (db_damage_block_t){
            .row_count = producer->destination_height,
            .col_count = producer->destination_width,
        };
        pixel_count = 1U;
        force_full = 1;
    }
    const db_gl_presentation_frame_t presentation = {
        .destination_width = producer->destination_width,
        .destination_height = producer->destination_height,
        .damage =
            (db_pixel_block_view_t){
                .blocks = producer->pending_presentation.pixel_damage,
                .count = pixel_count,
            },
        .buffer_age = age,
        .force_full = force_full,
        .repair_reason = force_full != 0 ? age.fallback_reason : "none",
    };
    if (producer->renderer->render_frame(plan, target, &presentation) == 0) {
        output->target_content = DB_TARGET_CONTENT_PARTIALLY_MODIFIED;
        return DB_RENDER_FATAL;
    }
    EGLBoolean swapped = EGL_FALSE;
    if ((producer->presentation.swap_damage_supported != 0) &&
        (producer->presentation.swap_buffers_with_damage != NULL) &&
        (pixel_count > 0U)) {
        EGLint rects[DB_PRESENTATION_DAMAGE_RECTS_PER_FRAME * 4U];
        for (size_t index = 0U; index < pixel_count; index++) {
            const db_damage_block_t *const block =
                &producer->pending_presentation.pixel_damage[index];
            const size_t base = index * 4U;
            rects[base] = (EGLint)block->col_start;
            rects[base + 1U] = (EGLint)(producer->destination_height -
                                        block->row_start - block->row_count);
            rects[base + 2U] = (EGLint)block->col_count;
            rects[base + 3U] = (EGLint)block->row_count;
        }
        swapped = producer->presentation.swap_buffers_with_damage(
            producer->dpy, producer->surf, rects, (EGLint)pixel_count);
    } else {
        swapped = eglSwapBuffers(producer->dpy, producer->surf);
    }
    if (swapped != EGL_TRUE) {
        runtime_failf("EGL swap failed");
    }

    struct gbm_bo *next_bo = gbm_surface_lock_front_buffer(producer->gbm_surf);
    if (next_bo == NULL) {
        output->target_content = DB_TARGET_CONTENT_PARTIALLY_MODIFIED;
        return DB_RENDER_FATAL;
    }
    producer->transaction.pending_fb = fb_from_bo(producer->kms_fd, next_bo, 1);
    output->result = db_render_result_success();
    producer->renderer->execution_report(&output->result.execution);
    output->target_content = DB_TARGET_CONTENT_VALID_UNCOMMITTED;
    producer->pending_presentation_valid = 1;
    return DB_RENDER_EXECUTED;
}

static int db_kms_gl_acquire(void *user_ctx, uint32_t frame_index,
                             db_presenter_facts_t *facts) {
    (void)frame_index;
    db_kms_atomic_gl_frame_producer_t *const producer =
        (db_kms_atomic_gl_frame_producer_t *)user_ctx;
    if ((producer == NULL) || (facts == NULL)) {
        return 0;
    }
    *facts = (db_presenter_facts_t){
        .destination_width = producer->destination_width,
        .destination_height = producer->destination_height,
        .generation = producer->transaction.generation,
        .prior_content_state = (producer->transaction.initial_modeset != 0)
                                   ? DB_TARGET_CONTENT_LOST
                                   : DB_TARGET_CONTENT_UNCHANGED,
        .target_recreated = producer->transaction.initial_modeset,
        .valid = 1,
    };
    return 1;
}

static int db_kms_gl_presenter_validate(void *user_ctx,
                                        const db_presenter_facts_t *facts) {
    const db_kms_atomic_gl_frame_producer_t *const producer =
        (const db_kms_atomic_gl_frame_producer_t *)user_ctx;
    return DB_BOOL((producer != NULL) && (facts != NULL) &&
                   (facts->generation == producer->transaction.generation));
}

static db_present_result_t
db_kms_gl_present(void *user_ctx, const db_frame_plan_t *plan,
                  const db_renderer_frame_output_t *output) {
    (void)plan;
    db_kms_atomic_gl_frame_producer_t *const producer =
        (db_kms_atomic_gl_frame_producer_t *)user_ctx;
    return (producer != NULL)
               ? db_kms_present_pending(&producer->transaction, output)
               : DB_PRESENT_FATAL;
}

static int db_kms_gl_preflight(void *user_ctx,
                               const db_presenter_facts_t *presenter,
                               const db_frame_requirements_t *requirements,
                               const db_qualification_snapshot_t *qualification,
                               db_renderer_preflight_t *preflight) {
    const db_kms_atomic_gl_frame_producer_t *const producer =
        (const db_kms_atomic_gl_frame_producer_t *)user_ctx;
    (void)requirements;
    if ((producer == NULL) || (preflight == NULL)) {
        return 0;
    }
    const int is_gl1 =
        DB_BOOL(producer->gl_renderer == DB_GL_RENDERER_GL1_5_GLES1_1);
    return db_renderer_preflight_policy_resolve(
        &(const db_renderer_preflight_policy_input_t){
            .profile = is_gl1 != 0 ? DB_RENDERER_PREFLIGHT_GL1_PERSISTENT
                                   : DB_RENDERER_PREFLIGHT_GL3_PERSISTENT,
            .plan_request =
                {
                    .pixel_width = producer->pixel_width,
                    .pixel_height = producer->pixel_height,
                },
        },
        presenter, qualification, preflight);
}

static int db_kms_gl_provision(void *user_ctx,
                               const db_renderer_preflight_t *preflight,
                               db_renderer_target_t *target) {
    const db_kms_atomic_gl_frame_producer_t *const producer =
        (const db_kms_atomic_gl_frame_producer_t *)user_ctx;
    if ((producer == NULL) || (preflight == NULL) || (target == NULL)) {
        return 0;
    }
    *target = db_renderer_target_from_preflight(
        preflight, 1U, producer->transaction.generation);
    return 1;
}

static int db_kms_gl_target_validate(void *user_ctx,
                                     const db_renderer_target_t *target) {
    const db_kms_atomic_gl_frame_producer_t *const producer =
        (const db_kms_atomic_gl_frame_producer_t *)user_ctx;
    return DB_BOOL((producer != NULL) && (target != NULL) &&
                   (target->valid != 0) &&
                   (target->generation == producer->transaction.generation));
}

static void db_kms_gl_finalize(void *user_ctx, const db_frame_plan_t *plan,
                               const db_renderer_frame_output_t *output,
                               int commit) {
    (void)plan;
    (void)output;
    db_kms_atomic_gl_frame_producer_t *const producer =
        (db_kms_atomic_gl_frame_producer_t *)user_ctx;
    if (producer == NULL) {
        return;
    }
    if ((commit != 0) && (producer->pending_presentation_valid != 0)) {
        producer->presentation = producer->pending_presentation;
    } else if (producer->transaction.pending_fb != NULL) {
        fb_release(producer->kms_fd, producer->gbm_surf,
                   producer->transaction.pending_fb);
        producer->transaction.pending_fb = NULL;
    }
    producer->pending_presentation_valid = 0;
}

db_display_frame_loop_result_t db_kms_atomic_gl_frame(void *user_ctx,
                                                      uint32_t frame_index) {
    db_kms_atomic_gl_frame_producer_t *const producer =
        (db_kms_atomic_gl_frame_producer_t *)user_ctx;
    if ((producer == NULL) || (producer->transaction.session == NULL)) {
        return DB_DISPLAY_FRAME_LOOP_STOP;
    }
    (void)frame_index;
    const db_run_step_result_t result =
        db_run_session_step(producer->transaction.session);
    return db_display_frame_loop_from_run_step(&result, NULL);
}

int db_kms_gl_run_session_init(db_kms_atomic_gl_frame_producer_t *producer) {
    if ((producer == NULL) || (producer->resolved_runtime == NULL) ||
        (producer->renderer == NULL) ||
        (producer->renderer->qualification_ops == NULL)) {
        return 0;
    }
    producer->transaction.generation = 1U;
    const db_renderer_diagnostic_config_t *const diagnostics =
        &producer->resolved_runtime->renderer.diagnostics;
    const int diagnostic_forced =
        (producer->gl_renderer == DB_GL_RENDERER_GL1_5_GLES1_1)
            ? diagnostics->gl1_gradient != DB_GL1_GRADIENT_AUTO
            : diagnostics->gl3_gradient != DB_GL3_GRADIENT_AUTO;
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
                           .acquire = db_kms_gl_acquire,
                           .validate = db_kms_gl_presenter_validate,
                           .present = db_kms_gl_present,
                       },
                   .renderer_ops =
                       {
                           .preflight = db_kms_gl_preflight,
                           .provision = db_kms_gl_provision,
                           .validate = db_kms_gl_target_validate,
                           .execute = db_kms_gl_execute,
                           .finalize = db_kms_gl_finalize,
                       },
                   .qualification_ops = *producer->renderer->qualification_ops,
                   .qualification_query =
                       {
                           .ignore_cache =
                               diagnostics->ignore_conformance_cache,
                           .rerun_probe = diagnostics->rerun_conformance_probe,
                           .diagnostic_forced = diagnostic_forced,
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
