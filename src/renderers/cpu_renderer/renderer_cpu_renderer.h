#ifndef DRIVERBENCH_RENDERER_CPU_RENDERER_H
#define DRIVERBENCH_RENDERER_CPU_RENDERER_H

#include <stdint.h>

void db_renderer_cpu_renderer_init(void);
void db_renderer_cpu_renderer_render_frame(double time_s);
void db_renderer_cpu_renderer_shutdown(void);

uint32_t db_renderer_cpu_renderer_work_unit_count(void);
const char *db_renderer_cpu_renderer_capability_mode(void);
const uint32_t *db_renderer_cpu_renderer_pixels_rgba8(uint32_t *out_width,
                                                      uint32_t *out_height);

#endif
