#version 450

layout(location = 0) in vec2 in_pos;

#if defined(VULKAN) || defined(GL_KHR_vulkan_glsl)
layout(push_constant) uniform PC {
    vec2 offset_ndc;
    vec2 scale_ndc;
    vec4 color;
} pc;
#else
layout(std140, binding = 0) uniform PC {
    vec2 offset_ndc;
    vec2 scale_ndc;
    vec4 color;
} pc;
#endif

layout(location = 0) flat out vec4 v_color;

void main() {
    gl_Position = vec4(pc.offset_ndc + (in_pos * pc.scale_ndc), 0.0, 1.0);
    v_color = pc.color;
}
