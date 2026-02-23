#Displays

Display modules are runtime backends used by the single `driverbench` binary.

Each module now exports a `db_run_*()` entrypoint (no standalone `main`).
Top-level dispatch is handled by `src/driverbench_main.c`.

- `glfw_window/`: GLFW event-loop display backends for CPU/OpenGL/Vulkan.
- `linux_kms_atomic/`: Linux DRM/KMS display backends for OpenGL.
- `offscreen/`: CPU offscreen deterministic backend.

Shared display constants/options:

- `../config/benchmark_config.h`
- `display_dispatch.h`
- `src/core/db_core.h` (runtime option keys)
