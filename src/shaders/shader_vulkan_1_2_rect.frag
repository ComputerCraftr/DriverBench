#version 450
layout(location = 0) in vec4 v_color;
layout(location = 0) out vec4 out_color;
layout(set = 0, binding = 0) uniform sampler2D u_history_tex;

#if defined(VULKAN) || defined(GL_KHR_vulkan_glsl)
layout(push_constant) uniform PC {
    vec2 offset_ndc;
    vec2 scale_ndc;
    vec4 color;
    vec4 base_color;
    vec4 target_color;
    uint render_mode;
    uint grid_rows;
    uint gradient_head_row;
    int grid_clearing_phase;
    uint gradient_window_rows;
    uint viewport_height;
    uint grid_cols;
    uint snake_active_cursor;
    uint snake_batch_size;
    int snake_phase_completed;
    uint viewport_width;
    uint palette_cycle;
    uint pattern_seed;
} pc;
#else
layout(std140, binding = 0) uniform PC {
    vec2 offset_ndc;
    vec2 scale_ndc;
    vec4 color;
    vec4 base_color;
    vec4 target_color;
    uint render_mode;
    uint grid_rows;
    uint gradient_head_row;
    int grid_clearing_phase;
    uint gradient_window_rows;
    uint viewport_height;
    uint grid_cols;
    uint snake_active_cursor;
    uint snake_batch_size;
    int snake_phase_completed;
    uint viewport_width;
    uint palette_cycle;
    uint pattern_seed;
} pc;
#endif

const uint RENDER_MODE_GRADIENT_SWEEP = 0u;
const uint RENDER_MODE_BANDS = 1u;
const uint RENDER_MODE_SNAKE_GRID = 2u;
const uint RENDER_MODE_GRADIENT_FILL = 3u;
const uint RENDER_MODE_RECT_SNAKE = 4u;

struct db_rect_snake_desc_t {
    uint x;
    uint y;
    uint width;
    uint height;
    vec3 color;
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

vec3 db_target_color_for_phase(int clearing_phase) {
    return (clearing_phase != 0) ? pc.base_color.rgb : pc.target_color.rgb;
}

int db_row_from_frag_coord() {
    float rows = float(max(pc.grid_rows, 1u));
    float viewport_height = float(max(pc.viewport_height, 1u));
    float y = clamp(gl_FragCoord.y, 0.0, viewport_height - 1.0);
    return int(floor((y * rows) / viewport_height));
}

vec4 db_gradient_color(int row_i, uint head_row_u, uint cycle_u, bool direction_down) {
    int rows_i = max(int(pc.grid_rows), 1);
    int window_i = clamp(int(pc.gradient_window_rows), 1, rows_i);
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

int db_col_from_frag_coord() {
    float cols = float(max(pc.grid_cols, 1u));
    float viewport_width = float(max(pc.viewport_width, 1u));
    float x = clamp(gl_FragCoord.x, 0.0, viewport_width - 1.0);
    return int(floor((x * cols) / viewport_width));
}

vec4 db_snake_grid_color(int row_i, int col_i, vec3 prior_color) {
    int cols = max(int(pc.grid_cols), 1);
    int step = row_i * cols;
    if((row_i & 1) == 0) {
        step += col_i;
    } else {
        step += (cols - 1 - col_i);
    }

    int active_cursor = int(pc.snake_active_cursor);
    int batch_size = max(int(pc.snake_batch_size), 1);
    int phase_completed = pc.snake_phase_completed;
    int clearing_phase = pc.grid_clearing_phase;

    vec3 target_color = db_target_color_for_phase(clearing_phase);
    if(phase_completed != 0) {
        return db_rgba(target_color);
    }
    if(step < active_cursor) {
        return db_rgba(target_color);
    }
    if(step >= (active_cursor + batch_size)) {
        return db_rgba(prior_color);
    }

    int window_index = step - active_cursor;
    float blend = db_window_blend(batch_size, window_index);
    return db_rgba(db_blend_prior_to_target(prior_color, target_color, blend));
}

vec4 db_rect_snake_color(int row_i, int col_i, vec3 prior_color) {
    uint row_u = uint(max(row_i, 0));
    uint col_u = uint(max(col_i, 0));
    uint rows_u = max(pc.grid_rows, 1u);
    uint cols_u = max(pc.grid_cols, 1u);
    uint rect_index_u = pc.gradient_head_row;
    db_rect_snake_desc_t current_rect = db_rect_snake_desc(pc.pattern_seed, rect_index_u, rows_u, cols_u);
    if(!db_rect_contains(current_rect, row_u, col_u)) {
        return db_rgba(prior_color);
    }
    uint step = db_rect_snake_step(current_rect, row_u, col_u);
    uint cursor = pc.snake_active_cursor;
    uint batch_size = max(pc.snake_batch_size, 1u);
    if(step < cursor) {
        return db_rgba(current_rect.color);
    }
    if(step < (cursor + batch_size)) {
        uint window_index = step - cursor;
        float blend = db_window_blend(int(batch_size), int(window_index));
        return db_rgba(db_blend_prior_to_target(prior_color, current_rect.color, blend));
    }
    return db_rgba(prior_color);
}

void main() {
    uint render_mode = pc.render_mode;
    if(render_mode == RENDER_MODE_BANDS) {
        out_color = v_color;
        return;
    }
    int row_i = db_row_from_frag_coord();
    if((render_mode == RENDER_MODE_GRADIENT_SWEEP) ||
        (render_mode == RENDER_MODE_GRADIENT_FILL)) {
        bool is_sweep = (render_mode == RENDER_MODE_GRADIENT_SWEEP);
        int gradient_row = is_sweep ? int(pc.snake_active_cursor) : row_i;
        bool direction_down = is_sweep ? (pc.snake_phase_completed != 0) : true;
        out_color = db_gradient_color(gradient_row, pc.gradient_head_row, pc.palette_cycle, direction_down);
        return;
    }
    if(render_mode == RENDER_MODE_SNAKE_GRID) {
        ivec2 history_coord = ivec2(gl_FragCoord.xy);
        vec3 prior_color = texelFetch(u_history_tex, history_coord, 0).rgb;
        out_color = db_snake_grid_color(row_i, db_col_from_frag_coord(), prior_color);
        return;
    }
    if(render_mode == RENDER_MODE_RECT_SNAKE) {
        ivec2 history_coord = ivec2(gl_FragCoord.xy);
        vec3 prior_color = texelFetch(u_history_tex, history_coord, 0).rgb;
        out_color = db_rect_snake_color(row_i, db_col_from_frag_coord(), prior_color);
        return;
    }
    out_color = v_color;
}
