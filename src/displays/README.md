# Displays

Display/backend specific entrypoints live here.
Shared benchmark constants are centralized in `bench_config.h`.

Display modules are now thin integration layers (window + event loop wiring),
while draw/compute logic lives under `src/renderers/`.

- `linux_kms_atomic/display_linux_kms_atomic_opengl_gl1_5_gles1_1.c`
    - OpenGL 1.5 / GLES 1.1 benchmark on Linux DRM/KMS + GBM/EGL.
- `linux_kms_atomic/display_linux_kms_atomic_opengl_gl3_3.c`
    - OpenGL 3.3 benchmark on Linux DRM/KMS + GBM/EGL.
- `glfw_window/display_glfw_window_opengl_gl1_5_gles1_1.c`
    - GLFW display entrypoint for the OpenGL GL1.5/GLES1.1 renderer.
- `glfw_window/display_glfw_window_opengl_gl3_3.c`
    - GLFW display entrypoint for the OpenGL GL3.3 renderer.
- `glfw_window/display_glfw_window_vulkan_1_2_multi_gpu.c`
    - GLFW display entrypoint for the Vulkan 1.2 multi-GPU renderer.
