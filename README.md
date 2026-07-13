# DriverBench

DriverBench builds one executable: `driverbench`.

CPU and OpenGL paths are always built. Vulkan and display backends depend on
build toggles/dependencies.

## Rendering Model

Runtime execution follows one ownership pipeline:

```text
CLI + runtime capabilities
    -> benchmark engine
    -> immutable canonical frame plan
    -> renderer logical backing
    -> presentation transform/native output
```

Benchmark implementations live under `src/benchmarks` and privately own mode
parsing, simulation state, gradient/bands/snake generation, rebuild sources,
and state hashing. They publish one immutable logical-resolution
`db_frame_plan_t`. Renderers execute only its generic geometry, rebuild, repair,
seed, and upload requirements; they do not inspect benchmark modes or
simulation cursors. A frame source commits benchmark progression only after the
renderer has successfully executed the published plan. Displays resolve a
separate presentation transform from that source extent to the current window
framebuffer or KMS mode:

- CPU renders canonical geometry into one persistent pixel surface.
- GL1 keeps one authoritative CPU backing store and one persistent presentation
  texture. Two bounded streaming PBOs transfer changed regions; their contents
  are temporary and direct client upload is used only when PBO support fails.
  A fixed presentation-quad VBO is preferred, with client arrays retained only
  as the compatibility fallback.
- GL3 keeps one persistent logical-resolution working-format FBO. Geometry
  updates that target and a fullscreen texture-sampling pass presents it; there
  is no history ping-pong copy.
- Vulkan keeps one persistent logical-resolution backing image. Redundant
  history-to-history and resize-preservation copies are removed. Windowed
  presentation samples that backing through a fullscreen triangle directly
  into the swapchain; swapchain images are not transfer destinations.

Vulkan multi-GPU candidates are gated behind canonical state and
`working_hash_rgba8` equivalence with the single-GPU path. CPU-authored semantic
pieces are the only scheduling input; row bands are merely an initial assignment
policy. Device groups render into peer-readable per-device slots before sampled
composition. Linux independent devices negotiate compatible opaque-FD or
DMA-BUF modifier images and use temporary one-shot `SYNC_FD` payloads without
host-polling producer completion. Unsupported modifier, memory-type, ownership,
or semaphore profiles are reported precisely and remain primary-only.

Window/Retina/KMS extent changes update only the presentation transform;
working targets remain canonical. Invalidation and explicit rebuild requests
recover from a typed canonical geometry or raster seed. State and
working-surface hashes are target
density independent; presented framebuffer hashes include the output extent.

Working precision and native output are independent. `rgba16f` may feed an SDR
output conversion and does not imply HDR. Native HDR is selected only after the
presenter verifies its output format, colorspace, transfer function, metadata,
and sink path. KMS CPU/GL1/GL3 use XRGB2101010 plus BT.2020/PQ when the plane,
EDID, connector properties, EGL path (for GL), and atomic test commit all
support it. GL1 uses shared CPU BT.2020/PQ conversion, transient PBO uploads to
a packed `GL_RGB10_A2` texture, and fixed-function nearest-neighbor
presentation; it does not require GLSL. GL3 uses its presentation shader, and
KMS CPU converts directly into XRGB2101010 scanout slots. GLFW Vulkan uses an
HDR10 ST2084 10-bit WSI surface plus
`VK_EXT_hdr_metadata`. GLFW CPU/OpenGL and offscreen remain SDR because they
cannot verify a complete native HDR chain. Explicit `--output-format hdr`
fails instead of treating a float working texture or unverified 10-bit buffer
as HDR.

HDR10 presentation treats working RGB as linear sRGB, converts it to BT.2020,
and applies PQ with 203-nit reference white and 1000-nit mastering/MaxCLL
metadata (0.005-nit minimum, 203-nit MaxFALL). Scaling remains deterministic
nearest-neighbor and occurs once at the presentation boundary; HDR selection
does not change canonical working hashes.

Renderer content capability and presenter output capability are reported
separately. Floating-point textures and PBOs preserve working precision and
headroom, but they neither select an HDR colorspace nor signal metadata to the
display. The verified renderer/presenter HDR routes are CPU/KMS, GL1/KMS,
GL3/KMS, and Vulkan/GLFW WSI.

## Build

Initialize vendored dependencies first:

