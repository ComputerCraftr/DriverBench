#version 450
layout(location = 0) in vec2 inPos; // [0..1] quad coords

#if defined(VULKAN) || defined(GL_KHR_vulkan_glsl)
layout(push_constant) uniform PC {
    vec2 offsetNDC;  // in NDC
    vec2 scaleNDC;   // in NDC
    vec4 color;
} pc;
#else
// OpenGL-mode GLSL tools reject push constants; keep a compatible fallback
// block so editor diagnostics remain useful.
layout(std140, binding = 0) uniform PC {
    vec2 offsetNDC;
    vec2 scaleNDC;
    vec4 color;
} pc;
#endif

layout(location = 0) out vec4 vColor;

void main() {
    // Convert inPos (0..1) to NDC band rect: (offset + inPos*scale)
    vec2 ndc = pc.offsetNDC + inPos * pc.scaleNDC;
    gl_Position = vec4(ndc, 0.0, 1.0);
    vColor = pc.color;
}
