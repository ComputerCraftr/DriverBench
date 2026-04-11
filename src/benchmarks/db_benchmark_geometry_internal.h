#ifndef DRIVERBENCH_BENCHMARK_GEOMETRY_INTERNAL_H
#define DRIVERBENCH_BENCHMARK_GEOMETRY_INTERNAL_H

#include "benchmarks/db_benchmark_runtime_internal.h"
#include <string.h>

static inline void db_band_color_rgb3(uint32_t band_index, uint32_t band_count,
                                      uint32_t frame_index, double *out_rgb) {
    if (out_rgb == NULL) {
        return;
    }
    const double band_value = (double)band_index;
    const double frame_value = (double)frame_index;
    const double pulse_value =
        BENCH_PULSE_BASE +
        (BENCH_PULSE_AMP * sin((frame_value * BENCH_PULSE_FREQ) +
                               (band_value * BENCH_PULSE_PHASE)));
    const double color_r_value =
        pulse_value * (BENCH_COLOR_R_BASE +
                       (BENCH_COLOR_R_SCALE * band_value / (double)band_count));
    const double band_rgb[3] = {
        color_r_value, pulse_value * BENCH_COLOR_G_SCALE, 1.0 - color_r_value};
    memcpy(out_rgb, band_rgb, 3U * sizeof(double));
}

static inline double db_color_channel(uint32_t seed) {
    const double normalized = db_u8_to_unit_f64(seed);
    return DB_COLOR_CHANNEL_BIAS + (normalized * DB_COLOR_CHANNEL_SCALE);
}

static inline void db_palette_cycle_color_rgb3(uint32_t cycle_index,
                                               double *out_rgb) {
    if (out_rgb == NULL) {
        return;
    }
    const uint32_t seed_base = db_mix_u32(
        ((cycle_index + 1U) * DB_PALETTE_SALT_BASE_STEP) ^ DB_U32_SALT_PALETTE);
    const double palette_rgb[3] = {
        db_color_channel(db_mix_u32(seed_base ^ DB_U32_SALT_COLOR_R)),
        db_color_channel(db_mix_u32(seed_base ^ DB_U32_SALT_COLOR_G)),
        db_color_channel(db_mix_u32(seed_base ^ DB_U32_SALT_COLOR_B)),
    };
    memcpy(out_rgb, palette_rgb, 3U * sizeof(double));
}

#endif
