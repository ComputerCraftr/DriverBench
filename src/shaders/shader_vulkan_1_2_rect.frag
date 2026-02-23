#version 450
layout(location = 0) in vec4 v_color;
layout(location = 0) out vec4 out_color;
layout(set = 0, binding = 0) uniform sampler2D u_history_tex;

#if defined(VULKAN) || defined(GL_KHR_vulkan_glsl)
layout(push_constant) uniform PC {
    vec2 offset_ndc;
    vec2 scale_ndc;
    vec4 color;
    ivec4 render_params;   // x=mode, y=grid_rows, z=head_row, w=mode_dependent
    ivec4 gradient_params; // x=sweep_window_rows, y=fill_window_rows,
                           // z=viewport_height, w=grid_cols
    ivec4 snake_params;    // x=active_cursor, y=batch_size, z=phase_completed,
                           // w=viewport_width
    vec4 base_color;
    vec4 target_color;
    uint gradient_sweep_cycle;
    uint gradient_fill_cycle;
    uint pattern_seed;
} pc;
#else
layout(std140, binding = 0) uniform PC {
    vec2 offset_ndc;
    vec2 scale_ndc;
    vec4 color;
    ivec4 render_params;
    ivec4 gradient_params;
    ivec4 snake_params;
    vec4 base_color;
    vec4 target_color;
    uint gradient_sweep_cycle;
    uint gradient_fill_cycle;
    uint pattern_seed;
} pc;
#endif

const int RENDER_MODE_GRADIENT_SWEEP = 0;
const int RENDER_MODE_BANDS = 1;
const int RENDER_MODE_SNAKE_GRID = 2;
const int RENDER_MODE_GRADIENT_FILL = 3;
const int RENDER_MODE_RECT_SNAKE = 4;

struct db_rect_snake_desc_t {
    int x;
    int y;
    int w;
    int h;
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

float db_palette_color_channel(uint seed) {
    return 0.20 + ((float(seed & 255u) / 255.0) * 0.75);
}

vec3 db_palette_cycle_color_rgb(uint cycle_u) {
    uint seed_base = db_mix_u32(((cycle_u + 1u) * 0x9E3779B9u) ^ 0xA511E9B3u);
    return vec3(db_palette_color_channel(db_mix_u32(seed_base ^ 0x27D4EB2Fu)), db_palette_color_channel(db_mix_u32(seed_base ^ 0x165667B1u)), db_palette_color_channel(db_mix_u32(seed_base ^ 0x85EBCA77u)));
}

db_rect_snake_desc_t db_rect_snake_desc(
    uint pattern_seed,
    uint rect_index,
    int rows_i,
    int cols_i
) {
    db_rect_snake_desc_t rect;
    uint seed_base = db_mix_u32(pattern_seed + (rect_index * 0x85EBCA77u) + 1u);
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

bool db_rect_contains(db_rect_snake_desc_t rect, int row_i, int col_i) {
    return (row_i >= rect.y) && (row_i < (rect.y + rect.h)) &&
        (col_i >= rect.x) && (col_i < (rect.x + rect.w));
}

int db_rect_snake_step(db_rect_snake_desc_t rect, int row_i, int col_i) {
    int local_row = row_i - rect.y;
    int local_col = col_i - rect.x;
    int snake_col = ((local_row & 1) == 0) ? local_col : ((rect.w - 1) - local_col);
    return (local_row * rect.w) + snake_col;
}

vec3 db_target_color_for_phase(int clearing_phase) {
    return (clearing_phase != 0) ? pc.base_color.rgb : pc.target_color.rgb;
}

int db_row_from_frag_coord() {
    float rows = float(max(pc.render_params.y, 1));
    float viewport_height = float(max(pc.gradient_params.z, 1));
    float y = clamp(gl_FragCoord.y, 0.0, viewport_height - 1.0);
    return int(floor((y * rows) / viewport_height));
}

vec4 db_gradient_color(int row_i, int head_row, uint cycle_u, bool direction_down) {
    int rows_i = max(pc.render_params.y, 1);
    int window_i = clamp(pc.gradient_params.x, 1, rows_i);
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
    float cols = float(max(pc.gradient_params.w, 1));
    float viewport_width = float(max(pc.snake_params.w, 1));
    float x = clamp(gl_FragCoord.x, 0.0, viewport_width - 1.0);
    return int(floor((x * cols) / viewport_width));
}

vec4 db_snake_grid_color(int row_i, int col_i, vec3 prior_color) {
    int cols = max(pc.gradient_params.w, 1);
    int step = row_i * cols;
    if((row_i & 1) == 0) {
        step += col_i;
    } else {
        step += (cols - 1 - col_i);
    }

    int active_cursor = max(pc.snake_params.x, 0);
    int batch_size = max(pc.snake_params.y, 1);
    int phase_completed = pc.snake_params.z;
    int clearing_phase = pc.render_params.w;

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
    int rows_i = max(pc.render_params.y, 1);
    int cols_i = max(pc.gradient_params.w, 1);
    uint rect_index_u = uint(max(pc.render_params.z, 0));
    db_rect_snake_desc_t current_rect = db_rect_snake_desc(pc.pattern_seed, rect_index_u, rows_i, cols_i);
    if(!db_rect_contains(current_rect, row_i, col_i)) {
        return db_rgba(prior_color);
    }
    int step = db_rect_snake_step(current_rect, row_i, col_i);
    int cursor = max(pc.snake_params.x, 0);
    int batch_size = max(pc.snake_params.y, 1);
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
    int render_mode = pc.render_params.x;
    if(render_mode == RENDER_MODE_BANDS) {
        out_color = v_color;
        return;
    }
    int row_i = db_row_from_frag_coord();
    if(render_mode == RENDER_MODE_GRADIENT_SWEEP) {
        out_color = db_gradient_color(pc.snake_params.x, pc.render_params.z, pc.gradient_sweep_cycle, (pc.snake_params.z != 0));
        return;
    }
    if(render_mode == RENDER_MODE_GRADIENT_FILL) {
        out_color = db_gradient_color(row_i, pc.render_params.z, pc.gradient_fill_cycle, true);
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