```bash
git submodule update --init --recursive
```

```bash
cmake --preset ninja-linux-native-clang-release
cmake --build --preset ninja-linux-native-clang-release
```

Optional build toggles (defaults shown):

- `-DDB_BUILD_VULKAN=ON`
- `-DDB_BUILD_GLFW_WINDOW_DISPLAY=ON`
- `-DDB_BUILD_LINUX_KMS_ATOMIC_DISPLAY=ON`
- `-DDB_GLFW_PROVIDER=vendored`
- `-DDB_GLFW_REQUIRED=OFF`
- `-DDB_ENABLE_LTO=OFF`
- `-DDB_ENABLE_LOOP_HINTS=ON`
- `-DDB_ENABLE_SANITIZERS=ON`
- `-DDB_ENABLE_DSYM=ON`
- `-DDB_TEST_HEADLESS_ONLY=OFF`
- `-DDB_TARGET_LINUX_32BIT=OFF`
- `-DDB_TARGET_LINUX_MUSL=OFF`
- `-DDB_TARGET_LINUX_MUSL_TRIPLE=x86_64-linux-musl`
- `-DDB_TARGET_LINUX_ROOT=`
- `-DDB_TARGET_LINUX_MUSL_STATIC_LINK=OFF`
- `-DDB_TEST_ALTERNATE_RELEASE_ROOTS=`

`DB_GLFW_PROVIDER` values:

- `vendored`: build static GLFW from `third_party/glfw` (default)
- `system`: use an installed system GLFW if available
- `off`: disable `glfw_window` and GLFW-backed OpenGL offscreen routes

Provider resolution policy:

- Default builds do not probe or prefer a system GLFW package on Linux or macOS.
- System GLFW discovery is only used when `-DDB_GLFW_PROVIDER=system` is set explicitly.
- `vendored` and `off` builds intentionally avoid the old implicit system-GLFW fallback behavior.
- `DB_GLFW_REQUIRED=ON` makes configure fail instead of silently disabling GLFW-backed paths when a requested provider or dependency stack is unavailable.

LTO policy:

- `DB_ENABLE_LTO=ON` enables compiler-supported CMake IPO/LTO in Release,
  RelWithDebInfo, and MinSizeRel builds
- CMake and the active compiler select the supported IPO implementation; there
  is no separate LTO-mode cache option
- Release presets enable `DB_ENABLE_LTO=ON` by default
- Ad hoc/default configures leave LTO off unless you enable it explicitly
- `DB_ENABLE_SANITIZERS=ON` applies AddressSanitizer and
  UndefinedBehaviorSanitizer to Debug builds on supported GNU-like compilers
- `DB_ENABLE_LOOP_HINTS=ON` enables project loop-optimization hints
- `DB_ENABLE_DSYM=ON` generates dSYM bundles for Apple Debug and RelWithDebInfo
  executables when `dsymutil` is available

Recommended build modes:

```bash
# Default: vendored static GLFW
git submodule update --init --recursive
cmake --preset ninja-linux-native-clang-release
cmake --build --preset ninja-linux-native-clang-release

# Explicit system GLFW override
cmake -S . -B build-system-glfw -DDB_GLFW_PROVIDER=system
cmake --build build-system-glfw -j

# Manual system-GLFW preset fallback
cmake --preset ninja-native-system-glfw-release
cmake --build --preset ninja-native-system-glfw-release

# Manual native GCC fallback
cmake --preset ninja-linux-native-gcc-release
cmake --build --preset ninja-linux-native-gcc-release

# Fully disable GLFW-dependent paths
cmake -S . -B build-no-glfw -DDB_GLFW_PROVIDER=off
cmake --build build-no-glfw -j
```

Vendored GLFW policy:

- macOS: vendored static GLFW is the default path
- Linux glibc: vendored static GLFW is the default path
- Linux musl: vendored GLFW supports both shared and static musl target presets; static archive requirements only apply when `DB_TARGET_LINUX_MUSL_STATIC_LINK=ON`
- Linux 32-bit: vendored static GLFW now uses the resolved 32-bit toolchain/root discovery path instead of forced `/usr/lib32` lookups
- Linux vendored builds use X11 only; Wayland is intentionally disabled
- system GLFW remains available only as an explicit override

Supported presets:

