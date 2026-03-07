#Displays

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
