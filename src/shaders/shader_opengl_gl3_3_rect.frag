#version 330 core
in vec3 v_color;
flat in int v_tile_index;
out vec4 out_color;

uniform int u_render_mode; // 0=gradient_sweep, 1=bands, 2=snake_grid
uniform int u_snake_clearing_phase;
uniform int u_snake_phase_completed;
uniform float u_snake_cursor;
uniform float u_snake_batch_size;
uniform float u_snake_cols;
uniform float u_grid_rows;
uniform float u_gradient_head_row;
uniform float u_gradient_window_rows;
uniform vec3 u_snake_base_color;
uniform vec3 u_snake_target_color;

void main() {
    const int RENDER_MODE_GRADIENT_SWEEP = 0;
    const int RENDER_MODE_BANDS = 1;
    const int RENDER_MODE_SNAKE_GRID = 2;
    if(u_render_mode == RENDER_MODE_BANDS) {
        out_color = vec4(v_color, 1.0);
        return;
    }
    float tile_index = float(v_tile_index);
    float cols = max(u_snake_cols, 1.0);
    float row = floor(tile_index / cols);

    if(u_render_mode == RENDER_MODE_GRADIENT_SWEEP) {
        float rows = max(u_grid_rows, 1.0);
        float head_row = mod(u_gradient_head_row, rows);
        float window_rows = clamp(u_gradient_window_rows, 1.0, rows);
        float delta = mod((row + rows - head_row), rows);
        if(delta >= window_rows) {
            out_color = vec4(u_snake_target_color, 1.0);
            return;
        }

        float half_span = (window_rows - 1.0) * 0.5;
        float blend = 0.0;
        if(half_span > 0.0) {
            blend = abs(delta - half_span) / half_span;
        }
        vec3 gradient_color = mix(u_snake_base_color, u_snake_target_color, blend);
        out_color = vec4(gradient_color, 1.0);
        return;
    }
    if(u_render_mode != RENDER_MODE_SNAKE_GRID) {
        out_color = vec4(v_color, 1.0);
        return;
    }

    float col = tile_index - (row * cols);
    bool reverse_row = mod(row, 2.0) > 0.5;
    float snake_col = reverse_row ? (cols - 1.0 - col) : col;
    float tile_step = (row * cols) + snake_col;
    float cursor = u_snake_cursor;
    float batch_size = max(u_snake_batch_size, 1.0);
    bool clearing = (u_snake_clearing_phase != 0);
    vec3 source_color = clearing ? u_snake_target_color : u_snake_base_color;
    vec3 target_color = clearing ? u_snake_base_color : u_snake_target_color;

    if(tile_step < cursor) {
        out_color = vec4(target_color, 1.0);
        return;
    }

    if(tile_step < (cursor + batch_size)) {
        if(u_snake_phase_completed != 0) {
            out_color = vec4(target_color, 1.0);
            return;
        }
        float idx = tile_step - cursor;
        float blend = (batch_size - idx) / batch_size;
        vec3 color = source_color + ((target_color - source_color) * blend);
        out_color = vec4(color, 1.0);
        return;
    }

    out_color = vec4(source_color, 1.0);
}
