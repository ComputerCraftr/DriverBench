#version 330 core

out vec2 texture_coord;

void main() {
    vec2 position = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    texture_coord = position;
    gl_Position = vec4((position * 2.0) - 1.0, 0.0, 1.0);
}
