#ifndef DRIVERBENCH_RENDERER_BANDS_COMMON_H
#define DRIVERBENCH_RENDERER_BANDS_COMMON_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "../displays/bench_config.h"

#define DB_BAND_TRI_VERTS_PER_BAND 6U
#define DB_BAND_POS_FLOATS 2U
#define DB_BAND_COLOR_FLOATS 3U
#define DB_BAND_VERT_FLOATS (DB_BAND_POS_FLOATS + DB_BAND_COLOR_FLOATS)

static inline void db_fill_band_vertices_pos_rgb(float *out_vertices,
                                                 uint32_t band_count,
                                                 double time_s) {
    const float inv_band_count = 1.0F / (float)band_count;
    for (uint32_t band_index = 0; band_index < band_count; band_index++) {
        const float band_f = (float)band_index;
        const float x0 = ((2.0F * band_f) * inv_band_count) - 1.0F;
        const float x1 = ((2.0F * (band_f + 1.0F)) * inv_band_count) - 1.0F;
        const float pulse =
            BENCH_PULSE_BASE_F +
            (BENCH_PULSE_AMP_F * sinf((float)((time_s * BENCH_PULSE_FREQ_F) +
                                              (band_f * BENCH_PULSE_PHASE_F))));
        const float color_r =
            pulse * (BENCH_COLOR_R_BASE_F +
                     (BENCH_COLOR_R_SCALE_F * band_f / (float)band_count));
        const float color_g = pulse * BENCH_COLOR_G_SCALE_F;
        const float color_b = 1.0F - color_r;

        const size_t base_offset = (size_t)band_index *
                                   DB_BAND_TRI_VERTS_PER_BAND *
                                   DB_BAND_VERT_FLOATS;
        float *out = &out_vertices[base_offset];

        // Triangle 1
        *out++ = x0;
        *out++ = -1.0F;
        *out++ = color_r;
        *out++ = color_g;
        *out++ = color_b;

        *out++ = x1;
        *out++ = -1.0F;
        *out++ = color_r;
        *out++ = color_g;
        *out++ = color_b;

        *out++ = x1;
        *out++ = 1.0F;
        *out++ = color_r;
        *out++ = color_g;
        *out++ = color_b;

        // Triangle 2
        *out++ = x0;
        *out++ = -1.0F;
        *out++ = color_r;
        *out++ = color_g;
        *out++ = color_b;

        *out++ = x1;
        *out++ = 1.0F;
        *out++ = color_r;
        *out++ = color_g;
        *out++ = color_b;

        *out++ = x0;
        *out++ = 1.0F;
        *out++ = color_r;
        *out++ = color_g;
        *out++ = color_b;
    }
}

#endif
