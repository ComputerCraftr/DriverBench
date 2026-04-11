#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "../../core/db_core.h"
#include "../../core/db_numeric.h"
#include "../../core/db_sort.h"
#include "vk_internal.h"

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
static const double vk_min_mean_improvement = 0.05;
static const double vk_max_p95_regression = 0.10;
static const double vk_p95_percentile = 95.0;
enum {
    DB_VK_SPLIT_COARSE_SHARE_COUNT = 3U,
    DB_VK_SPLIT_DEVIATION_FLOOR_NS = 50000U,
};

const char *db_vk_multi_gpu_phase_name(db_vk_multi_gpu_phase_t phase) {
    switch (phase) {
    case DB_VK_MULTI_GPU_CLOSED:
        return "closed";
    case DB_VK_MULTI_GPU_WARMING:
        return "warming";
    case DB_VK_MULTI_GPU_CALIBRATING:
        return "calibrating";
    case DB_VK_MULTI_GPU_VALIDATED:
        return "validated";
    case DB_VK_MULTI_GPU_ACTIVE:
        return "active";
    }
    return "invalid";
}

int db_vk_multi_gpu_phase_transition_valid(db_vk_multi_gpu_phase_t from,
                                           db_vk_multi_gpu_phase_t to) {
    if (to == DB_VK_MULTI_GPU_CLOSED) {
        return 1;
    }
    switch (from) {
    case DB_VK_MULTI_GPU_CLOSED:
        return DB_BOOL(to == DB_VK_MULTI_GPU_WARMING);
    case DB_VK_MULTI_GPU_WARMING:
        return DB_BOOL(to == DB_VK_MULTI_GPU_CALIBRATING);
    case DB_VK_MULTI_GPU_CALIBRATING:
        return DB_BOOL(to == DB_VK_MULTI_GPU_VALIDATED);
    case DB_VK_MULTI_GPU_VALIDATED:
        return DB_BOOL(to == DB_VK_MULTI_GPU_ACTIVE);
    case DB_VK_MULTI_GPU_ACTIVE:
        return 0;
    }
    return 0;
}

uint32_t db_vk_import_memory_type_bits(uint32_t exported_fd_type_bits,
                                       uint32_t alias_requirement_type_bits) {
    return exported_fd_type_bits & alias_requirement_type_bits;
}

uint64_t db_vk_timestamp_delta(uint64_t begin, uint64_t end,
                               uint32_t valid_bits) {
    if (valid_bits >= 64U) {
        return end - begin;
    }
    if (valid_bits == 0U) {
        return 0U;
    }
    const uint64_t mask = (UINT64_C(1) << valid_bits) - 1U;
    return (end - begin) & mask;
}

int db_vk_timestamp_deviation_acceptable(uint64_t deviation_ns,
                                         uint64_t critical_path_ns) {
    const uint64_t relative_limit = critical_path_ns / 20U;
    const uint64_t limit =
        DB_MAX((uint64_t)DB_VK_SPLIT_DEVIATION_FLOOR_NS, relative_limit);
    return DB_BOOL(deviation_ns <= limit);
}

static uint64_t vk_split_median(const db_vk_split_search_t *search,
                                uint32_t share_index) {
    uint64_t values[DB_VK_SPLIT_SAMPLES_PER_SHARE] = {0};
    const uint32_t base = share_index * DB_VK_SPLIT_SAMPLES_PER_SHARE;
    for (uint32_t index = 0U; index < DB_VK_SPLIT_SAMPLES_PER_SHARE; index++) {
        values[index] = search->samples[base + index].host_critical_path_ns;
    }
    for (uint32_t index = 1U; index < DB_VK_SPLIT_SAMPLES_PER_SHARE; index++) {
        const uint64_t value = values[index];
        uint32_t position = index;
        while ((position > 0U) && (values[position - 1U] > value)) {
            values[position] = values[position - 1U];
            position--;
        }
        values[position] = value;
    }
    return values[DB_VK_SPLIT_SAMPLES_PER_SHARE / 2U];
}

