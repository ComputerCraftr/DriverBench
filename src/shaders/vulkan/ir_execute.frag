#version 450

layout(location = 0) flat in vec4 v_start_color;
layout(location = 1) flat in vec4 v_end_color;
layout(location = 2) flat in vec4 v_gradient;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 1, std430) readonly buffer GradientLookup {
    uint words[];
} lookup_data;

void main() {
    if (v_gradient.x < 0.5) {
        out_color = v_start_color;
        return;
    }
    int logical_row = int(floor(gl_FragCoord.y));
    if (v_gradient.x >= 1.5) {
        uint relative_row = uint(max(logical_row - int(v_gradient.y), 0));
        uint word_offset = uint(v_gradient.z);
        if (v_gradient.x < 2.5) {
            out_color = unpackUnorm4x8(lookup_data.words[word_offset +
                                                        relative_row]);
        } else {
            uint row_offset = word_offset + (relative_row * 2U);
            vec2 rg = unpackHalf2x16(lookup_data.words[row_offset]);
            vec2 ba = unpackHalf2x16(lookup_data.words[row_offset + 1U]);
            out_color = vec4(rg, ba);
        }
        return;
    }
    int axis_start = int(v_gradient.y);
    int axis_end = int(v_gradient.z);
    float amount = 0.0;
    if (axis_end > axis_start) {
        int clamped_row = clamp(logical_row, axis_start, axis_end);
        amount = float(clamped_row - axis_start) /
                 float(axis_end - axis_start);
    }
    out_color = mix(v_start_color, v_end_color, amount);
}
