#include <stdint.h>

#include "../../config/benchmark_config.h"
#include "../../core/db_core.h"
#include "renderer_vulkan_1_2_multi_gpu_internal.h"

// NOLINTBEGIN(misc-include-cleaner)

#define EMA_KEEP 0.9
#define EMA_NEW 0.1
#define HOST_COHERENT_MSCALE_NS 1e6
#define SLOW_GPU_RATIO_THRESHOLD 1.5

uint32_t db_vk_select_owner_for_work(uint32_t candidate_owner,
                                     uint32_t gpu_count, uint32_t work_units,
                                     uint64_t frame_start_ns,
                                     uint64_t budget_ns, uint64_t safety_ns,
                                     const double *ema_ms_per_unit) {
    uint32_t owner = candidate_owner;
    if (owner >= gpu_count) {
        owner = 0U;
    }
    if ((owner == 0U) || (gpu_count <= 1U) || (ema_ms_per_unit == NULL)) {
        return 0U;
    }

    const double base = ema_ms_per_unit[0];
    if (base > 0.0) {
        const double ratio = ema_ms_per_unit[owner] / base;
        if (ratio > SLOW_GPU_RATIO_THRESHOLD) {
            return 0U;
        }
    }

    const uint64_t predicted_ns =
        (uint64_t)(ema_ms_per_unit[owner] * HOST_COHERENT_MSCALE_NS *
                   (double)((work_units > 0U) ? work_units : 1U));
    const uint64_t now = db_now_ns_monotonic();
    if ((now + predicted_ns) > (frame_start_ns + budget_ns - safety_ns)) {
        return 0U;
    }
    return owner;
}

void db_vk_update_ema_fallback(db_pattern_t pattern, uint32_t gpu_count,
                               const uint32_t *work_owner,
                               const uint32_t *grid_tiles_per_gpu,
                               uint32_t grid_tiles_drawn, double frame_ms,
                               double *ema_ms_per_work_unit) {
    if (pattern == DB_PATTERN_BANDS) {
        const double ms_per_work_unit = frame_ms / (double)BENCH_BANDS;
        uint32_t bands_per_gpu[MAX_GPU_COUNT] = {0};
        for (uint32_t b = 0; b < BENCH_BANDS; b++) {
            bands_per_gpu[work_owner[b]]++;
        }
        for (uint32_t g = 0; g < gpu_count; g++) {
            if (bands_per_gpu[g] == 0U) {
                continue;
            }
            ema_ms_per_work_unit[g] = (EMA_KEEP * ema_ms_per_work_unit[g]) +
                                      (EMA_NEW * ms_per_work_unit);
        }
        return;
    }

    const double ms_per_tile =
        frame_ms / (double)((grid_tiles_drawn > 0U) ? grid_tiles_drawn : 1U);
    for (uint32_t g = 0; g < gpu_count; g++) {
        if (grid_tiles_per_gpu[g] == 0U) {
            continue;
        }
        ema_ms_per_work_unit[g] =
            (EMA_KEEP * ema_ms_per_work_unit[g]) + (EMA_NEW * ms_per_tile);
    }
}

// NOLINTEND(misc-include-cleaner)