- Native Linux:
  - `ninja-linux-native-clang-release`
  - `ninja-linux-native-clang-debug`
  - `ninja-linux-native-gcc-release`
  - `ninja-linux-native-gcc-debug`
- Linux cross/static:
  - `ninja-linux-gnu-i686-clang-release`
  - `ninja-linux-musl-x86_64-release`
  - `ninja-linux-musl-x86_64-static-release`
  - `ninja-linux-musl-i686-release`
  - `ninja-linux-musl-i686-static-release`
- Native macOS:
  - `ninja-macos-native-clang-release`
  - `ninja-macos-native-clang-debug`

Preset policy:

- release-oriented presets enable LTO explicitly
- vendored GLFW is the default provider for the standard presets
- GLFW is required for presets that declare it, so missing submodules or dependency stacks fail configure instead of silently disabling GLFW
- the primary automated validation path uses vendored GLFW
- `ninja-native-system-glfw-release` is a manual/debugging fallback preset
- `ninja-linux-native-gcc-release` and `ninja-linux-native-gcc-debug` are manual compiler fallback presets

Cross-build preset notes:

- `ninja-linux-gnu-i686-clang-release` builds CPU + OpenGL + vendored GLFW, with Vulkan and KMS disabled in that preset; it prefers an explicit `DB_TARGET_LINUX_ROOT` when provided and otherwise falls back to normal host multilib discovery
- `ninja-linux-native-gcc-release` and `ninja-linux-native-gcc-debug` provide native Linux GCC fallback lanes while the primary native validation path remains Clang-based
- `ninja-linux-musl-x86_64-release` and `ninja-linux-musl-i686-release` cross-compile musl targets without forcing `-static`
- `ninja-linux-musl-x86_64-static-release` and `ninja-linux-musl-i686-static-release` add `DB_TARGET_LINUX_MUSL_STATIC_LINK=ON`
- Linux cross roots are resolved in this order:
  - explicit `DB_TARGET_LINUX_ROOT`
  - environment `DB_TARGET_LINUX_ROOT`
  - existing `CMAKE_SYSROOT`
  - conventional musl fallback `/usr/<triple>` when targeting musl

Cross-target optimistic feature requirements:

- Vendored GLFW on Linux requires these target-side libraries:
  - `X11`
  - `Xrandr`
  - `Xinerama`
  - `Xcursor`
  - `Xi`
  - `Xext`
  - `GL`
- Vendored GLFW on Linux also requires these target-side headers:
  - `X11/Xlib.h`
  - `X11/extensions/Xrandr.h`
  - `X11/extensions/Xinerama.h`
  - `X11/Xcursor/Xcursor.h`
  - `X11/extensions/XInput2.h`
  - `X11/extensions/Xrender.h`
  - `GL/glx.h`
- `ninja-linux-gnu-i686-clang-release` uses host multilib discovery when
  `DB_TARGET_LINUX_ROOT` is unset. That means the 32-bit variants of the X11
  and OpenGL libraries above must be installed and linkable on the host.
- `ninja-linux-musl-x86_64-release` expects the active musl root to contain the
  target libc headers plus target-side X11/OpenGL development files. With KMS
  and Vulkan enabled it also needs target-side `gbm`, `EGL`, `drm`,
  `vulkan/vulkan_core.h`, and `libvulkan.so`.
- `ninja-linux-musl-x86_64-static-release` additionally requires static
  archives for the same dependency stack. If the root only contains shared
  libraries such as `libGL.so`, configure will fail intentionally.
- `ninja-linux-musl-i686-release` and
  `ninja-linux-musl-i686-static-release` require the same i686 musl target-side
  X11/OpenGL headers and libraries inside the active musl root. Host
  `/usr/include` and `/usr/lib32` content is now rejected for these presets on
  purpose.

`cpu` API is always built.
`offscreen` display is always built (CPU always; OpenGL requires GLFW support).

## Run

```bash
./build/driverbench [dispatch flags] [runtime flags]
```

Dispatch flags:

- `--api auto|cpu|opengl|vulkan`
- `--renderer auto|gl1_5_gles1_1|gl3_3` (OpenGL only)
- `--display offscreen|glfw_window|linux_kms_atomic` (required, explicit only)
- `--kms-card /dev/dri/cardX` (KMS only)

