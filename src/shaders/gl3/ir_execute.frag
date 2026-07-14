#version 330 core

flat in vec3 v_start_color;
flat in vec3 v_end_color;
flat in vec4 v_gradient;
out vec4 out_color;

void main() {
    if (v_gradient.x < 0.5) {
        out_color = vec4(v_start_color, 1.0);
        return;
    }

    int target_height = int(v_gradient.w);
    int logical_row = target_height - 1 - int(floor(gl_FragCoord.y));
    int axis_start = int(v_gradient.y);
    int axis_end = int(v_gradient.z);
    float amount = 0.0;
    if (axis_end > axis_start) {
        int clamped_row = clamp(logical_row, axis_start, axis_end);
        amount = float(clamped_row - axis_start) / float(axis_end - axis_start);
    }
    out_color = vec4(mix(v_start_color, v_end_color, amount), 1.0);
}