static uint64_t vk_split_uncertainty(const db_vk_split_search_t *search,
                                     uint32_t share_index) {
    uint64_t uncertainty = 0U;
    const uint32_t base = share_index * DB_VK_SPLIT_SAMPLES_PER_SHARE;
    for (uint32_t index = 0U; index < DB_VK_SPLIT_SAMPLES_PER_SHARE; index++) {
        uncertainty =
            DB_MAX(uncertainty, search->samples[base + index].uncertainty_ns);
    }
    return uncertainty;
}

static uint32_t vk_split_best_share_index(const db_vk_split_search_t *search,
                                          uint32_t share_count) {
    uint32_t best = 0U;
    for (uint32_t index = 1U; index < share_count; index++) {
        const uint64_t candidate = vk_split_median(search, index);
        const uint64_t current = vk_split_median(search, best);
        const uint64_t uncertainty = vk_split_uncertainty(search, index) +
                                     vk_split_uncertainty(search, best);
        const uint64_t delta =
            (candidate > current) ? candidate - current : current - candidate;
        if (((candidate < current) && (delta > uncertainty)) ||
            ((delta <= uncertainty) &&
             (search->shares_bps[index] < search->shares_bps[best]))) {
            best = index;
        }
    }
    return best;
}

uint32_t db_vk_split_search_next_share(const db_vk_split_search_t *search) {
    if ((search == NULL) || (search->complete != 0)) {
        return 0U;
    }
    static const uint32_t coarse[DB_VK_SPLIT_COARSE_SHARE_COUNT] = {
        2500U, 5000U, 7500U};
    if (search->share_index < DB_VK_SPLIT_COARSE_SHARE_COUNT) {
        return coarse[search->share_index];
    }
    const uint32_t best_index =
        vk_split_best_share_index(search, DB_VK_SPLIT_COARSE_SHARE_COUNT);
    const uint32_t best = search->shares_bps[best_index];
    const uint32_t lower = (best > 1250U) ? best - 1250U : 1250U;
    const uint32_t upper = DB_MIN(best + 1250U, 8750U);
    return (search->share_index == DB_VK_SPLIT_COARSE_SHARE_COUNT) ? lower
                                                                   : upper;
}

void db_vk_split_search_record(db_vk_split_search_t *search,
                               const db_vk_split_sample_t *sample) {
    if ((search == NULL) || (sample == NULL) || (search->complete != 0)) {
        return;
    }
    const uint32_t share = search->share_index;
    search->shares_bps[share] = db_vk_split_search_next_share(search);
    if (search->warmed[share] == 0U) {
        search->warmed[share] = 1U;
        return;
    }
    if (sample->valid == 0) {
        search->invalid_count[share]++;
        if (search->invalid_count[share] <= DB_VK_SPLIT_INVALID_RETRY_LIMIT) {
            return;
        }
        search->share_index++;
        return;
    }
    const uint32_t storage =
        (share * DB_VK_SPLIT_SAMPLES_PER_SHARE) + search->valid_count[share];
    search->samples[storage] = *sample;
    search->valid_count[share]++;
    search->sample_count++;
    if (search->valid_count[share] == DB_VK_SPLIT_SAMPLES_PER_SHARE) {
        search->share_index++;
    }
    if (search->share_index == DB_VK_SPLIT_SEARCH_SHARE_COUNT) {
        const uint32_t best =
            vk_split_best_share_index(search, DB_VK_SPLIT_SEARCH_SHARE_COUNT);
        search->selected_share_bps = search->shares_bps[best];
        search->complete = 1;
    }
}

void db_vk_calibration_state_open(db_vk_calibration_state_t *state) {
    if (state == NULL) {
        return;
    }
    *state = (db_vk_calibration_state_t){
        .phase = DB_VK_MULTI_GPU_WARMING,
    };
}

