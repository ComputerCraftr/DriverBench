#include "../../core/db_frame_plan.h"
#include "../../core/db_frame_source.h"
#include "core/db_format_contract.h"
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

struct fb *db_kms_atomic_next_cpu_fb(void *user_ctx, uint32_t frame_index) {
    db_kms_atomic_cpu_frame_producer_t *producer =
        (db_kms_atomic_cpu_frame_producer_t *)user_ctx;
    if (producer == NULL) {
        return NULL;
    }
    db_frame_plan_t plan = {0};
    db_frame_source_generate(
        producer->core, frame_index,
        &(const db_frame_plan_request_t){
            .pixel_width = producer->surface.pixel_width,
            .pixel_height = producer->surface.pixel_height,
            .force_rebuild = DB_BOOL(frame_index == 0U),
            .rebuild_reason = DB_FRAME_REBUILD_INITIAL_TARGET,
        },
        &plan);
    (void)db_cpu_render_frame_to_surface(&plan, &producer->surface, NULL);
    db_frame_source_commit_success(producer->core, &plan);
    producer->present_serial++;
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
            producer->present_serial, slot->last_present_serial, slot->valid,
            DB_PRESENTATION_DAMAGE_HISTORY_LENGTH);
    int force_full = 1;
    const size_t logical_count = db_presentation_damage_history_resolve(
        &producer->damage_history, &age, plan.geometry.logical_damage,
        plan.grid_rows, plan.grid_cols, producer->logical_damage,
        DB_PRESENTATION_DAMAGE_RECTS_PER_FRAME, &force_full);
    int overflow = 0;
    size_t pixel_count = db_presentation_map_logical_damage(
        (db_grid_block_view_t){
            .blocks = producer->logical_damage,
            .count = logical_count,
        },
        plan.grid_rows, plan.grid_cols, producer->width, producer->height,
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
    slot->valid = 1;
    slot->last_present_serial = producer->present_serial;
    if (force_full != 0) {
        const db_log_field_t fields[] = {
            DB_LOG_U64("slot", slot_index),
            DB_LOG_U64("frame", frame_index),
            DB_LOG_U64("bytes_written", bytes_written),
            DB_LOG_TOKEN("reason", age.fallback_reason),
        };
        db_log_info(producer->backend, "kms_scanout_full_update", fields,
                    DB_LOG_FIELD_COUNT(fields));
    }
    return slot->fb;
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
