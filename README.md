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

Runtime flags:

- `--allow-remote-display <0|1>`
- `--benchmark-mode <gradient_sweep|bands|snake_grid|gradient_fill|snake_rect|snake_shapes>`
- `--bench-speed <value>` (`> 0`, max `1024`)
- `--fps-cap <value>`
- `--hash <none|state|pixel|both>`
- `--hash-report <final|aggregate|both>`
- `--frame-limit <value>`
- `--offscreen <0|1>`
- `--random-seed <value>`
- `--vsync <0|1|on|off|true|false>`

Runtime options are now configured via CLI flags.
Benchmark mode may be left unset to use its default auto-selection behavior.
`--bench-speed` controls per-frame benchmark progression (snake/gradient modes).

Examples:

```bash
./build/driverbench --api cpu --display offscreen --benchmark-mode snake_grid --random-seed 12345 --frame-limit 300
./build/driverbench --api cpu --display offscreen --benchmark-mode gradient_fill --hash both --hash-report aggregate --frame-limit 600
./build/driverbench --api opengl --renderer gl3_3 --display glfw_window --vsync 0 --frame-limit 1000
./build/driverbench --api vulkan --display glfw_window --benchmark-mode gradient_fill
```

## Determinism Tests

`ctest` runs deterministic CPU/offscreen hash tests against the unified binary.

Enable optional GLFW offscreen determinism tests with:

```bash
cmake -S . -B build -DDB_ENABLE_GLFW_OFFSCREEN_TESTS=ON
```
