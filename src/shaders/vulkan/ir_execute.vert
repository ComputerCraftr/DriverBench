#version 450

layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec4 in_rect;
layout(location = 2) in vec4 in_start_color;
layout(location = 3) in vec4 in_end_color;
layout(location = 4) in vec4 in_gradient;

layout(location = 0) flat out vec4 v_start_color;
layout(location = 1) flat out vec4 v_end_color;
layout(location = 2) flat out vec4 v_gradient;

void main() {
    gl_Position = vec4(in_rect.xy + (in_pos * in_rect.zw), 0.0, 1.0);
    v_start_color = in_start_color;
    v_end_color = in_end_color;
    v_gradient = in_gradient;
}