void db_vk_calibration_state_record(db_vk_calibration_state_t *state,
                                    const db_vk_calibration_pair_t *pair) {
    if ((state == NULL) || (pair == NULL)) {
        return;
    }
    if (state->phase == DB_VK_MULTI_GPU_WARMING) {
        state->warmup_count++;
        if (state->warmup_count == DB_VK_CALIBRATION_WARMUP_COUNT) {
            state->phase = DB_VK_MULTI_GPU_CALIBRATING;
        }
        return;
    }
    if ((state->phase != DB_VK_MULTI_GPU_CALIBRATING) ||
        (state->pair_count >= DB_VK_CALIBRATION_PAIR_COUNT)) {
        return;
    }
    state->pairs[state->pair_count++] = *pair;
    if (state->pair_count == DB_VK_CALIBRATION_PAIR_COUNT) {
        state->result = db_vk_evaluate_calibration(
            state->pairs, DB_VK_CALIBRATION_PAIR_COUNT);
        state->phase = state->result.activate ? DB_VK_MULTI_GPU_ACTIVE
                                              : DB_VK_MULTI_GPU_VALIDATED;
    }
}

db_vk_calibration_result_t
db_vk_evaluate_calibration(const db_vk_calibration_pair_t *pairs,
                           size_t pair_count) {
    db_vk_calibration_result_t result = {0};
    if ((pairs == NULL) || (pair_count != DB_VK_CALIBRATION_PAIR_COUNT)) {
        return result;
    }
    double improvements[DB_VK_CALIBRATION_PAIR_COUNT] = {0};
    double primary[DB_VK_CALIBRATION_PAIR_COUNT] = {0};
    double candidate[DB_VK_CALIBRATION_PAIR_COUNT] = {0};
    result.complete = 1;
    result.hashes_match = 1;
    result.timing_confident = 1;
    for (size_t index = 0U; index < pair_count; index++) {
        if ((pairs[index].primary_ms <= 0.0) ||
            (pairs[index].candidate_ms <= 0.0)) {
            result.complete = 0;
            return result;
        }
        const double primary_uncertainty_ms =
            (double)pairs[index].primary_uncertainty_ns / DB_NS_PER_MS;
        const double candidate_uncertainty_ms =
            (double)pairs[index].candidate_uncertainty_ns / DB_NS_PER_MS;
        primary[index] = pairs[index].primary_ms - primary_uncertainty_ms;
        candidate[index] = pairs[index].candidate_ms + candidate_uncertainty_ms;
        if (primary[index] <= 0.0) {
            result.timing_confident = 0;
            return result;
        }
        improvements[index] =
            (primary[index] - candidate[index]) / primary[index];
        if ((pairs[index].primary_state_hash !=
             pairs[index].candidate_state_hash) ||
            (pairs[index].primary_working_hash !=
             pairs[index].candidate_working_hash)) {
            result.hashes_match = 0;
        }
    }
    if ((db_sort_f64_ascending(improvements, pair_count) != DB_SORT_OK) ||
        (db_sort_f64_ascending(primary, pair_count) != DB_SORT_OK) ||
        (db_sort_f64_ascending(candidate, pair_count) != DB_SORT_OK)) {
        return (db_vk_calibration_result_t){0};
    }
    result.median_improvement =
        (improvements[(pair_count / 2U) - 1U] + improvements[pair_count / 2U]) /
        2.0;
    result.primary_p95_ms = db_vk_scheduler_percentile_sorted(
        primary, pair_count, vk_p95_percentile);
    result.candidate_p95_ms = db_vk_scheduler_percentile_sorted(
        candidate, pair_count, vk_p95_percentile);
    result.activate =
        DB_BOOL(result.hashes_match && result.timing_confident &&
                (result.median_improvement >= vk_min_mean_improvement) &&
                (result.candidate_p95_ms <=
                 result.primary_p95_ms * (1.0 + vk_max_p95_regression)));
    return result;
}

