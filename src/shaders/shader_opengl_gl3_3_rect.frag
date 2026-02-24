#version 330 core
in vec3 v_color;
flat in int v_tile_index;
out vec4 out_color;

uniform uint u_gradient_head_row;
uniform uint u_gradient_window_rows;
uniform vec3 u_grid_base_color;
uniform uint u_grid_cols;
uniform uint u_grid_rows;
uniform vec3 u_grid_target_color;
uniform sampler2D u_history_tex;
uniform int u_mode_phase_flag;
uniform uint u_palette_cycle;
uniform uint u_pattern_seed;
uniform uint u_render_mode;
uniform uint u_snake_batch_size;
uniform uint u_snake_cursor;
uniform uint u_snake_rect_index;

struct db_rect_snake_desc_t {
    vec3 color;
    uint height;
    uint width;
    uint x;
    uint y;
};

vec4 db_rgba(vec3 color_rgb) {
    return vec4(color_rgb, 1.0);
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

uint db_mix_u32(uint value) {
    value ^= (value >> 16u);
    value *= 0x7FEB352Du;
    value ^= (value >> 15u);
    value *= 0x846CA68Bu;
    value ^= (value >> 16u);
    return value;
}

uint db_u32_range(uint seed, uint min_v, uint max_v) {
    if(max_v <= min_v) {
        return min_v;
    }
    uint span = (max_v - min_v) + 1u;
    return min_v + (seed % span);
}

uint db_u32_min(uint a, uint b) {
    return (a < b) ? a : b;
}

uint db_u32_max(uint a, uint b) {
    return (a > b) ? a : b;
}

uint db_u32_saturating_sub(uint a, uint b) {
    return (a > b) ? (a - b) : 0u;
}

float db_color_channel(uint seed) {
    return 0.20 + ((float(seed & 255u) / 255.0) * 0.75);
}

vec3 db_palette_cycle_color_rgb(uint cycle_u) {
    uint seed_base = db_mix_u32(((cycle_u + 1u) * 0x9E3779B9u) ^ 0xA511E9B3u);
    return vec3(db_color_channel(db_mix_u32(seed_base ^ 0x27D4EB2Fu)), db_color_channel(db_mix_u32(seed_base ^ 0x165667B1u)), db_color_channel(db_mix_u32(seed_base ^ 0x85EBCA77u)));
}

db_rect_snake_desc_t db_rect_snake_desc(
    uint pattern_seed,
    uint rect_index,
    uint rows_u,
    uint cols_u
) {
    db_rect_snake_desc_t rect;
    uint seed_base = db_mix_u32(pattern_seed + (rect_index * 0x85EBCA77u) + 1u);
    uint min_w = (cols_u >= 16u) ? 8u : 1u;
    uint min_h = (rows_u >= 16u) ? 8u : 1u;
    uint max_w = db_u32_max(min_w, (cols_u / 3u) + min_w);
    uint max_h = db_u32_max(min_h, (rows_u / 3u) + min_h);
    rect.width = db_u32_range(db_mix_u32(seed_base ^ 0xA511E9B3u), min_w, db_u32_min(max_w, cols_u));
    rect.height = db_u32_range(db_mix_u32(seed_base ^ 0x63D83595u), min_h, db_u32_min(max_h, rows_u));
    uint max_x = db_u32_saturating_sub(cols_u, rect.width);
    uint max_y = db_u32_saturating_sub(rows_u, rect.height);
    rect.x = db_u32_range(db_mix_u32(seed_base ^ 0x9E3779B9u), 0u, max_x);
    rect.y = db_u32_range(db_mix_u32(seed_base ^ 0xC2B2AE35u), 0u, max_y);
    rect.color.r = db_color_channel(db_mix_u32(seed_base ^ 0x27D4EB2Fu));
    rect.color.g = db_color_channel(db_mix_u32(seed_base ^ 0x165667B1u));
    rect.color.b = db_color_channel(db_mix_u32(seed_base ^ 0x85EBCA77u));
    return rect;
}

bool db_rect_contains(db_rect_snake_desc_t rect, uint row_u, uint col_u) {
    return (row_u >= rect.y) && (row_u < (rect.y + rect.height)) &&
        (col_u >= rect.x) && (col_u < (rect.x + rect.width));
}

uint db_rect_snake_step(db_rect_snake_desc_t rect, uint row_u, uint col_u) {
    uint local_row = row_u - rect.y;
    uint local_col = col_u - rect.x;
    uint snake_col = ((local_row & 1u) == 0u) ? local_col : ((rect.width - 1u) - local_col);
    return (local_row * rect.width) + snake_col;
}

vec3 db_target_color_for_phase(bool clearing) {
    return clearing ? u_grid_base_color : u_grid_target_color;
}

db_rect_snake_desc_t db_full_grid_rect(uint rows_u, uint cols_u) {
    db_rect_snake_desc_t rect;
    rect.x = 0u;
    rect.y = 0u;
    rect.width = cols_u;
    rect.height = rows_u;
    rect.color = vec3(0.0);
    return rect;
}

vec4 db_gradient_color(
    int row_i,
    uint head_row_u,
    uint cycle_u,
    bool direction_down
) {
    int rows_i = max(int(u_grid_rows), 1);
    int window_i = clamp(int(u_gradient_window_rows), 1, rows_i);
    int head_row = int(head_row_u);
    int head_i = head_row - window_i;
    vec3 source_color = db_palette_cycle_color_rgb(cycle_u);
    vec3 target_color = db_palette_cycle_color_rgb(cycle_u + 1u);
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

vec4 db_snake_color(
    db_rect_snake_desc_t rect,
    uint row_u,
    uint col_u,
    vec3 prior_color,
    vec3 target_color,
    uint cursor,
    uint batch_size
) {
    if(batch_size == 0u) {
        return db_rgba(prior_color);
    }
    if(!db_rect_contains(rect, row_u, col_u)) {
        return db_rgba(prior_color);
    }
    uint step = db_rect_snake_step(rect, row_u, col_u);
    if(step < cursor) {
        return db_rgba(target_color);
    }
    if(step < (cursor + batch_size)) {
        uint window_index = step - cursor;
        float blend = db_window_blend(int(batch_size), int(window_index));
        return db_rgba(db_blend_prior_to_target(prior_color, target_color, blend));
    }
    return db_rgba(prior_color);
}

void main() {
    const uint RENDER_MODE_GRADIENT_SWEEP = 0u;
    const uint RENDER_MODE_BANDS = 1u;
    const uint RENDER_MODE_SNAKE_GRID = 2u;
    const uint RENDER_MODE_GRADIENT_FILL = 3u;
    const uint RENDER_MODE_RECT_SNAKE = 4u;
    if(u_render_mode == RENDER_MODE_BANDS) {
        out_color = db_rgba(v_color);
        return;
    }
    int tile_index = v_tile_index;
    int cols = max(int(u_grid_cols), 1);
    int row = tile_index / cols;

    if((u_render_mode == RENDER_MODE_GRADIENT_SWEEP) ||
        (u_render_mode == RENDER_MODE_GRADIENT_FILL)) {
        bool is_sweep = (u_render_mode == RENDER_MODE_GRADIENT_SWEEP);
        bool direction_down = is_sweep ? (u_mode_phase_flag != 0) : true;
        out_color = db_gradient_color(row, u_gradient_head_row, u_palette_cycle, direction_down);
        return;
    }
    if((u_render_mode != RENDER_MODE_SNAKE_GRID) &&
        (u_render_mode != RENDER_MODE_RECT_SNAKE)) {
        out_color = db_rgba(v_color);
        return;
    }
    int col = tile_index - (row * cols);
    uint row_u = uint(max(row, 0));
    uint col_u = uint(max(col, 0));
    uint rows_u = max(u_grid_rows, 1u);
    uint cols_u = max(u_grid_cols, 1u);
    ivec2 history_coord = ivec2(gl_FragCoord.xy);
    vec3 prior_color = texelFetch(u_history_tex, history_coord, 0).rgb;
    uint batch_size_u = u_snake_batch_size;
    if(u_render_mode == RENDER_MODE_RECT_SNAKE) {
        db_rect_snake_desc_t rect = db_rect_snake_desc(u_pattern_seed, u_snake_rect_index, rows_u, cols_u);
        out_color = db_snake_color(rect, row_u, col_u, prior_color, rect.color, u_snake_cursor, batch_size_u);
    } else {
        db_rect_snake_desc_t rect = db_full_grid_rect(rows_u, cols_u);
        bool clearing = (u_mode_phase_flag != 0);
        vec3 target_color = db_target_color_for_phase(clearing);
        out_color = db_snake_color(rect, row_u, col_u, prior_color, target_color, u_snake_cursor, batch_size_u);
    }
}
