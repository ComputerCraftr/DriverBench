#version 330 core
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec3 in_color;
out vec3 v_color;
flat out int v_tile_index;

void main() {
    v_color = in_color;
    v_tile_index = gl_VertexID / 6;
    gl_Position = vec4(in_pos, 0.0, 1.0);
}
