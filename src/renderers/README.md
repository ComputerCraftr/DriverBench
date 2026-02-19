# Renderers

This folder is reserved for renderer-only modules that are display-backend agnostic.

Current renderer modules:

- `opengl_gl1_5_gles1_1/`
    - OpenGL 1.5 / GLES 1.1 fixed-function renderer logic.
- `opengl_gl3_3/`
    - OpenGL 3.3 shader renderer logic (GLSL loaded from files).
- `vulkan_1_2_multi_gpu/`
    - Vulkan 1.2 multi-GPU renderer logic.
- `cpu_renderer/`
    - CPU BO renderer logic shared by offscreen display/test entrypoints.

Display entrypoints live in `src/displays/`.

Benchmark modes are shared across renderers:

- `gradient_sweep` (default): 32-row green/gray/green window sweeping downward.
- `bands`: animated full-height vertical color bands (high flash intensity).
- `snake_grid`: deterministic S-pattern tile sweep with phased recolor.
- `gradient_fill`: top-down gray->green conversion sweep, then restart.
