#version 330 core

layout(location = 0) in vec2 a_unit_position;
layout(location = 1) in vec4 a_rect;
layout(location = 2) in vec3 a_start_color;
layout(location = 3) in vec3 a_end_color;
layout(location = 4) in vec4 a_gradient;

flat out vec3 v_start_color;
flat out vec3 v_end_color;
flat out vec4 v_gradient;

void main() {
    v_start_color = a_start_color;
    v_end_color = a_end_color;
    v_gradient = a_gradient;
    vec2 position = a_rect.xy + (a_unit_position * a_rect.zw);
    gl_Position = vec4(position, 0.0, 1.0);
}
