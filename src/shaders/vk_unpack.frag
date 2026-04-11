#version 450

#if defined(VULKAN) || defined(GL_KHR_vulkan_glsl)
layout(std430, set = 0, binding = 0) readonly buffer PackedPixels {
    uint words[];
};
#else
layout(std430, binding = 0) readonly buffer PackedPixels {
    uint words[];
};
#endif

layout(location = 0) out vec4 out_color;

#if defined(VULKAN) || defined(GL_KHR_vulkan_glsl)
layout(push_constant) uniform UnpackConstants {
    ivec2 destination_origin;
    uvec2 extent;
    uint row_words;
    uint word_offset;
    uint rgba16f;
} pc;
#else
layout(std140, binding = 1) uniform UnpackConstants {
    ivec2 destination_origin;
    uvec2 extent;
    uint row_words;
    uint word_offset;
    uint rgba16f;
} pc;
#endif

void main() {
    uvec2 p = uvec2(ivec2(gl_FragCoord.xy) - pc.destination_origin);
    uint index = pc.word_offset + (p.y * pc.row_words);
    if (pc.rgba16f != 0) {
        index += p.x * 2;
        out_color = vec4(unpackHalf2x16(words[index]),
                         unpackHalf2x16(words[index + 1]));
    } else {
        out_color = unpackUnorm4x8(words[index + p.x]);
    }
}