Runtime flags:

- `--allow-remote-display <0|1>`
- `--backbuffer-draw-mode <dirty|full>`
- `--benchmark-mode <gradient_sweep|bands|snake_grid|gradient_fill|snake_rect|snake_shapes>`
- `--bench-speed <value>` (`> 0`, max `1024`)
- `--working-format <rgba8|rgba16f>` (default: `rgba16f`; controls the canonical working-surface precision)
- `--output-format <auto|sdr|hdr>` (default: `auto`; `hdr` requires a verified native HDR format, colorspace, and metadata path)
- `--debug-clear-default-framebuffer <0|1>`
- `--fps-cap <value>`
- `--hash <none|state|pixel|both>`
- `--hash-report <final|aggregate|both>`
- `--frame-limit <value>`
- `--glfw-hidden-window <0|1>`
- `--metrics-mode <basic|dual>`
- `--present-buffer-mode <auto|replace|single_source|ring>`
- `--random-seed <value>`
- `--resize-at-frame <FRAME:WIDTHxHEIGHT>` (GLFW diagnostic; dimensions are logical window units)
- `--vk-allow-cpu-workers <0|1>`
- `--vk-multi-device-policy <auto|group_only|independent_ok>`
- `--vk-no-present <0|1>`
- `--trace-damage <0|1|2|3>` (`1`: summaries, `2`: up to 128 details, `3`: exhaustive)
- `--trace-gl-errors <0|1>`
- `--trace-shadow-upload <0|1|2|3>` (`1`: summaries, `2`: up to 128 spans, `3`: exhaustive)
- `--trace-vulkan <0|1|2>` (`1`: plans/phases, `2`: transport and synchronization details)
- `--vsync <0|1|on|off|true|false>`
- `--help`

Runtime options are now configured via CLI flags.
Benchmark mode may be left unset to use its default auto-selection behavior.
`--bench-speed` controls per-frame benchmark progression (snake/gradient modes).

GLFW resize diagnostics change only the presentation destination. Canonical
renderer surfaces remain at the logical raster extent, so maximizing or using a
high-DPI framebuffer does not invalidate GL1, GL3, or Vulkan persistent backing
contents. The observed framebuffer extent and content scale are reported by
`presentation_resize`, followed by a replacement `presentation_contract`.

GLFW preserved presentation is trusted only through native buffer age. GLX
uses `GLX_EXT_buffer_age`; EGL uses `EGL_EXT_buffer_age`. Age is queried once
per frame. Unavailable or zero age, an age beyond retained history, and surface
recreation force one full presentation repair without invalidating the logical
GL1 backing. The old post-swap clear/readback reuse heuristic has been removed.
Age 1 means the acquired back buffer contains the immediately previous
presentation, so GL1 draws only current damage and performs no historical
replay. Age N draws current damage plus the previous N-1 retained rectangle
sets. Only normalized rectangles are retained; no historical framebuffer
pixels or textures are stored.

Gradient and snake-grid recovery remains compact and procedural. Overlapping
snake rectangle and shape modes instead keep one fixed logical-resolution
benchmark checkpoint in the selected working format. Successful frames update
that checkpoint transactionally; failed frames leave it unchanged. A rebuild
restores the immutable committed checkpoint and then applies the current
frame's bounded geometry, so recovery time and memory do not grow with the
completed shape count. GL1 separately retains its own fixed CPU backing because
that surface is an active texture-upload resource, not benchmark history.

Project logs are schema-2 events. Filter stable fields directly rather than
parsing prose, for example:

```sh
build/driverbench --display glfw_window --api opengl \
  --renderer gl1_5_gles1_1 --benchmark-mode snake_grid \
  --resize-at-frame 2:1279x719 --frame-limit 4 --trace-damage 1 \
  | grep -E 'event=(window_resize_request|presentation_resize|presentation_contract|frame_plan)'
```

Display/API support summary:

- `offscreen`: CPU always; OpenGL available when GLFW support is enabled.
- `glfw_window`: CPU + OpenGL; Vulkan when Vulkan support is built and GLFW support is enabled.
- `linux_kms_atomic`: CPU + OpenGL (Linux-only when KMS backend is built).

Notes:

