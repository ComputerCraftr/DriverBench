# DriverBench

DriverBench builds one executable: `driverbench`.

It opportunistically includes CPU/OpenGL/Vulkan paths based on detected
dependencies at configure time.

## Build

```bash
cmake --preset ninja
cmake --build build -j
```

Optional build toggles (defaults shown):

- `-DDB_BUILD_OPENGL=ON`
- `-DDB_BUILD_VULKAN=ON`
- `-DDB_BUILD_GLFW_WINDOW_DISPLAY=ON`
- `-DDB_BUILD_LINUX_KMS_ATOMIC_DISPLAY=ON`

`cpu` API and `offscreen` display are always built.

## Run

```bash
./build/driverbench [dispatch flags] [runtime flags]
```

Dispatch flags:

- `--api cpu|opengl|vulkan`
- `--renderer auto|gl1_5_gles1_1|gl3_3` (OpenGL only)
- `--display offscreen|glfw_window|linux_kms_atomic` (required, explicit only)
- `--kms-card /dev/dri/cardX` (KMS only)

Runtime flags (CLI aliases for env vars):

- `--allow-remote-display <0|1>` -> `DRIVERBENCH_ALLOW_REMOTE_DISPLAY`
- `--benchmark-mode <gradient_sweep|bands|snake_grid|gradient_fill|rect_snake>` -> `DRIVERBENCH_BENCHMARK_MODE`
- `--fps-cap <value>` -> `DRIVERBENCH_FPS_CAP`
- `--framebuffer-hash <0|1>` -> `DRIVERBENCH_FRAMEBUFFER_HASH`
- `--frame-limit <value>` -> `DRIVERBENCH_FRAME_LIMIT`
- `--hash-every-frame <0|1>` -> `DRIVERBENCH_HASH_EVERY_FRAME`
- `--offscreen <0|1>` -> `DRIVERBENCH_OFFSCREEN`
- `--offscreen-frames <value>` -> `DRIVERBENCH_OFFSCREEN_FRAMES`
- `--random-seed <value>` -> `DRIVERBENCH_RANDOM_SEED`
- `--vsync <0|1|on|off|true|false>` -> `DRIVERBENCH_VSYNC`

Runtime options are now configured via CLI flags.
Benchmark mode may be left unset to use its default auto-selection behavior.

Examples:

```bash
./build/driverbench --api cpu --display offscreen --benchmark-mode snake_grid --random-seed 12345 --offscreen-frames 300
./build/driverbench --api opengl --renderer gl3_3 --display glfw_window --vsync 0 --frame-limit 1000
./build/driverbench --api vulkan --display glfw_window --benchmark-mode gradient_fill
```

## Determinism Tests

`ctest` runs deterministic CPU/offscreen hash tests against the unified binary.

Enable optional GLFW offscreen determinism tests with:

```bash
cmake -S . -B build -DDB_ENABLE_GLFW_OFFSCREEN_TESTS=ON
```
