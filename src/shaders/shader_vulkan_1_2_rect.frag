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
    uint gradient_head_row;
    uint gradient_window_rows;
    uint grid_cols;
    uint grid_rows;
    int gradient_direction_flag;
    uint palette_cycle;
    uint render_mode;
    uint snake_batch_size;
    uint snake_cursor;
    int snake_phase_flag;
    int snake_phase_completed;
    uint snake_shape_kind;
    uint snake_region_height;
    uint snake_region_width;
    uint snake_region_x;
    uint snake_region_y;
    vec4 snake_region_color;
    vec4 snake_profile0;
    vec4 snake_profile1;
    vec4 snake_profile2;
    uint snake_triangle_variant;
    uint viewport_height;
    uint viewport_width;
    uint frame_index;
    uint band_count;
}
pc;
#else
layout(std140, binding = 0) uniform PC {
    vec2 offset_ndc;
    vec2 scale_ndc;
    vec4 color;
    vec4 base_color;
    vec4 target_color;
    uint gradient_head_row;
    uint gradient_window_rows;
    uint grid_cols;
    uint grid_rows;
    int gradient_direction_flag;
    uint palette_cycle;
    uint render_mode;
    uint snake_batch_size;
    uint snake_cursor;
    int snake_phase_flag;
    int snake_phase_completed;
    uint snake_shape_kind;
    uint snake_region_height;
    uint snake_region_width;
    uint snake_region_x;
    uint snake_region_y;
    vec4 snake_region_color;
    vec4 snake_profile0;
    vec4 snake_profile1;
    vec4 snake_profile2;
    uint snake_triangle_variant;
    uint viewport_height;
    uint viewport_width;
    uint frame_index;
    uint band_count;
}
pc;
#endif

#include "shader_rect_common.inc.glsl"

int db_row_from_frag_coord(void) {
    float rows = float(max(pc.grid_rows, 1u));
    float viewport_height = float(max(pc.viewport_height, 1u));
    float y = clamp(gl_FragCoord.y, 0.0, viewport_height - 1.0);
    return int(floor((y * rows) / viewport_height));
}

int db_col_from_frag_coord(void) {
    float cols = float(max(pc.grid_cols, 1u));
    float viewport_width = float(max(pc.viewport_width, 1u));
    float x = clamp(gl_FragCoord.x, 0.0, viewport_width - 1.0);
    return int(floor((x * cols) / viewport_width));
}

vec3 db_target_color_for_phase(int phase_flag) {
    return (phase_flag != 0) ? pc.base_color.rgb : pc.target_color.rgb;
}

db_snake_shape_desc_t db_push_snake_shape_desc(void) {
    db_snake_region_desc_t region = db_snake_region_desc_t(
        pc.snake_region_color.rgb, pc.snake_region_height,
        pc.snake_region_width, pc.snake_region_x, pc.snake_region_y);
    db_snake_shape_profile_t profile = db_snake_shape_profile_t(
        pc.snake_profile0.x, pc.snake_profile0.y, pc.snake_profile0.z,
        pc.snake_profile0.w, pc.snake_profile1.x, pc.snake_profile1.y,
        pc.snake_profile1.z, pc.snake_profile1.w, pc.snake_profile2.x,
        pc.snake_profile2.y, pc.snake_profile2.z, pc.snake_profile2.w, 0u,
        pc.snake_triangle_variant);
    return db_snake_shape_desc_t(region, profile, pc.snake_shape_kind);
}

vec4 db_gradient_color(int row_i, uint head_row_u, uint cycle_u,
                       bool direction_down) {
    int rows_i = max(int(pc.grid_rows), 1);
    int window_i = clamp(int(pc.gradient_window_rows), 1, rows_i);
    int head_row = int(head_row_u);
    int head_i = head_row - window_i;
    vec3 source_color = db_palette_cycle_color_rgb(cycle_u);
    vec3 target_color = db_palette_cycle_color_rgb(cycle_u + 1u);
    if (row_i < head_i) {
        return db_rgba(direction_down ? target_color : source_color);
    }
    if (row_i >= (head_i + window_i)) {
        return db_rgba(direction_down ? source_color : target_color);
    }
    int delta_i = row_i - head_i;
    float blend = 1.0;
    if (window_i > 1) {
        float t = float(delta_i) / float(window_i - 1);
        blend = direction_down ? (1.0 - t) : t;
    }
    return db_rgba(mix(source_color, target_color, blend));
}

void main() {
    const uint RENDER_MODE_GRADIENT_SWEEP = 0u;
    const uint RENDER_MODE_BANDS = 1u;
    const uint RENDER_MODE_SNAKE_GRID = 2u;
    const uint RENDER_MODE_GRADIENT_FILL = 3u;
    const uint RENDER_MODE_SNAKE_RECT = 4u;
    const uint RENDER_MODE_SNAKE_SHAPES = 5u;
    uint render_mode_u = pc.render_mode;

    if (render_mode_u == RENDER_MODE_BANDS) {
        int col_i = db_col_from_frag_coord();
        uint col_u = uint(max(col_i, 0));
        uint cols_u = max(pc.grid_cols, 1u);
        uint band_count_u = max(pc.band_count, 1u);
        out_color = db_rgba(
            db_band_color(db_band_index_from_col(col_u, cols_u, band_count_u),
                          band_count_u, pc.frame_index));
        return;
    }
    if ((render_mode_u == RENDER_MODE_GRADIENT_SWEEP) ||
        (render_mode_u == RENDER_MODE_GRADIENT_FILL)) {
        int row_i = db_row_from_frag_coord();
        bool is_sweep = (render_mode_u == RENDER_MODE_GRADIENT_SWEEP);
        bool direction_down =
            is_sweep ? (pc.gradient_direction_flag != 0) : true;
        out_color = db_gradient_color(row_i, pc.gradient_head_row,
                                      pc.palette_cycle, direction_down);
        return;
    }
    if ((render_mode_u != RENDER_MODE_SNAKE_GRID) &&
        (render_mode_u != RENDER_MODE_SNAKE_RECT) &&
        (render_mode_u != RENDER_MODE_SNAKE_SHAPES)) {
        out_color = v_color;
        return;
    }

    int row_i = db_row_from_frag_coord();
    int col_i = db_col_from_frag_coord();
    ivec2 history_coord = ivec2(gl_FragCoord.xy);
    vec3 prior_color = texelFetch(u_history_tex, history_coord, 0).rgb;
    uint row_u = uint(max(row_i, 0));
    uint col_u = uint(max(col_i, 0));
    uint cols_u = max(pc.grid_cols, 1u);
    uint batch_size = pc.snake_batch_size;
    if ((render_mode_u == RENDER_MODE_SNAKE_RECT) ||
        (render_mode_u == RENDER_MODE_SNAKE_SHAPES)) {
        db_snake_shape_desc_t shape_desc = db_push_snake_shape_desc();
        out_color = db_snake_color(
            shape_desc, (render_mode_u == RENDER_MODE_SNAKE_SHAPES), row_u,
            col_u, prior_color, shape_desc.region.color, pc.snake_cursor,
            batch_size, 0);
    } else {
        vec3 target_color = db_target_color_for_phase(pc.snake_phase_flag);
        out_color = db_snake_grid_color(row_u, col_u, cols_u, prior_color,
                                        target_color, pc.snake_cursor,
                                        batch_size, pc.snake_phase_completed);
    }
}
