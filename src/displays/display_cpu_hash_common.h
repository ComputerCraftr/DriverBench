#ifndef DRIVERBENCH_DISPLAY_CPU_HASH_COMMON_H
#define DRIVERBENCH_DISPLAY_CPU_HASH_COMMON_H

#include <stddef.h>
#include <stdint.h>

#include "../core/db_core.h"
#include "../core/db_hash.h"
#include "../renderers/cpu_renderer/renderer_cpu_renderer.h"
#include "../renderers/renderer_benchmark_common.h"
#include "display_runtime_config_common.h"

static inline uint64_t
db_display_cpu_renderer_bo_hash_or_fail(const char *backend) {
    uint32_t pixel_width = 0U;
    uint32_t pixel_height = 0U;
    if (db_renderer_cpu_renderer_is_hdr_float_bo() != 0) {
        const uint16_t *pixels = db_renderer_cpu_renderer_pixels_rgba16f(
            &pixel_width, &pixel_height);
        if (pixels == NULL) {
            db_failf(backend, "cpu renderer returned invalid HDR framebuffer");
        }
        return db_hash_rgba16f_pixels_canonical(
            pixels, pixel_width, pixel_height,
            (size_t)pixel_width * 4U * sizeof(uint16_t), 0);
    }

    const uint32_t *pixels =
        db_renderer_cpu_renderer_pixels_rgba8(&pixel_width, &pixel_height);
    if (pixels == NULL) {
        db_failf(backend, "cpu renderer returned invalid framebuffer");
    }
    return db_hash_rgba8_pixels_canonical((const uint8_t *)pixels, pixel_width,
                                          pixel_height,
                                          (size_t)pixel_width * 4U, 0);
}

typedef void (*db_display_cpu_present_damage_cb_t)(
    const db_damage_block_t *damage_blocks, size_t damage_count,
    void *user_data);

static inline void db_display_cpu_render_present_and_hash(
    const db_display_frame_step_t *frame_step, uint32_t frame_index,
    double elapsed_ms, db_display_cpu_present_damage_cb_t present_cb,
    void *present_user_data, uint64_t (*output_hash_fn)(void)) {
    if ((frame_step == NULL) || (output_hash_fn == NULL)) {
        return;
    }
    db_renderer_cpu_renderer_render_frame(frame_index);
    size_t damage_count = 0U;
    const db_damage_block_t *damage_blocks =
        db_renderer_cpu_renderer_damage_blocks(&damage_count);
    if (present_cb != NULL) {
        present_cb(damage_blocks, damage_count, present_user_data);
    }
    db_display_cpu_frame_step(frame_step, frame_index, elapsed_ms,
                              db_renderer_cpu_renderer_state_hash,
                              output_hash_fn);
}

#endif
