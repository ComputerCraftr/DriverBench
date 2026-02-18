# DriverBench

OpenGL and Vulkan driver benchmarks

## Configure Matrix

Choose backend combinations with CMake cache variables:

- `DB_OPENGL_RENDERER=gl1_5_gles1_1|gl3_3`
- `DB_OPENGL_DISPLAY=auto|glfw_window|linux_kms_atomic`
- `DB_VULKAN_DISPLAY=auto|glfw_window|vk_khr_display`

Notes:

- `vk_khr_display` is reserved and not implemented yet.
- `linux_kms_atomic` is Linux-only and currently supports `gl1_5_gles1_1`.
- Default `auto` values opportunistically build as many valid targets as your platform/dependencies allow.

## Build Defaults

- `cmake --preset ninja` now configures a `Release` build by default.
- Release-like builds use portable optimization defaults:
  - `-O3`
  - `-funroll-loops` (compiler-led loop optimization hints)
- Clang-family compilers (`Clang`/`AppleClang`) enable `-flto` by default in
  release-like builds.
- Non-Clang compilers auto-disable LTO with a configure-time warning.

Override knobs:

- `-DCMAKE_BUILD_TYPE=Debug` to disable release optimizations.
- `-DDB_ENABLE_AGGRESSIVE_OPT=OFF` to disable aggressive optimization flags.
- `-DDB_ENABLE_LOOP_HINTS=OFF` to disable loop hint flags.
- `-DDB_ENABLE_LTO=OFF` to disable LTO.

## Target Names

Build outputs use versioned, display-aware target names only:

- `driverbench_glfw_window_opengl_gl1_5_gles1_1`
- `driverbench_glfw_window_opengl_gl3_3`
- `driverbench_linux_kms_atomic_opengl_gl1_5_gles1_1`
- `driverbench_glfw_window_vulkan_1_2_multi_gpu`

## Runtime Options

- `DRIVERBENCH_VSYNC=1|0|on|off|true|false`
  - Overrides GLFW swap interval at runtime (default follows `BENCH_VSYNC_ENABLED`).
- `DRIVERBENCH_FPS_CAP=<number>|0|off|uncapped`
  - Caps render loop rate for GLFW backends when set to a positive FPS value.
  - `0`, `off`, `false`, `uncapped`, or unset keeps rendering uncapped.
- `DRIVERBENCH_BENCHMARK_MODE=bands|snake_grid`
  - `bands` is an animated color-changing vertical-band workload.
    Warning: this mode can produce intense rapid flashing, especially with
    `DRIVERBENCH_VSYNC=off` and high FPS.
  - `snake_grid` uses a deterministic S-pattern sweep over a tile grid sized
    to the benchmark window (`BENCH_WINDOW_WIDTH_PX x BENCH_WINDOW_HEIGHT_PX`)
    and updates a deterministic window of tiles per frame
    (`BENCH_SNAKE_PHASE_WINDOW_TILES`, default `64`) with progressive
    green-over-grey phasing. The sweep paints from top to bottom, then restarts
    at the top and clears back to grey.
