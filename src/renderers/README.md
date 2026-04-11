# Renderers

This folder is reserved for renderer-only modules that are display-backend agnostic.

Current renderer modules:

- `opengl_gl1_5_gles1_1/`
    - OpenGL 1.5 / GLES 1.1 fixed-function renderer logic.
- `opengl_gl3_3/`
    - OpenGL 3.3 shader renderer logic (GLSL embedded in binary).
- `vulkan_1_2_multi_gpu/`
    - Vulkan 1.2 multi-GPU renderer logic (SPIR-V embedded in binary).
- `cpu_renderer/`
    - CPU BO renderer logic.

Renderers are selected at runtime by the unified `driverbench` dispatch layer.
Display/backend entrypoints live in `src/displays/`.

All renderers consume canonical frame plans produced by the benchmark core.
The plan carries logical damage, current geometry, one typed authoritative
rebuild seed, repair coverage, rebuild reason, and the canonical state hash.
The seed is procedural geometry for reconstructable modes or an immutable
working-format raster checkpoint for overlapping snake modes. Renderers restore
the seed before applying current geometry.

Persistent target ownership:

- GL1: one authoritative CPU backing store, one persistent presentation
  texture, two transient streaming PBOs, and rectangle-only presentation
  damage history. Native buffer age selects full or scissored presentation;
  the presentation quad uses a VBO when available and client arrays only as a
  compatibility fallback.
- GL3: one logical-resolution working-format FBO, presented with a fullscreen
  nearest-neighbor sampling shader.
- Vulkan: one logical-resolution backing image; windowed presentation samples
  it into the swapchain with a fullscreen presentation pipeline.
- CPU: one caller-owned persistent pixel surface.

Native output resize changes only the presentation transform. Working-target
invalidation requests a canonical geometry rebuild; GL3 and Vulkan do not
preserve invalidated targets by copying old target contents.

Benchmark modes are shared across renderers:

- `gradient_sweep` (default): 32-row green/gray/green window sweeping downward.
- `bands`: animated full-height vertical color bands (high flash intensity).
- `snake_grid`: deterministic S-pattern tile sweep with phased recolor.
- `snake_rect`: deterministic PRNG random rectangle regions swept in S-pattern.
- `snake_shapes`: deterministic PRNG random shape regions (rectangles, circles, diamonds, triangles, trapezoids) swept in S-pattern.
- `gradient_fill`: top-down gray->green conversion sweep, then restart.

## Diagnostics & Tracing

DriverBench includes a low-level tracing framework for debugging renderer behavior and driver inconsistencies:

- **Damage tracing (`--trace-damage 1|2|3`)** emits `frame_plan`,
  `target_lifecycle`, and `damage_summary` records. Level 2 adds bounded
  `damage_block`
  records; level 3 emits every ordered detail.
- **Shadow upload tracing (`--trace-shadow-upload 1|2|3`)** emits one
  `shadow_upload` summary. Level 2 emits at most 128 spans and level 3 emits
  all `shadow_upload_span` records.
- **Vulkan tracing (`--trace-vulkan 1|2`)** exposes topology and phase
  summaries at level 1 and individual transport/synchronization attempts at
  level 2.
- **GL error tracing (`--trace-gl-errors 1`)** emits `gl_error_summary` and
  `gl_error` records with phase, target, context, and error code.

Every project trace uses `[component][level] event=<name> schema=2` followed by
typed key/value fields so CTest and external tools can parse the same contract.
