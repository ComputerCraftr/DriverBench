# Displays

Display modules are runtime backends used by the single `driverbench` binary.

Each module now exports a `db_run_*()` entrypoint (no standalone `main`).
Top-level dispatch is handled by `src/driverbench_main.c`.

- `glfw_window/`: GLFW event-loop display backends for CPU/OpenGL/Vulkan.
- `linux_kms_atomic/`: Linux DRM/KMS display backends for CPU/OpenGL.
- `offscreen/`: deterministic offscreen backend:
  - CPU always
  - OpenGL when GLFW support is compiled
  - offscreen OpenGL routing is selected centrally via
    `db_dispatch_offscreen_gl_route(...)`
  - current policy:
    - GL1 -> hidden GLFW window
    - GL3 -> dedicated offscreen FBO

GLFW provider/build policy:

- `DB_BUILD_GLFW_WINDOW_DISPLAY=ON` is the single feature gate for all
  GLFW-backed paths.
- `DB_GLFW_PROVIDER=vendored` builds static GLFW from `third_party/glfw`
  (git submodule, default).
- `DB_GLFW_PROVIDER=system` uses an installed GLFW only when explicitly
  requested.
- `DB_GLFW_PROVIDER=off` disables both `glfw_window` and GLFW-backed offscreen
  OpenGL routes.
- `DB_GLFW_REQUIRED=ON` turns provider/dependency discovery failures into
  configure errors for strict builds instead of silently disabling GLFW.
- vendored/off builds do not fall back to implicit system GLFW discovery.
- Linux vendored GLFW builds are X11-only by policy.
- Linux musl and Linux 32-bit presets attempt vendored GLFW by default.
- Linux cross presets accept only target-root headers and libraries for
  vendored GLFW dependency discovery; host `/usr/include` and unrelated host
  library paths are rejected for musl-rooted builds.
- Linux cross root discovery is centralized in the Linux toolchain module:
  - explicit `DB_TARGET_LINUX_ROOT` wins
  - musl conventional fallback `/usr/<triple>` is used only when no explicit
    root/sysroot input is provided
- static archive requirements for vendored GLFW are driven by
  `DB_TARGET_LINUX_MUSL_STATIC_LINK`, not by musl targeting alone.
- one native system-GLFW preset exists as a manual fallback path, but the
  primary automated validation path uses vendored GLFW.
- alternate release test suites are opt-in through
  `DB_TEST_ALTERNATE_RELEASE_ROOTS` and run only already-built, directly
  runnable binaries from other build roots.
- shared musl alternate suites require the musl ELF interpreter to exist on the
  host at the binary's linked interpreter path, typically
  `/lib/ld-musl-x86_64.so.1`.

Shared display constants/options:

- `../config/benchmark_config.h`
- `display_dispatch.h`
- `src/core/db_core.h` (runtime option keys)
- `display_gl_runtime_common.[ch]` (GL runtime prepare/validate helpers)

Backend capability validation is centralized in `display_dispatch.h` via:

- `db_dispatch_display_capabilities(...)`
- `db_dispatch_display_preferred_auto_api(...)`
- `db_dispatch_display_supports_api(...)`
- `db_dispatch_display_supports_gl_renderer(...)`
- `db_dispatch_display_supports_backend(...)`
