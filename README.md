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

## Target Names

Build outputs use versioned, display-aware target names only:

- `driverbench_glfw_window_opengl_gl1_5_gles1_1`
- `driverbench_glfw_window_opengl_gl3_3`
- `driverbench_linux_kms_atomic_opengl_gl1_5_gles1_1`
- `driverbench_glfw_window_vulkan_1_2_multi_gpu`
