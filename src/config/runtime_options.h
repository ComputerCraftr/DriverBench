#ifndef DRIVERBENCH_RUNTIME_OPTIONS_H
#define DRIVERBENCH_RUNTIME_OPTIONS_H

#define DB_RUNTIME_OPT_ALLOW_REMOTE_DISPLAY "allow_remote_display"
#define DB_RUNTIME_OPT_BACKBUFFER_DRAW_MODE "backbuffer_draw_mode"
#define DB_RUNTIME_OPT_BENCH_SPEED "bench_speed"
#define DB_RUNTIME_OPT_BENCHMARK_MODE "benchmark_mode"
#define DB_RUNTIME_OPT_CPU_HDR "cpu_hdr"
#define DB_RUNTIME_OPT_FPS_CAP "fps_cap"
#define DB_RUNTIME_OPT_FRAME_LIMIT "frame_limit"
#define DB_RUNTIME_OPT_HASH "hash"
#define DB_RUNTIME_OPT_HASH_REPORT "hash_report"
#define DB_RUNTIME_OPT_METRICS_MODE "metrics_mode"
#define DB_RUNTIME_OPT_RANDOM_SEED "random_seed"
#define DB_RUNTIME_OPT_VK_ALLOW_CPU_WORKERS "vk_allow_cpu_workers"
#define DB_RUNTIME_OPT_VK_MULTI_DEVICE_POLICY "vk_multi_device_policy"
#define DB_RUNTIME_OPT_VK_NO_PRESENT "vk_no_present"
#define DB_RUNTIME_OPT_VSYNC "vsync"

const char *db_runtime_option_get(const char *name);
void db_runtime_option_set(const char *name, const char *value);
void db_runtime_option_set_backbuffer_draw_full(int enabled);

#endif
