#ifndef DRIVERBENCH_RENDERER_CPU_RENDERER_H
#define DRIVERBENCH_RENDERER_CPU_RENDERER_H

#include <stddef.h>
#include <stdint.h>

#include "../renderer_benchmark_common.h"

void db_renderer_cpu_renderer_init(void);
void db_renderer_cpu_renderer_render_frame(uint32_t frame_index);
void db_renderer_cpu_renderer_shutdown(void);

uint32_t db_renderer_cpu_renderer_work_unit_count(void);
const char *db_renderer_cpu_renderer_capability_mode(void);
const uint32_t *db_renderer_cpu_renderer_pixels_rgba8(uint32_t *out_width,
                                                      uint32_t *out_height);
const db_dirty_row_range_t *
db_renderer_cpu_renderer_damage_rows(size_t *out_count);
uint64_t db_renderer_cpu_renderer_state_hash(void);

#endif
