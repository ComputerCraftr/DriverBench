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
uniform int u_gradient_fill_cycle;
uniform int u_rect_seed;
uniform vec3 u_grid_base_color;
uniform vec3 u_grid_target_color;
uniform sampler2D u_history_tex;

struct RectSnakeDesc {
    int x;
    int y;
    int w;
    int h;
    vec3 color;
};

uint db_mix_u32(uint value);
vec3 db_gradient_fill_palette_color_rgb(uint cycle_u);

vec4 db_rgba(vec3 color_rgb) {
    return vec4(color_rgb, 1.0);
}

vec3 db_target_color_for_phase(bool clearing) {
    return clearing ? u_grid_base_color : u_grid_target_color;
}

float db_window_blend(int batch_size, int window_index) {
    if(batch_size <= 1) {
        return 1.0;
    }
    return float((batch_size - 1) - window_index) / float(batch_size - 1);
}

vec3 db_blend_prior_to_target(
    vec3 prior_color,
    vec3 target_color,
    float blend
) {
    return mix(prior_color, target_color, blend);
}

vec4 db_gradient_sweep_color(int row_i) {
    int rows_i = max(u_grid_rows, 1);
    int window_i = clamp(u_gradient_window_rows, 1, rows_i);
    int head_i = u_gradient_head_row - window_i;
    uint cycle_u = uint(max(u_gradient_fill_cycle, 0));
    bool direction_down = (u_grid_clearing_phase != 0);
    vec3 source_color = db_gradient_fill_palette_color_rgb(cycle_u);
    vec3 target_color = db_gradient_fill_palette_color_rgb(cycle_u + 1u);
    if(row_i < head_i) {
        return db_rgba(direction_down ? target_color : source_color);
    }
    if(row_i >= (head_i + window_i)) {
        return db_rgba(direction_down ? source_color : target_color);
    }
    int delta_i = row_i - head_i;

    float blend = 1.0;
    if(window_i > 1) {
        float t = float(delta_i) / float(window_i - 1);
        blend = direction_down ? (1.0 - t) : t;
    }
    return db_rgba(mix(source_color, target_color, blend));
}

vec4 db_gradient_fill_color(int row_i) {
    int rows_i = max(u_grid_rows, 1);
    int head_i = max(u_gradient_head_row, 0);
    uint cycle_u = uint(max(u_gradient_fill_cycle, 0));
    vec3 source_color = db_gradient_fill_palette_color_rgb(cycle_u);
    vec3 target_color = db_gradient_fill_palette_color_rgb(cycle_u + 1u);
    if(row_i >= head_i) {
        return db_rgba(source_color);
    }

    int window_i = clamp(u_gradient_fill_window_rows, 1, rows_i);
    int delta_i = head_i - row_i;
    if(delta_i >= window_i) {
        return db_rgba(target_color);
    }
    if(window_i <= 1) {
        return db_rgba(target_color);
    }
    return db_rgba(mix(source_color, target_color, float(delta_i) / float(window_i - 1)));
}

uint db_mix_u32(uint value) {
    value ^= (value >> 16u);
    value *= 0x7FEB352Du;
    value ^= (value >> 15u);
    value *= 0x846CA68Bu;
    value ^= (value >> 16u);
    return value;
}

int db_u32_range(uint seed, int min_v, int max_v) {
    if(max_v <= min_v) {
        return min_v;
    }
    uint span = uint((max_v - min_v) + 1);
    return min_v + int(seed % span);
}

float db_rect_channel(uint seed) {
    return 0.25 + ((float(seed & 255u) / 255.0) * 0.70);
}

float db_gradient_fill_color_channel(uint seed) {
    return 0.20 + ((float(seed & 255u) / 255.0) * 0.75);
}

vec3 db_gradient_fill_palette_color_rgb(uint cycle_u) {
    uint seed_base = db_mix_u32(((cycle_u + 1u) * 0x9E3779B9u) ^ 0xA511E9B3u);
    return vec3(db_gradient_fill_color_channel(db_mix_u32(seed_base ^ 0x27D4EB2Fu)), db_gradient_fill_color_channel(db_mix_u32(seed_base ^ 0x165667B1u)), db_gradient_fill_color_channel(db_mix_u32(seed_base ^ 0x85EBCA77u)));
}

