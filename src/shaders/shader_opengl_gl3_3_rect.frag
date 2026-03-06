#version 330 core
in vec3 v_color;
out vec4 out_color;

uniform uint u_band_count;
uniform int u_gradient_direction_flag;
uniform uint u_gradient_head_row;
uniform uint u_gradient_window_rows;
uniform uint u_grid_cols;
uniform uint u_grid_rows;
uniform vec3 u_grid_base_color;
uniform vec3 u_grid_target_color;
uniform sampler2D u_history_tex;
uniform int u_snake_phase_flag;
uniform int u_snake_phase_completed;
uniform uint u_palette_cycle;
uniform uint u_render_mode;
uniform uint u_snake_batch_size;
uniform uint u_snake_cursor;
uniform uint u_snake_shape_kind;
uniform uint u_snake_region_height;
uniform uint u_snake_region_width;
uniform uint u_snake_region_x;
uniform uint u_snake_region_y;
uniform vec3 u_snake_region_color;
uniform vec3 u_snake_profile0;
uniform vec3 u_snake_profile1;
uniform vec3 u_snake_profile2;
uniform vec3 u_snake_profile3;
uniform uint u_snake_triangle_variant;
uniform uint u_viewport_height;
uniform uint u_viewport_width;
uniform uint u_frame_index;

#include "shader_rect_common.inc.glsl"

int db_row_from_frag_coord(void) {
    float rows = float(max(u_grid_rows, 1u));
    float viewport_height = float(max(u_viewport_height, 1u));
    float y = clamp(gl_FragCoord.y, 0.0, viewport_height - 1.0);
    float y_top = (viewport_height - 1.0) - y;
    return int(floor((y_top * rows) / viewport_height));
}

int db_col_from_frag_coord(void) {
    float cols = float(max(u_grid_cols, 1u));
    float viewport_width = float(max(u_viewport_width, 1u));
    float x = clamp(gl_FragCoord.x, 0.0, viewport_width - 1.0);
    return int(floor((x * cols) / viewport_width));
}

vec3 db_target_color_for_phase(bool clearing) {
    return clearing ? u_grid_base_color : u_grid_target_color;
}

db_snake_shape_desc_t db_uniform_snake_shape_desc(void) {
    db_snake_region_desc_t region = db_snake_region_desc_t(
        u_snake_region_color, u_snake_region_height, u_snake_region_width,
        u_snake_region_x, u_snake_region_y);
    db_snake_shape_profile_t profile = db_snake_shape_profile_t(
        u_snake_profile0.x, u_snake_profile0.y, u_snake_profile0.z,
        u_snake_profile1.x, u_snake_profile1.y, u_snake_profile1.z,
        u_snake_profile2.x, u_snake_profile2.y, u_snake_profile2.z,
        u_snake_profile3.x, u_snake_profile3.y, u_snake_profile3.z, 0u,
        u_snake_triangle_variant);
    return db_snake_shape_desc_t(region, profile, u_snake_shape_kind);
}

vec4 db_gradient_color(int row_i, uint head_row_u, uint cycle_u,
                       bool direction_down) {
    int rows_i = max(int(u_grid_rows), 1);
    int window_i = clamp(int(u_gradient_window_rows), 1, rows_i);
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
    if (u_render_mode == RENDER_MODE_BANDS) {
        int col_i = db_col_from_frag_coord();
        uint cols_u = max(u_grid_cols, 1u);
        uint band_count_u = max(u_band_count, 1u);
        uint col_u = uint(max(col_i, 0));
        out_color = db_rgba(
            db_band_color(db_band_index_from_col(col_u, cols_u, band_count_u),
                          band_count_u, u_frame_index));
        return;
    }
    if ((u_render_mode == RENDER_MODE_GRADIENT_SWEEP) ||
        (u_render_mode == RENDER_MODE_GRADIENT_FILL)) {
        int row_i = db_row_from_frag_coord();
        bool is_sweep = (u_render_mode == RENDER_MODE_GRADIENT_SWEEP);
        bool direction_down =
            is_sweep ? (u_gradient_direction_flag != 0) : true;
        out_color = db_gradient_color(row_i, u_gradient_head_row,
                                      u_palette_cycle, direction_down);
        return;
    }
    if ((u_render_mode != RENDER_MODE_SNAKE_GRID) &&
        (u_render_mode != RENDER_MODE_SNAKE_RECT) &&
        (u_render_mode != RENDER_MODE_SNAKE_SHAPES)) {
        out_color = db_rgba(v_color);
        return;
    }

    int row_i = db_row_from_frag_coord();
    int col_i = db_col_from_frag_coord();
    uint cols_u = max(u_grid_cols, 1u);
    uint row_u = uint(max(row_i, 0));
    uint col_u = uint(max(col_i, 0));
    ivec2 history_coord = ivec2(gl_FragCoord.xy);
    vec3 prior_color = texelFetch(u_history_tex, history_coord, 0).rgb;
    uint batch_size_u = u_snake_batch_size;
    if ((u_render_mode == RENDER_MODE_SNAKE_RECT) ||
        (u_render_mode == RENDER_MODE_SNAKE_SHAPES)) {
        db_snake_shape_desc_t shape_desc = db_uniform_snake_shape_desc();
        out_color = db_snake_color(
            shape_desc, (u_render_mode == RENDER_MODE_SNAKE_SHAPES), row_u,
            col_u, prior_color, shape_desc.region.color, u_snake_cursor,
            batch_size_u, 0);
    } else {
        bool clearing = (u_snake_phase_flag != 0);
        vec3 target_color = db_target_color_for_phase(clearing);
        out_color = db_snake_grid_color(row_u, col_u, cols_u, prior_color,
                                        target_color, u_snake_cursor,
                                        batch_size_u, u_snake_phase_completed);
    }
}
