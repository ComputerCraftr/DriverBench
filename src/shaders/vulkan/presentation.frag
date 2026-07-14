#version 450

#if defined(VULKAN) || defined(GL_KHR_vulkan_glsl)
layout(set = 0, binding = 0) uniform sampler2D backing_texture;
#else
layout(binding = 0) uniform sampler2D backing_texture;
#endif

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 out_color;

#if defined(VULKAN) || defined(GL_KHR_vulkan_glsl)
layout(push_constant) uniform PC {
    float hdr_output_enabled;
} pc;
#else
layout(std140, binding = 1) uniform PC {
    float hdr_output_enabled;
} pc;
#endif

vec3 linear_srgb_to_bt2020(vec3 value) {
    return mat3(0.6274038959, 0.0690972894, 0.0163914389,
                0.3292830384, 0.9195403951, 0.0880133079,
                0.0433130657, 0.0113623156, 0.8955952532) * value;
}

vec3 pq_encode_nits(vec3 nits) {
    const float m1 = 2610.0 / 16384.0;
    const float m2 = 2523.0 / 32.0;
    const float c1 = 3424.0 / 4096.0;
    const float c2 = 2413.0 / 128.0;
    const float c3 = 2392.0 / 128.0;
    vec3 powered = pow(clamp(nits, 0.0, 10000.0) / 10000.0, vec3(m1));
    return pow((vec3(c1) + (c2 * powered)) /
                   (vec3(1.0) + (c3 * powered)),
               vec3(m2));
}

void main() {
    vec3 linear_rgb = max(texture(backing_texture, uv).rgb, vec3(0.0));
    if (pc.hdr_output_enabled > 0.5) {
        vec3 bt2020 = linear_srgb_to_bt2020(linear_rgb);
        out_color = vec4(
            pq_encode_nits(min(bt2020 * 203.0, vec3(1000.0))), 1.0);
    } else {
        out_color = vec4(clamp(linear_rgb, 0.0, 1.0), 1.0);
    }
}
