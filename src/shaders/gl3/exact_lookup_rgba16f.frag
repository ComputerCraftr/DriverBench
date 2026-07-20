#version 330 core
#extension GL_ARB_shading_language_packing : require

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
    uvec2 row_bits =
        texelFetch(row_lookup, int(v_lookup_base + row_offset)).rg;
    vec2 rg = unpackHalf2x16(row_bits.x);
    vec2 ba = unpackHalf2x16(row_bits.y);
    out_color = vec4(rg, ba);
}