RectSnakeDesc db_rect_snake_desc(
    int rect_seed,
    uint rect_index,
    int rows_i,
    int cols_i
) {
    RectSnakeDesc rect;
    uint seed_base = db_mix_u32(uint(max(rect_seed, 0)) + (rect_index * 0x85EBCA77u) + 1u);
    int min_w = (cols_i >= 16) ? 8 : 1;
    int min_h = (rows_i >= 16) ? 8 : 1;
    int max_w = (cols_i >= min_w) ? ((cols_i / 3) + min_w) : min_w;
    int max_h = (rows_i >= min_h) ? ((rows_i / 3) + min_h) : min_h;
    rect.w = db_u32_range(db_mix_u32(seed_base ^ 0xA511E9B3u), min_w, min(max_w, cols_i));
    rect.h = db_u32_range(db_mix_u32(seed_base ^ 0x63D83595u), min_h, min(max_h, rows_i));
    int max_x = (cols_i > rect.w) ? (cols_i - rect.w) : 0;
    int max_y = (rows_i > rect.h) ? (rows_i - rect.h) : 0;
    rect.x = db_u32_range(db_mix_u32(seed_base ^ 0x9E3779B9u), 0, max_x);
    rect.y = db_u32_range(db_mix_u32(seed_base ^ 0xC2B2AE35u), 0, max_y);
    rect.color.r = db_rect_channel(db_mix_u32(seed_base ^ 0x27D4EB2Fu));
    rect.color.g = db_rect_channel(db_mix_u32(seed_base ^ 0x165667B1u));
    rect.color.b = db_rect_channel(db_mix_u32(seed_base ^ 0x85EBCA77u));
    return rect;
}

bool db_rect_contains(RectSnakeDesc rect, int row_i, int col_i) {
    return (row_i >= rect.y) && (row_i < (rect.y + rect.h)) &&
        (col_i >= rect.x) && (col_i < (rect.x + rect.w));
}

int db_rect_snake_step(RectSnakeDesc rect, int row_i, int col_i) {
    int local_row = row_i - rect.y;
    int local_col = col_i - rect.x;
    int snake_col = ((local_row & 1) == 0) ? local_col : ((rect.w - 1) - local_col);
    return (local_row * rect.w) + snake_col;
}

vec4 db_rect_snake_color(int row_i, int col_i, vec3 prior_color) {
    int rows_i = max(u_grid_rows, 1);
    int cols_i = max(u_grid_cols, 1);
    int rect_seed = max(u_rect_seed, 0);
    uint rect_index_u = uint(max(u_gradient_head_row, 0));
    RectSnakeDesc current_rect = db_rect_snake_desc(rect_seed, rect_index_u, rows_i, cols_i);
    if(!db_rect_contains(current_rect, row_i, col_i)) {
        return db_rgba(prior_color);
    }

    int step = db_rect_snake_step(current_rect, row_i, col_i);
    int cursor = max(u_grid_cursor, 0);
    int batch_size = max(u_grid_batch_size, 1);
    if(step < cursor) {
        return db_rgba(current_rect.color);
    }
    if(step < (cursor + batch_size)) {
        int window_index = step - cursor;
        float blend = db_window_blend(batch_size, window_index);
        return db_rgba(db_blend_prior_to_target(prior_color, current_rect.color, blend));
    }
    return db_rgba(prior_color);
}

void main() {
    const int RENDER_MODE_GRADIENT_SWEEP = 0;
    const int RENDER_MODE_BANDS = 1;
    const int RENDER_MODE_SNAKE_GRID = 2;
    const int RENDER_MODE_GRADIENT_FILL = 3;
    const int RENDER_MODE_RECT_SNAKE = 4;
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
    if(u_render_mode == RENDER_MODE_RECT_SNAKE) {
        int col = tile_index - (row * cols);
        ivec2 history_coord = ivec2(gl_FragCoord.xy);
        vec3 prior_color = texelFetch(u_history_tex, history_coord, 0).rgb;
        out_color = db_rect_snake_color(row, col, prior_color);
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
    vec3 target_color = db_target_color_for_phase(clearing);

    if(tile_step < cursor) {
        out_color = db_rgba(target_color);
        return;
    }

    if(tile_step < (cursor + batch_size)) {
        ivec2 history_coord = ivec2(gl_FragCoord.xy);
        vec3 prior_color = texelFetch(u_history_tex, history_coord, 0).rgb;
        int idx = tile_step - cursor;
        float blend = db_window_blend(batch_size, idx);
        vec3 color = db_blend_prior_to_target(prior_color, target_color, blend);
        out_color = db_rgba(color);
        return;
    }

    ivec2 history_coord = ivec2(gl_FragCoord.xy);
    vec3 prior_color = texelFetch(u_history_tex, history_coord, 0).rgb;
    out_color = db_rgba(prior_color);
}
