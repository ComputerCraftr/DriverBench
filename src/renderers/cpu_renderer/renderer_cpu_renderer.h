#ifndef DRIVERBENCH_RENDERER_CPU_RENDERER_H
#define DRIVERBENCH_RENDERER_CPU_RENDERER_H

#include <stddef.h>
#include <stdint.h>

#include "../renderer_benchmark_types.h"

typedef enum {
    DB_CPU_RENDER_TARGET_PRESERVED_SURFACE = 0,
    DB_CPU_RENDER_TARGET_REPLACE_SURFACE = 1,
} db_cpu_render_target_mode_t;

void db_renderer_cpu_renderer_init_with_hdr_float_bo(int use_hdr_float_bo);
const db_damage_block_t *db_renderer_cpu_renderer_render_frame_to_surface_mode(
    uint32_t frame_index, const db_benchmark_pixel_surface_t *surface,
    db_cpu_render_target_mode_t target_mode, size_t *out_damage_count);
const db_damage_block_t *db_renderer_cpu_renderer_render_frame_to_surface(
    uint32_t frame_index, const db_benchmark_pixel_surface_t *surface,
    size_t *out_damage_count);
void db_renderer_cpu_renderer_shutdown(void);

uint32_t db_renderer_cpu_renderer_work_unit_count(void);
const char *db_renderer_cpu_renderer_capability_mode(void);
uint64_t db_renderer_cpu_renderer_state_hash(void);

#endif
