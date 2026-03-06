#version 330 core
out vec3 v_color;

void main() {
    vec2 positions[6] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 1.0, -1.0),
        vec2( 1.0,  1.0),
        vec2(-1.0, -1.0),
        vec2( 1.0,  1.0),
        vec2(-1.0,  1.0)
    );
    v_color = vec3(1.0, 1.0, 1.0);
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
}
