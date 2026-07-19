#include "db_benchmark_core.h"

#include "benchmarks/db_benchmark_checkpoint_internal.h"
#include "benchmarks/db_benchmark_runtime_internal.h"
#include "core/db_core.h"
#include "core/db_frame_plan.h"
#include "core/db_hash.h"
#include "core/db_log.h"
#include "core/db_render_types.h"

#include <stddef.h>
#include <stdint.h>

static uint64_t checkpoint_requirements_token(const db_benchmark_core_t *core,
                                              uint32_t width, uint32_t height,
                                              uint32_t frame_index,
                                              uint64_t content_revision,
                                              size_t required_bytes) {
    uint64_t token = db_benchmark_runtime_state_hash_cross_renderer(
        &core->runtime, width, height);
    token = db_fnv1a64_mix_u64(token, (uint64_t)core->working_format);
    token = db_fnv1a64_mix_u64(token, content_revision);
    token = db_fnv1a64_mix_u64(token, frame_index);
    return db_fnv1a64_mix_u64(token, required_bytes);
}

static db_frame_checkpoint_binding_t
checkpoint_binding(const db_benchmark_checkpoint_t *checkpoint) {
    if ((checkpoint == NULL) || (checkpoint->enabled == 0)) {
        return (db_frame_checkpoint_binding_t){0};
    }
    uint64_t token =
        db_fnv1a64_mix_u64(DB_FNV1A64_OFFSET, checkpoint->generation);
    token = db_fnv1a64_mix_u64(token, checkpoint->content_revision);
    token = db_fnv1a64_mix_u64(token, checkpoint->surface.pixel_width);
    token = db_fnv1a64_mix_u64(token, checkpoint->surface.pixel_height);
    token = db_fnv1a64_mix_u64(token, checkpoint->surface.format);
    return (db_frame_checkpoint_binding_t){
        .binding_token = token,
        .resource_generation = checkpoint->generation,
        .content_revision = checkpoint->content_revision,
        .width = checkpoint->surface.pixel_width,
        .height = checkpoint->surface.pixel_height,
        .format = checkpoint->surface.format,
        .valid = 1,
    };
}

db_frame_plan_status_t
db_benchmark_core_probe_frame(const db_benchmark_core_t *core,
                              uint32_t frame_index,
                              db_frame_requirements_t *requirements) {
    if ((core == NULL) || (requirements == NULL) || (core->initialized == 0)) {
        return DB_FRAME_PLAN_INVALID;
    }
    *requirements = (db_frame_requirements_t){0};
    if (core->runtime_flags.pattern.is_snake_region_mode == 0) {
        return DB_FRAME_PLAN_OK;
    }
    const uint32_t width = db_grid_cols_effective();
    const uint32_t height = db_grid_rows_effective();
    size_t allocation_bytes = 0U;
    if (db_benchmark_checkpoint_preflight(width, height, core->working_format,
                                          &allocation_bytes) !=
        DB_BENCHMARK_CHECKPOINT_OK) {
        return DB_FRAME_PLAN_CHECKPOINT_UNAVAILABLE;
    }
    const size_t pixel_bytes = (core->working_format == DB_PIXEL_FORMAT_RGBA16F)
                                   ? DB_RGBA16F_BYTES_PER_PIXEL
                                   : DB_RGBA8_BYTES_PER_PIXEL;
    size_t row_stride = 0U;
    if (db_try_mul_size((size_t)width, pixel_bytes, &row_stride) == 0) {
        return DB_FRAME_PLAN_ARITHMETIC_OVERFLOW;
    }
    *requirements = (db_frame_requirements_t){
        .checkpoint_format = core->working_format,
        .checkpoint_width = width,
        .checkpoint_height = height,
        .checkpoint_row_stride_bytes = row_stride,
        .checkpoint_allocation_bytes = allocation_bytes,
        .committed_revision = core->checkpoint.content_revision,
        .requirements_token = checkpoint_requirements_token(
            core, width, height, frame_index, core->checkpoint.content_revision,
            allocation_bytes),
        .frame_index = frame_index,
        .checkpoint_required = 1,
    };
    return (core->checkpoint.enabled != 0) ? DB_FRAME_PLAN_OK
                                           : DB_FRAME_PLAN_CHECKPOINT_REQUIRED;
}

db_frame_plan_status_t db_benchmark_core_provision_requirements(
    db_benchmark_core_t *core, const db_frame_requirements_t *requirements,
    db_frame_checkpoint_binding_t *binding) {
    if ((core == NULL) || (requirements == NULL) ||
        (requirements->checkpoint_required == 0)) {
        return DB_FRAME_PLAN_INVALID;
    }
    if ((requirements->checkpoint_width != db_grid_cols_effective()) ||
        (requirements->checkpoint_height != db_grid_rows_effective()) ||
        (requirements->checkpoint_format != core->working_format) ||
        (requirements->committed_revision !=
         core->checkpoint.content_revision) ||
        (requirements->requirements_token !=
         checkpoint_requirements_token(
             core, requirements->checkpoint_width,
             requirements->checkpoint_height, requirements->frame_index,
             requirements->committed_revision,
             requirements->checkpoint_allocation_bytes))) {
        return DB_FRAME_PLAN_INVALID;
    }
    if (core->checkpoint.enabled != 0) {
        const db_frame_checkpoint_binding_t current =
            checkpoint_binding(&core->checkpoint);
        if ((current.content_revision != requirements->committed_revision) ||
            (current.width != requirements->checkpoint_width) ||
            (current.height != requirements->checkpoint_height) ||
            (current.format != requirements->checkpoint_format)) {
            return DB_FRAME_PLAN_INVALID;
        }
        core->provisioned_requirements = *requirements;
        core->checkpoint_binding = current;
        if (binding != NULL) {
            *binding = current;
        }
        return DB_FRAME_PLAN_OK;
    }
    double seed_rgb[3] = {0.0, 0.0, 0.0};
    db_benchmark_seed_background_color_rgb3(&core->runtime, seed_rgb);
    const db_benchmark_checkpoint_status_t status =
        db_benchmark_checkpoint_init(
            &core->checkpoint, db_grid_cols_effective(),
            db_grid_rows_effective(), core->working_format, seed_rgb);
    if (status != DB_BENCHMARK_CHECKPOINT_OK) {
        const db_log_field_t fields[] = {
            DB_LOG_TOKEN("code", "checkpoint_unavailable"),
            DB_LOG_TOKEN("status", db_benchmark_checkpoint_status_name(status)),
            DB_LOG_U64("memory_budget_bytes",
                       DB_BENCHMARK_CHECKPOINT_MAX_BYTES),
        };
        db_log_error("benchmark", "checkpoint_allocation", fields,
                     DB_LOG_FIELD_COUNT(fields));
        return DB_FRAME_PLAN_CHECKPOINT_UNAVAILABLE;
    }
    core->provisioned_requirements = *requirements;
    core->checkpoint_binding = checkpoint_binding(&core->checkpoint);
    if (binding != NULL) {
        *binding = core->checkpoint_binding;
    }
    return DB_FRAME_PLAN_OK;
}
