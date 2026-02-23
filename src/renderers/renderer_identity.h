#ifndef DRIVERBENCH_RENDERER_IDENTITY_H
#define DRIVERBENCH_RENDERER_IDENTITY_H

static inline const char *db_renderer_name_cpu(void) {
    return "renderer_cpu_renderer";
}

static inline const char *db_renderer_name_opengl_gl1_5_gles1_1(void) {
    return "renderer_opengl_gl1_5_gles1_1";
}

static inline const char *db_renderer_name_opengl_gl3_3(void) {
    return "renderer_opengl_gl3_3";
}

static inline const char *db_renderer_name_vulkan_1_2_multi_gpu(void) {
    return "renderer_vulkan_1_2_multi_gpu";
}

#endif
