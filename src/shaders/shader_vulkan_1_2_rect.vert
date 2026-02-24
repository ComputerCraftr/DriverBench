#version 450
layout(location = 0) in vec2 in_pos; // [0..1] quad coords

#if defined(VULKAN) || defined(GL_KHR_vulkan_glsl)
layout(push_constant) uniform PC {
    vec2 offset_ndc; // in NDC
    vec2 scale_ndc;  // in NDC
    vec4 color;
    vec4 base_color;
    vec4 target_color;
    uint gradient_head_row;
    uint gradient_window_rows;
    uint grid_cols;
    uint grid_rows;
    int mode_phase_flag;
    uint palette_cycle;
    uint pattern_seed;
    uint render_mode;
    uint snake_batch_size;
    uint snake_cursor;
    int snake_phase_completed;
    uint snake_shape_index;
    uint viewport_height;
    uint viewport_width;
} pc;
#else
// OpenGL-mode GLSL tools reject push constants; keep a compatible fallback
// block so editor diagnostics remain useful.
layout(std140, binding = 0) uniform PC {
    vec2 offset_ndc;
    vec2 scale_ndc;
    vec4 color;
    vec4 base_color;
    vec4 target_color;
    uint gradient_head_row;
    uint gradient_window_rows;
    uint grid_cols;
    uint grid_rows;
    int mode_phase_flag;
    uint palette_cycle;
    uint pattern_seed;
    uint render_mode;
    uint snake_batch_size;
    uint snake_cursor;
    int snake_phase_completed;
    uint snake_shape_index;
    uint viewport_height;
    uint viewport_width;
} pc;
#endif

layout(location = 0) out vec4 v_color;

void main() {
    // Convert in_pos (0..1) to NDC band rect: (offset + in_pos*scale)
    vec2 ndc = pc.offset_ndc + in_pos * pc.scale_ndc;
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_color = pc.color;
}
