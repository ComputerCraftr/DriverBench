#include <math.h>
#include <stdint.h>

#include "renderer_vulkan_1_2_multi_gpu_internal.h"

// NOLINTBEGIN(misc-include-cleaner)

#define EMA_KEEP 0.9
#define EMA_NEW 0.1
#define HOST_COHERENT_MSCALE_NS 1e6
#define SECONDARY_DISPATCH_OVERHEAD_NS 40000.0
#define OWNER_SELECTION_EPSILON_NS 1.0
#define FRAME_TIME_EMA_KEEP 0.9
#define FRAME_TIME_EMA_NEW 0.1
#define FRAME_JITTER_EMA_KEEP 0.9
#define FRAME_JITTER_EMA_NEW 0.1
#define DB_VK_PERCENTILE_MAX 100.0

static inline double db_vk_predicted_lane_ns(double ms_per_unit,
                                             uint64_t work_units,
                                             double extra_ns) {
    if (ms_per_unit <= 0.0) {
        return 0.0;
    }
    return (ms_per_unit * HOST_COHERENT_MSCALE_NS * (double)work_units) +
           extra_ns;
}

static double db_vk_projected_makespan_ns(uint32_t gpu_count,
                                          const double *ema_ms_per_unit,
                                          const uint32_t *frame_work_units,
                                          uint32_t assign_owner,
                                          uint64_t assign_units) {
    double makespan_ns = 0.0;
    for (uint32_t g = 0U; g < gpu_count; g++) {
        const double lane_ms = ema_ms_per_unit[g];
        if (lane_ms <= 0.0) {
            continue;
        }
        uint64_t units = (frame_work_units != NULL) ? frame_work_units[g] : 0U;
        double extra_ns = 0.0;
        if (g == assign_owner) {
            units += assign_units;
            if (g != 0U) {
                extra_ns = SECONDARY_DISPATCH_OVERHEAD_NS;
            }
        }
        const double lane_ns =
            db_vk_predicted_lane_ns(lane_ms, units, extra_ns);
        if (lane_ns > makespan_ns) {
            makespan_ns = lane_ns;
        }
    }
    return makespan_ns;
}

uint32_t db_vk_select_owner_for_work(uint32_t gpu_count, uint32_t work_units,
                                     uint64_t budget_ns, uint64_t safety_ns,
                                     const double *ema_ms_per_unit,
                                     const uint32_t *frame_work_units) {
    // List-scheduling step:
    // evaluate every owner for this chunk and pick the owner that minimizes
    // projected frame makespan, subject to budget/safety limits.
    if ((gpu_count <= 1U) || (ema_ms_per_unit == NULL)) {
        return 0U;
    }
    if (budget_ns <= safety_ns) {
        return 0U;
    }

    const uint64_t units = (work_units > 0U) ? (uint64_t)work_units : 1ULL;
    const double budget_limit_ns = (double)(budget_ns - safety_ns);

    uint32_t best_owner = 0U;
    double best_makespan_ns = db_vk_projected_makespan_ns(
        gpu_count, ema_ms_per_unit, frame_work_units, 0U, units);
    if (best_makespan_ns > budget_limit_ns) {
        best_makespan_ns = 0.0;
    }

    for (uint32_t owner = 1U; owner < gpu_count; owner++) {
        const double lane_ms = ema_ms_per_unit[owner];
        if (lane_ms <= 0.0) {
            continue;
        }
        const double makespan_ns = db_vk_projected_makespan_ns(
            gpu_count, ema_ms_per_unit, frame_work_units, owner, units);
        if (makespan_ns > budget_limit_ns) {
            continue;
        }
        if ((best_makespan_ns <= 0.0) ||
            (makespan_ns + OWNER_SELECTION_EPSILON_NS < best_makespan_ns)) {
            best_owner = owner;
            best_makespan_ns = makespan_ns;
        }
    }
    return best_owner;
}

void db_vk_update_ema_fallback(uint32_t gpu_count,
                               const uint32_t *frame_work_units,
                               double frame_ms, double *ema_ms_per_work_unit) {
    if ((frame_work_units == NULL) || (ema_ms_per_work_unit == NULL)) {
        return;
    }
    uint32_t total_work_units = 0U;
    for (uint32_t g = 0; g < gpu_count; g++) {
        total_work_units += frame_work_units[g];
    }
    const double ms_per_work_unit =
        frame_ms / (double)((total_work_units > 0U) ? total_work_units : 1U);
    for (uint32_t g = 0; g < gpu_count; g++) {
        if (frame_work_units[g] == 0U) {
            continue;
        }
        ema_ms_per_work_unit[g] =
            (EMA_KEEP * ema_ms_per_work_unit[g]) + (EMA_NEW * ms_per_work_unit);
    }
}

void db_vk_scheduler_update_frame_pacing(double frame_ms, double *frame_ema_ms,
                                         double *frame_jitter_ema_ms) {
    if ((frame_ema_ms == NULL) || (frame_jitter_ema_ms == NULL)) {
        return;
    }
    if (*frame_ema_ms <= 0.0) {
        *frame_ema_ms = frame_ms;
        *frame_jitter_ema_ms = 0.0;
        return;
    }
    *frame_ema_ms = (FRAME_TIME_EMA_KEEP * (*frame_ema_ms)) +
                    (FRAME_TIME_EMA_NEW * frame_ms);
    const double jitter = fabs(frame_ms - (*frame_ema_ms));
    *frame_jitter_ema_ms = (FRAME_JITTER_EMA_KEEP * (*frame_jitter_ema_ms)) +
                           (FRAME_JITTER_EMA_NEW * jitter);
}

double db_vk_scheduler_percentile_sorted(const double *samples, size_t count,
                                         double pct) {
    if ((samples == NULL) || (count == 0U)) {
        return 0.0;
    }
    if (pct <= 0.0) {
        return samples[0U];
    }
    if (pct >= DB_VK_PERCENTILE_MAX) {
        return samples[count - 1U];
    }
    const double rank = ((pct / DB_VK_PERCENTILE_MAX) * (double)(count - 1U));
    const size_t index = (size_t)rank;
    const size_t next = (index + 1U < count) ? (index + 1U) : index;
    const double frac = rank - (double)index;
    return (samples[index] * (1.0 - frac)) + (samples[next] * frac);
}

// NOLINTEND(misc-include-cleaner)
