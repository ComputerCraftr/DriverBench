#ifndef DRIVERBENCH_RUNTIME_OPTIONS_H
#define DRIVERBENCH_RUNTIME_OPTIONS_H

#define DB_RUNTIME_OPT_ALLOW_REMOTE_DISPLAY "allow_remote_display"
#define DB_RUNTIME_OPT_BACKBUFFER_DRAW_MODE "backbuffer_draw_mode"
#define DB_RUNTIME_OPT_BENCH_SPEED "bench_speed"
#define DB_RUNTIME_OPT_BENCHMARK_MODE "benchmark_mode"
#define DB_RUNTIME_OPT_OUTPUT_FORMAT "output_format"
#define DB_RUNTIME_OPT_WORKING_FORMAT "working_format"
#define DB_RUNTIME_OPT_FPS_CAP "fps_cap"
#define DB_RUNTIME_OPT_FRAME_LIMIT "frame_limit"
#define DB_RUNTIME_OPT_GL1_GRADIENT "gl1_gradient"
#define DB_RUNTIME_OPT_GL1_REPLAY_CAPACITY "gl1_replay_capacity"
#define DB_RUNTIME_OPT_GL1_TARGET "gl1_target"
#define DB_RUNTIME_OPT_GL3_GRADIENT "gl3_gradient"
#define DB_RUNTIME_OPT_HASH "hash"
#define DB_RUNTIME_OPT_HASH_REPORT "hash_report"
#define DB_RUNTIME_OPT_METRICS_MODE "metrics_mode"
#define DB_RUNTIME_OPT_PRESENT_BUFFER_MODE "present_buffer_mode"
#define DB_RUNTIME_OPT_RANDOM_SEED "random_seed"
#define DB_RUNTIME_OPT_RESIZE_AT_FRAME "resize_at_frame"
#define DB_RUNTIME_OPT_VK_ALLOW_CPU_WORKERS "vk_allow_cpu_workers"
#define DB_RUNTIME_OPT_VK_MULTI_DEVICE_POLICY "vk_multi_device_policy"
#define DB_RUNTIME_OPT_VK_NO_PRESENT "vk_no_present"
#define DB_RUNTIME_OPT_TRACE_DAMAGE "trace_damage"
#define DB_RUNTIME_OPT_TRACE_GL_ERRORS "trace_gl_errors"
#define DB_RUNTIME_OPT_TRACE_SHADOW_UPLOAD "trace_shadow_upload"
#define DB_RUNTIME_OPT_TRACE_VULKAN "trace_vulkan"
#define DB_RUNTIME_OPT_DUMP_GRADIENT_DIVERGENCE "dump_gradient_divergence"
#define DB_RUNTIME_OPT_IGNORE_CONFORMANCE_CACHE "ignore_conformance_cache"
#define DB_RUNTIME_OPT_RERUN_CONFORMANCE_PROBE "rerun_conformance_probe"
#define DB_RUNTIME_OPT_VK_GRADIENT "vk_gradient"
#define DB_RUNTIME_OPT_VSYNC "vsync"

#include <stdint.h>

typedef struct {
    uint32_t frame;
    uint32_t width;
    uint32_t height;
} db_resize_schedule_t;

const char *db_runtime_option_get(const char *name);
void db_runtime_option_set(const char *name, const char *value);
void db_runtime_options_reset_all(void);
void db_runtime_option_set_backbuffer_draw_full(int enabled);
void db_runtime_option_set_present_buffer_mode(const char *mode);
int db_resize_schedule_parse(const char *text, db_resize_schedule_t *out);

#endif
