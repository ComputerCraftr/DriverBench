#ifndef DRIVERBENCH_DISPLAY_TYPES_H
#define DRIVERBENCH_DISPLAY_TYPES_H

typedef enum {
    DB_API_CPU = 0,
    DB_API_OPENGL = 1,
    DB_API_VULKAN = 2,
} db_api_t;

typedef enum {
    DB_DISPLAY_GLFW_WINDOW = 0,
    DB_DISPLAY_LINUX_KMS_ATOMIC = 1,
    DB_DISPLAY_OFFSCREEN = 2,
} db_display_t;

typedef enum {
    DB_GL_RENDERER_GL1_5_GLES1_1 = 0,
    DB_GL_RENDERER_GL3_3 = 1,
} db_gl_renderer_t;

#endif