int db_vk_multi_gpu_measured_benefit(double primary_mean_ms,
                                     double candidate_mean_ms,
                                     double primary_p95_ms,
                                     double candidate_p95_ms) {
    if ((primary_mean_ms <= 0.0) || (candidate_mean_ms <= 0.0) ||
        (primary_p95_ms <= 0.0) || (candidate_p95_ms <= 0.0)) {
        return 0;
    }
    const double required_mean =
        primary_mean_ms * (1.0 - vk_min_mean_improvement);
    const double allowed_p95 = primary_p95_ms * (1.0 + vk_max_p95_regression);
    return DB_BOOL((candidate_mean_ms <= required_mean) &&
                   (candidate_p95_ms <= allowed_p95));
}

int db_vk_external_interop_usable(int platform_supported, int external_memory,
                                  int external_semaphore, int external_image) {
    return DB_BOOL(platform_supported && external_memory &&
                   external_semaphore && external_image);
}

db_vk_transport_profile_t
db_vk_negotiate_transport(const db_vk_transport_capabilities_t *capabilities) {
    db_vk_transport_profile_t profile = {
        .transport = DB_VK_TRANSPORT_UNSUPPORTED,
        .ownership_domain = DB_VK_EXTERNAL_OWNERSHIP_EXTERNAL,
    };
    if (capabilities == NULL) {
        return profile;
    }
    if (capabilities->device_group_peer_read != 0) {
        profile.transport = DB_VK_TRANSPORT_DEVICE_GROUP_PEER_IMAGE;
        profile.supported = 1;
        return profile;
    }
    if (capabilities->sync_fd_semaphore == 0) {
        return profile;
    }
    if (capabilities->foreign_domain_required != 0) {
        if (capabilities->foreign_domain_supported_by_both == 0) {
            return profile;
        }
        profile.ownership_domain = DB_VK_EXTERNAL_OWNERSHIP_FOREIGN;
    } else if (capabilities->external_domain_supported == 0) {
        return profile;
    }
    if ((capabilities->opaque_identity_compatible != 0) &&
        (capabilities->opaque_external_image != 0)) {
        profile.transport = DB_VK_TRANSPORT_OPAQUE_FD_IMAGE;
        profile.supported = 1;
    } else if ((capabilities->dma_buf_external_image != 0) &&
               (capabilities->dma_buf_modifier_compatible != 0)) {
        profile.transport = DB_VK_TRANSPORT_DMA_BUF_IMAGE;
        profile.supported = 1;
    } else if (capabilities->dma_buf_external_buffer != 0) {
        profile.transport = DB_VK_TRANSPORT_DMA_BUF_BUFFER;
        profile.supported = 1;
    }
    return profile;
}

static inline double
vk_predicted_lane_ns(double ms_per_unit, uint64_t work_units, double extra_ns) {
    if (ms_per_unit <= 0.0) {
        return 0.0;
    }
    return (ms_per_unit * HOST_COHERENT_MSCALE_NS * (double)work_units) +
           extra_ns;
}

static double vk_projected_makespan_ns(uint32_t gpu_count,
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
        const double lane_ns = vk_predicted_lane_ns(lane_ms, units, extra_ns);
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

    const uint64_t units = DB_MAX((uint64_t)work_units, 1ULL);
    const double budget_limit_ns = (double)(budget_ns - safety_ns);

    uint32_t best_owner = 0U;
    double best_makespan_ns = vk_projected_makespan_ns(
        gpu_count, ema_ms_per_unit, frame_work_units, 0U, units);
    if (best_makespan_ns > budget_limit_ns) {
        best_makespan_ns = 0.0;
    }

    for (uint32_t owner = 1U; owner < gpu_count; owner++) {
        const double lane_ms = ema_ms_per_unit[owner];
        if (lane_ms <= 0.0) {
            continue;
        }
        const double makespan_ns = vk_projected_makespan_ns(
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
    const uint32_t work_unit_divisor = DB_MAX(total_work_units, 1U);
    const double ms_per_work_unit = frame_ms / (double)work_unit_divisor;
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
    const size_t next = DB_MIN(index + 1U, count - 1U);
    const double frac = rank - (double)index;
    return (samples[index] * (1.0 - frac)) + (samples[next] * frac);
}
