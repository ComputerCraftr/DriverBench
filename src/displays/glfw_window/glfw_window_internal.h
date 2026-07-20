#ifndef DRIVERBENCH_GLFW_WINDOW_INTERNAL_H
#define DRIVERBENCH_GLFW_WINDOW_INTERNAL_H

#include "core/db_format_contract.h"
#include "displays/display_types.h"
#include "driverbench_config.h"
#include "renderers/gl_hash_readback.h"

#include <stdint.h>

typedef struct GLFWwindow GLFWwindow;

extern const db_native_output_capability_t g_glfw_native_output_capability;

uint64_t db_glfw_hash_canonical_default_framebuffer_or_fail(
    const char *backend_name, GLFWwindow *window, uint32_t canonical_width,
    uint32_t canonical_height, db_gl_framebuffer_hash_scratch_t *scratch);
int db_run_glfw_window_opengl(db_gl_renderer_t renderer,
                              const db_cli_config_t *cfg);

#endif
