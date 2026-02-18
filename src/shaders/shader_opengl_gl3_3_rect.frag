#version 330 core
in vec3 v_color;
flat in int v_tile_index;
out vec4 out_color;

uniform int u_render_mode; // 0=gradient_sweep, 1=bands, 2=snake_grid, 3=gradient_fill
uniform int u_grid_clearing_phase;
uniform int u_grid_phase_completed;
uniform int u_grid_cursor;
uniform int u_grid_batch_size;
uniform int u_grid_cols;
uniform int u_grid_rows;
uniform int u_gradient_head_row;
uniform int u_gradient_window_rows;
uniform int u_gradient_fill_window_rows;
uniform vec3 u_grid_base_color;
uniform vec3 u_grid_target_color;

vec4 db_rgba(vec3 color_rgb) {
    return vec4(color_rgb, 1.0);
}

vec4 db_gradient_sweep_color(int row_i) {
    int rows_i = max(u_grid_rows, 1);
    int head_i = u_gradient_head_row % rows_i;
    if(head_i < 0) {
        head_i += rows_i;
    }
    row_i = row_i % rows_i;
    if(row_i < 0) {
        row_i += rows_i;
    }
    int window_i = clamp(u_gradient_window_rows, 1, rows_i);
    int delta_i = (row_i - head_i + rows_i) % rows_i;
    if(delta_i >= window_i) {
        return db_rgba(u_grid_target_color);
    }

    float half_span = (float(window_i) - 1.0) * 0.5;
    float blend = 0.0;
    if(half_span > 0.0) {
        blend = abs(float(delta_i) - half_span) / half_span;
    }
    return db_rgba(mix(u_grid_base_color, u_grid_target_color, blend));
}

vec4 db_gradient_fill_color(int row_i) {
    int rows_i = max(u_grid_rows, 1);
    int head_i = u_gradient_head_row % rows_i;
    if(head_i < 0) {
        head_i += rows_i;
    }
    bool clearing = (u_grid_clearing_phase != 0);
    vec3 source_color = clearing ? u_grid_target_color : u_grid_base_color;
    vec3 target_color = clearing ? u_grid_base_color : u_grid_target_color;
    if(row_i >= head_i) {
        return db_rgba(source_color);
    }

    int window_i = clamp(u_gradient_fill_window_rows, 1, rows_i);
    int delta_i = head_i - row_i;
    if(delta_i >= window_i) {
        return db_rgba(target_color);
    }
    return db_rgba(mix(source_color, target_color, float(delta_i) / float(window_i)));
}

void main() {
    const int RENDER_MODE_GRADIENT_SWEEP = 0;
    const int RENDER_MODE_BANDS = 1;
    const int RENDER_MODE_SNAKE_GRID = 2;
    const int RENDER_MODE_GRADIENT_FILL = 3;
    if(u_render_mode == RENDER_MODE_BANDS) {
        out_color = db_rgba(v_color);
        return;
    }
    int tile_index = v_tile_index;
    int cols = max(u_grid_cols, 1);
    int row = tile_index / cols;

    if(u_render_mode == RENDER_MODE_GRADIENT_SWEEP) {
        out_color = db_gradient_sweep_color(row);
        return;
    }
    if(u_render_mode == RENDER_MODE_GRADIENT_FILL) {
        out_color = db_gradient_fill_color(row);
        return;
    }
    if(u_render_mode != RENDER_MODE_SNAKE_GRID) {
        out_color = db_rgba(v_color);
        return;
    }

    int col = tile_index - (row * cols);
    bool reverse_row = (row & 1) != 0;
    int snake_col = reverse_row ? ((cols - 1) - col) : col;
    int tile_step = (row * cols) + snake_col;
    int cursor = u_grid_cursor;
    int batch_size = max(u_grid_batch_size, 1);
    bool clearing = (u_grid_clearing_phase != 0);
    vec3 source_color = clearing ? u_grid_target_color : u_grid_base_color;
    vec3 target_color = clearing ? u_grid_base_color : u_grid_target_color;

    if(tile_step < cursor) {
        out_color = db_rgba(target_color);
        return;
    }

    if(tile_step < (cursor + batch_size)) {
        if(u_grid_phase_completed != 0) {
            out_color = db_rgba(target_color);
            return;
        }
        int idx = tile_step - cursor;
        float blend = float(batch_size - idx) / float(batch_size);
        vec3 color = source_color + ((target_color - source_color) * blend);
        out_color = db_rgba(color);
        return;
    }

    out_color = db_rgba(source_color);
}
