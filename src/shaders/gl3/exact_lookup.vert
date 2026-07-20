#version 330 core

layout(location = 0) in vec2 a_unit_position;
layout(location = 1) in vec4 a_rect;
layout(location = 2) in vec3 a_start_color;
layout(location = 4) in vec4 a_gradient;
layout(location = 5) in uvec2 a_lookup;

flat out vec3 v_start_color;
flat out float v_mode;
flat out uint v_lookup_base;
flat out int v_lookup_origin;
flat out int v_target_height;

void main() {
    v_start_color = a_start_color;
    v_mode = a_gradient.x;
    v_lookup_base = a_lookup.x;
    v_lookup_origin = int(a_lookup.y);
    v_target_height = int(a_gradient.w);
    vec2 position = a_rect.xy + (a_unit_position * a_rect.zw);
    gl_Position = vec4(position, 0.0, 1.0);
}