- `DB_BUILD_GLFW_WINDOW_DISPLAY=OFF` disables both `glfw_window` and the GLFW-backed OpenGL offscreen routes.
- `DB_GLFW_PROVIDER=off` keeps the same runtime behavior as disabling the GLFW build gate, but makes the intent explicit at configure time.
- GL1 offscreen still uses the hidden-GLFW route; GL3 offscreen keeps its dedicated FBO render path but still requires GLFW for context creation under the current single-gate policy.

Examples:

```bash
./build/driverbench --api cpu --display offscreen --benchmark-mode snake_grid --random-seed 12345 --frame-limit 300
./build/driverbench --api cpu --display offscreen --benchmark-mode gradient_fill --hash both --hash-report aggregate --frame-limit 600
./build/driverbench --api opengl --renderer gl3_3 --display glfw_window --vsync 0 --frame-limit 1000
./build/driverbench --api vulkan --display glfw_window --benchmark-mode gradient_fill
```

## Logs and Traces

DriverBench-generated logs use one versioned, single-line contract:

```text
[component][level] event=<name> schema=2 key=value key="escaped value"
```

External Mesa, Vulkan loader, GLFW, EGL, and driver messages are not rewritten.
Useful trace runs:

```bash
# CPU canonical frame plans and geometry writes
build/driverbench --api cpu --display offscreen --benchmark-mode snake_grid \
  --frame-limit 3 --trace-damage 1

# GL1 backing, staging, upload, and per-block details
build/driverbench --api opengl --renderer gl1_5_gles1_1 --display offscreen \
  --benchmark-mode snake_grid --frame-limit 3 --trace-damage 2 \
  --trace-shadow-upload 2 --trace-gl-errors 1

# GL3 persistent FBO and sampled fullscreen presentation
build/driverbench --api opengl --renderer gl3_3 --display offscreen \
  --benchmark-mode snake_grid --frame-limit 3 --trace-damage 1

# Vulkan persistent backing and sampled fullscreen presentation
build/driverbench --api vulkan --display glfw_window --glfw-hidden-window 1 \
  --benchmark-mode snake_grid --frame-limit 3 --trace-damage 1 \
  --trace-vulkan 1
```

Trace events expose frame-plan geometry operation, rebuild reason, canonical and
pixel extents, target identity/generation, hashes, transfer sizes, result, and
presentation method. Run the trace and persistent-target contracts with:

```bash
ctest --test-dir build -R 'trace|persistent|rebuild|structured_log' \
  --output-on-failure
```

## Determinism Tests

`ctest` runs one canonical determinism matrix per benchmark family against the
primary unified binary. Each matrix requires the same `state_hash_final` and
canonical `framebuffer_hash_final` for equal-work schedules (`1x40`, `20x2`,
and `40x1`) across supported CPU, GL1, GL3, Vulkan, offscreen, hidden GLFW,
dirty, full, single-source, and ring strategies. Native and working-buffer
hashes are diagnostics only.

Canonical hashes live in
`cmake/DriverBenchCanonicalGoldens.cmake`. Regenerate them only after every
available matrix scenario agrees:

```bash
cmake --build build --target update_canonical_goldens
```

The update target fails without modifying the manifest when any renderer,
display strategy, or equal-work schedule diverges.

Canonical pixel and presentation hashes use the versioned
`fnv1a64_tree_v1` contract: domain-separated 1024-byte leaves and typed binary
tree nodes hashed with FNV-1a64. State hashes and per-run aggregate folding use
serial `fnv1a64_serial_v1`. These are deterministic integrity hashes, not
cryptographic hashes.

Fast ISA conformance tests verify identical tree digests and runtime dispatch
under QEMU without starting a display or renderer:

```bash
ctest --test-dir build -R 'hash_conformance|qemu_hash' --output-on-failure
```

SSE2, AVX2, and AArch64 NEON tests run when their QEMU executable and cross
runtime are available. The supplementary i686 SSE2 test skips cleanly when a
32-bit compiler runtime is not installed. QEMU validates instruction safety and
digest equivalence, not performance.

Alternate release coverage is opt-in and runs the same suite against already-built
binaries that are directly executable on the current host.

Configure alternate release suites with
`-DDB_TEST_ALTERNATE_RELEASE_ROOTS=lane=/path/to/build;other=/path/to/other/build`.
Each alternate root must already contain:

- `driverbench`
- `driverbench_unit_tests`

