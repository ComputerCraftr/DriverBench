#version 330 core

uniform usamplerBuffer row_lookup;

flat in vec3 v_start_color;
flat in float v_mode;
flat in uint v_lookup_base;
flat in int v_lookup_origin;
flat in int v_target_height;
out vec4 out_color;

void main() {
    if (v_mode < 1.5) {
        out_color = vec4(v_start_color, 1.0);
        return;
    }
    int logical_row = v_target_height - 1 - int(floor(gl_FragCoord.y));
    uint row_offset = uint(max(logical_row - v_lookup_origin, 0));
    uint row_bits =
        texelFetch(row_lookup, int(v_lookup_base + row_offset)).r;
    uvec4 rgba = uvec4(
        row_bits & 0xffu,
        (row_bits >> 8u) & 0xffu,
        (row_bits >> 16u) & 0xffu,
        (row_bits >> 24u) & 0xffu);
    out_color = vec4(rgba) / 255.0;
}
