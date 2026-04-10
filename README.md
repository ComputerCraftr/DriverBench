# DriverBench

DriverBench builds one executable: `driverbench`.

CPU and OpenGL paths are always built. Vulkan and display backends depend on
build toggles/dependencies.

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
- `-DDB_LTO_MODE=full`
- `-DDB_TARGET_LINUX_ROOT=`
- `-DDB_TARGET_LINUX_MUSL_STATIC_LINK=OFF`

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

- `DB_ENABLE_LTO=ON` enables LTO in release-like builds
- `DB_LTO_MODE=full|thin` selects the requested LTO mode
- Clang supports both `full` and `thin`
- GCC supports `full` only; requesting `thin` is a configure error
- `RelWithDebInfo` still disables LTO by default unless `DB_ENABLE_LTO_RELWITHDEBINFO=ON`
- Release presets enable `DB_ENABLE_LTO=ON` by default
- Ad hoc/default configures leave LTO off unless you enable it explicitly

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
- `--cpu-hdr <0|1>` (default: `1`, CPU renderer RGBA16F BO + half-float texture upload on GLFW)
- `--debug-clear-default-framebuffer <0|1>`
- `--fps-cap <value>`
- `--hash <none|state|pixel|both>`
- `--hash-report <final|aggregate|both>`
- `--frame-limit <value>`
- `--glfw-hidden-window <0|1>`
- `--metrics-mode <basic|dual>`
- `--present-buffer-mode <auto|replace|single_source|ring>`
- `--random-seed <value>`
- `--vk-allow-cpu-workers <0|1>`
- `--vk-multi-device-policy <auto|group_only|independent_ok>`
- `--vk-no-present <0|1>`
- `--vsync <0|1|on|off|true|false>`

Runtime options are now configured via CLI flags.
Benchmark mode may be left unset to use its default auto-selection behavior.
`--bench-speed` controls per-frame benchmark progression (snake/gradient modes).

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

## Determinism Tests

`ctest` runs the canonical release suite against the primary unified binary.
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
- CPU determinism
- GLFW/OpenGL determinism and runtime regressions when that alternate build has
  GLFW enabled

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

Enable optional GLFW offscreen determinism tests with:

```bash
cmake -S . -B build -DDB_ENABLE_GLFW_OFFSCREEN_TESTS=ON
```