Alternate suites mirror the primary release test set:

- unit tests
- CLI regressions
- binary symbol leak checks
- canonical benchmark-family determinism matrices
- GLFW/OpenGL/Vulkan matrix scenarios and runtime regressions when supported by
  the alternate build

Examples:

```bash
cmake -S . -B build-alt-gcc \
  -DDB_TEST_ALTERNATE_RELEASE_ROOTS='gcc=build/gcc-release'
ctest --test-dir build-alt-gcc -L alternate -j

cmake -S . -B build-alt32 \
  -DDB_TEST_ALTERNATE_RELEASE_ROOTS='linux32=build/linux32'
ctest --test-dir build-alt32 -L alternate -j

cmake -S . -B build-alt-musl \
  -DDB_TEST_ALTERNATE_RELEASE_ROOTS='musl=build/musl-x86_64'
ctest --test-dir build-alt-musl -L alternate -j
```

Host-runnable alternate-suite notes:

- Native GCC and Linux 32-bit glibc release binaries can run through
  `ctest -L alternate` directly on this host configuration.
- Shared musl binaries are only registered as alternate suites when their ELF
  interpreter is present on the host at the path they were linked against,
  typically `/lib/ld-musl-x86_64.so.1`.
- Static musl binaries avoid the ELF interpreter requirement, but only after
  the musl root provides static X11/OpenGL/Vulkan archives for the enabled
  feature set.

Typical build + test commands for the currently supported host-runnable lanes:

```bash
# Native primary suite
cmake --preset ninja-linux-native-clang-release
cmake --build --preset ninja-linux-native-clang-release
ctest --test-dir build -j

# Native GCC alternate suite
cmake --preset ninja-linux-native-gcc-release
cmake --build --preset ninja-linux-native-gcc-release
cmake -S . -B build-alt-gcc \
  -DDB_TEST_ALTERNATE_RELEASE_ROOTS='gcc=build/gcc-release'
ctest --test-dir build-alt-gcc -L alternate -j

# Full-featured Linux 32-bit alternate suite
cmake --preset ninja-linux-gnu-i686-clang-release
cmake --build --preset ninja-linux-gnu-i686-clang-release
cmake -S . -B build-alt32 \
  -DDB_TEST_ALTERNATE_RELEASE_ROOTS='linux32=build/linux32'
ctest --test-dir build-alt32 -L alternate -j

# Shared musl build (registers as an alternate suite only when the musl loader
# is directly available on the host)
cmake --preset ninja-linux-musl-x86_64-release
cmake --build --preset ninja-linux-musl-x86_64-release
cmake -S . -B build-alt-musl \
  -DDB_TEST_ALTERNATE_RELEASE_ROOTS='musl=build/musl-x86_64'
ctest --test-dir build-alt-musl -L alternate -j
```

GLFW-dependent CTests use hidden windows. Tests that do not require GLFW WSI
use `--display offscreen`; the resize contract retains a hidden GLFW surface
because it specifically validates native window/framebuffer resize behavior.

For display-server-independent CI, keep the normal renderer build enabled but
register only tests that cannot create native windows or require a graphics
runtime:

```bash
cmake -S . -B build/ci-headless -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DDB_GLFW_PROVIDER=vendored \
  -DDB_GLFW_REQUIRED=ON \
  -DDB_TEST_HEADLESS_ONLY=ON \
  -DDB_ENABLE_LTO=OFF
cmake --build build/ci-headless --parallel
ctest --test-dir build/ci-headless --output-on-failure --parallel 4
```

This mode still compiles the host-supported renderer and presenter code. It
runs unit tests, source policies, hash conformance, CLI contracts, CPU
offscreen regressions, and CPU canonical determinism matrices. It omits GLFW,
OpenGL, Vulkan WSI, KMS runtime, resize, and cross-renderer tests that need a
native graphics environment. `.github/workflows/headless.yml` runs this lane
on the current GitHub-hosted Ubuntu and macOS runner images.

Header-only clang-tidy diagnostics are checked separately because
`run-clang-tidy` schedules compilation-database source files, not headers as
primary inputs:

```bash
python3 scripts/run_header_clang_tidy.py \
  --source-root . --build-dir build
ctest --test-dir build -R '^source_header_clang_tidy$' --output-on-failure
```
