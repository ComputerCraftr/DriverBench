#version 450

layout(location = 0) out vec2 uv;

void main() {
    const vec2 positions[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0),
                                      vec2(-1.0, 3.0));
    const vec2 texcoords[3] = vec2[3](vec2(0.0, 0.0), vec2(2.0, 0.0),
                                      vec2(0.0, 2.0));
#if defined(VULKAN) || defined(GL_KHR_vulkan_glsl)
    const int vertex_index = gl_VertexIndex;
#else
    const int vertex_index = gl_VertexID;
#endif
    gl_Position = vec4(positions[vertex_index], 0.0, 1.0);
    uv = texcoords[vertex_index];
}
