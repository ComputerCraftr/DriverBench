#ifndef DRIVERBENCH_BENCH_CONFIG_H
#define DRIVERBENCH_BENCH_CONFIG_H

// Shared benchmark shape/config used by all renderer/display combinations.
#define BENCH_BANDS 16U

#define BENCH_WINDOW_WIDTH_PX 1000
#define BENCH_WINDOW_HEIGHT_PX 600

#define BENCH_TARGET_FPS_D 60.0
#define BENCH_MS_PER_SEC_D 1000.0
#define BENCH_LOG_INTERVAL_MS_D 5000.0
#define BENCH_VSYNC_ENABLED 1 // vsync on to prevent tearing and rapid flashing
#define BENCH_GLFW_SWAP_INTERVAL (BENCH_VSYNC_ENABLED ? 1 : 0)

#define BENCH_PULSE_BASE_F 0.5F
#define BENCH_PULSE_AMP_F 0.5F
#define BENCH_PULSE_FREQ_F 2.0F
#define BENCH_PULSE_PHASE_F 0.3F
#define BENCH_COLOR_R_BASE_F 0.2F
#define BENCH_COLOR_R_SCALE_F 0.8F
#define BENCH_COLOR_G_SCALE_F 0.6F

#endif
