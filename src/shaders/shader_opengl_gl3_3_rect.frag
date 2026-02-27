#version 330 core
in vec3 v_color;
flat in int v_tile_index;
out vec4 out_color;

uniform uint u_gradient_head_row;
uniform uint u_gradient_window_rows;
uniform vec3 u_grid_base_color;
uniform uint u_band_count;
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
uniform uint u_snake_shape_index;
uniform float u_time_s;
uniform uint u_viewport_width;

const uint SHAPE_KIND_RECT = 0u;
const uint SHAPE_KIND_CIRCLE = 1u;
const uint SHAPE_KIND_DIAMOND = 2u;
const uint SHAPE_KIND_TRIANGLE = 3u;
const uint SHAPE_KIND_TRAPEZOID = 4u;
const uint SHAPE_KIND_COUNT = 5u;

struct db_snake_region_desc_t {
    vec3 color;
    uint height;
    uint width;
    uint x;
    uint y;
};

struct db_snake_shape_profile_t {
    float circle_radius_x;
    float circle_radius_y;
    float diamond_radius;
    float triangle_bottom_width;
    float trapezoid_top_width;
    float trapezoid_bottom_width;
    float rect_half_width;
    float rect_half_height;
    float extent_x;
    float extent_y;
    float rotate_cos;
    float rotate_sin;
    uint rotate_enabled;
    uint triangle_variant;
};

struct db_snake_shape_desc_t {
    db_snake_region_desc_t region;
    db_snake_shape_profile_t profile;
    uint kind;
};

vec4 db_rgba(vec3 color_rgb) {
    return vec4(color_rgb, 1.0);
}

vec3 db_band_color(uint band_index, uint band_count, float time_s) {
    float band_f = float(band_index);
    float pulse = 0.5 + (0.5 * sin((time_s * 2.5) + (band_f * 0.4)));
    float color_r = pulse * (0.1 + (0.8 * band_f / float(max(band_count, 1u))));
    return vec3(color_r, pulse * 0.7, 1.0 - color_r);
}

uint db_band_index_from_frag_coord_x() {
    float viewport_width = float(max(u_viewport_width, 1u));
    float bands = float(max(u_band_count, 1u));
    float x = clamp(gl_FragCoord.x, 0.0, viewport_width - 1.0);
    return uint(floor((x * bands) / viewport_width));
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

db_snake_region_desc_t db_snake_region_desc(
    uint pattern_seed,
    uint shape_index,
    uint rows_u,
    uint cols_u
) {
    db_snake_region_desc_t region;
    uint seed_base = db_mix_u32(pattern_seed + (shape_index * 0x85EBCA77u) + 1u);
    uint min_w = (cols_u >= 16u) ? 8u : 1u;
    uint min_h = (rows_u >= 16u) ? 8u : 1u;
    uint max_w = db_u32_max(min_w, (cols_u / 3u) + min_w);
    uint max_h = db_u32_max(min_h, (rows_u / 3u) + min_h);
    region.width = db_u32_range(db_mix_u32(seed_base ^ 0xA511E9B3u), min_w, db_u32_min(max_w, cols_u));
    region.height = db_u32_range(db_mix_u32(seed_base ^ 0x63D83595u), min_h, db_u32_min(max_h, rows_u));
    uint max_x = db_u32_saturating_sub(cols_u, region.width);
    uint max_y = db_u32_saturating_sub(rows_u, region.height);
    region.x = db_u32_range(db_mix_u32(seed_base ^ 0x9E3779B9u), 0u, max_x);
    region.y = db_u32_range(db_mix_u32(seed_base ^ 0xC2B2AE35u), 0u, max_y);
    region.color.r = db_color_channel(db_mix_u32(seed_base ^ 0x27D4EB2Fu));
    region.color.g = db_color_channel(db_mix_u32(seed_base ^ 0x165667B1u));
    region.color.b = db_color_channel(db_mix_u32(seed_base ^ 0x85EBCA77u));
    return region;
}

bool db_snake_region_contains(
    db_snake_region_desc_t region,
    uint row_u,
    uint col_u
) {
    return (row_u >= region.y) && (row_u < (region.y + region.height)) &&
        (col_u >= region.x) && (col_u < (region.x + region.width));
}

uint db_snake_region_step(
    db_snake_region_desc_t region,
    uint row_u,
    uint col_u
) {
    uint local_row = row_u - region.y;
    uint local_col = col_u - region.x;
    uint snake_col = ((local_row & 1u) == 0u) ? local_col : ((region.width - 1u) - local_col);
    return (local_row * region.width) + snake_col;
}

vec3 db_target_color_for_phase(bool clearing) {
    return clearing ? u_grid_base_color : u_grid_target_color;
}

uint db_snake_shapes_kind(uint pattern_seed, uint shape_index) {
    uint mixed = db_mix_u32(pattern_seed ^ (shape_index * 0xA511E9B3u));
    return mixed % SHAPE_KIND_COUNT;
}

float db_unit_float_from_u32(uint value) {
    const float U24_MAX_F = 16777215.0;
    uint value_24 = value >> 8u;
    return float(value_24) / U24_MAX_F;
}

float db_float_range_from_u32(uint value, float min_value, float max_value) {
    if(max_value <= min_value) {
        return min_value;
    }
    return min_value +
        (db_unit_float_from_u32(value) * (max_value - min_value));
}

db_snake_shape_profile_t db_snake_shape_profile_from_index(
    uint pattern_seed,
    uint shape_index,
    uint shape_kind
) {
    const float SHAPE_CENTER = 0.5;
    const float SHAPE_CIRCLE_RADIUS_MIN = 0.30;
    const float SHAPE_CIRCLE_RADIUS_MAX = 0.50;
    const float SHAPE_DIAMOND_RADIUS_MIN = 0.40;
    const float SHAPE_DIAMOND_RADIUS_MAX = 0.60;
    const float SHAPE_EDGE_INSET = 0.05;
    const float SHAPE_EXTENT_SAFETY_MARGIN = 0.000001;
    const float SHAPE_EXTENT_MIN = 0.82;
    const float SHAPE_EXTENT_MAX = 0.98;
    const float SHAPE_RECT_HALF_WIDTH_MIN = 0.30;
    const float SHAPE_RECT_HALF_WIDTH_MAX = 0.50;
    const float SHAPE_RECT_HALF_HEIGHT_MIN = 0.30;
    const float SHAPE_RECT_HALF_HEIGHT_MAX = 0.50;
    const float SHAPE_ROTATION_FULL_TURN_RAD = 6.2831853;
    const float SHAPE_TRI_BOTTOM_WIDTH_MIN = 0.70;
    const float SHAPE_TRI_BOTTOM_WIDTH_MAX = 0.90;
    const float SHAPE_TRAP_TOP_WIDTH_MIN = 0.20;
    const float SHAPE_TRAP_TOP_WIDTH_MAX = 0.75;
    const float SHAPE_TRAP_BOTTOM_WIDTH_MIN = 0.55;
    const float SHAPE_TRAP_BOTTOM_WIDTH_MAX = 1.00;
    const uint SHAPE_TRIANGLE_VARIANT_COUNT = 3u;
    db_snake_shape_profile_t profile;
    uint seed_base = db_mix_u32(pattern_seed ^ (shape_index * 0xA511E9B3u));
    profile.circle_radius_x = db_float_range_from_u32(db_mix_u32(seed_base ^ 0x7FEB352Du), SHAPE_CIRCLE_RADIUS_MIN, SHAPE_CIRCLE_RADIUS_MAX);
    profile.circle_radius_y = db_float_range_from_u32(db_mix_u32(seed_base ^ 0x846CA68Bu), SHAPE_CIRCLE_RADIUS_MIN, SHAPE_CIRCLE_RADIUS_MAX);
    profile.diamond_radius = db_float_range_from_u32(db_mix_u32(seed_base ^ 0x6C8E9CF5u), SHAPE_DIAMOND_RADIUS_MIN, SHAPE_DIAMOND_RADIUS_MAX);
    profile.triangle_bottom_width = db_float_range_from_u32(db_mix_u32(seed_base ^ 0x1B56C4E9u), SHAPE_TRI_BOTTOM_WIDTH_MIN, SHAPE_TRI_BOTTOM_WIDTH_MAX);
    profile.trapezoid_top_width = db_float_range_from_u32(db_mix_u32(seed_base ^ 0xC2B2AE35u), SHAPE_TRAP_TOP_WIDTH_MIN, SHAPE_TRAP_TOP_WIDTH_MAX);
    profile.trapezoid_bottom_width = db_float_range_from_u32(db_mix_u32(seed_base ^ 0x27D4EB2Fu), SHAPE_TRAP_BOTTOM_WIDTH_MIN, SHAPE_TRAP_BOTTOM_WIDTH_MAX);
    profile.rect_half_width = db_float_range_from_u32(db_mix_u32(seed_base ^ 0x9E3779B9u), SHAPE_RECT_HALF_WIDTH_MIN, SHAPE_RECT_HALF_WIDTH_MAX);
    profile.rect_half_height = db_float_range_from_u32(db_mix_u32(seed_base ^ 0xCF1BBCDDu), SHAPE_RECT_HALF_HEIGHT_MIN, SHAPE_RECT_HALF_HEIGHT_MAX);
    uint rotate_seed = db_mix_u32(seed_base ^ 0xCA5A826Bu);
    profile.rotate_enabled = ((rotate_seed & 3u) == 0u) ? 1u : 0u;
    if(profile.rotate_enabled != 0u) {
        float angle_unit = db_unit_float_from_u32(db_mix_u32(seed_base ^ 0xC6BC2796u));
        float angle = angle_unit * SHAPE_ROTATION_FULL_TURN_RAD;
        profile.rotate_cos = cos(angle);
        profile.rotate_sin = sin(angle);
    } else {
        profile.rotate_cos = 1.0;
        profile.rotate_sin = 0.0;
    }
    profile.triangle_variant = db_mix_u32(seed_base ^ 0xB5297A4Du) % SHAPE_TRIANGLE_VARIANT_COUNT;
    if(profile.trapezoid_top_width > profile.trapezoid_bottom_width) {
        float tmp = profile.trapezoid_top_width;
        profile.trapezoid_top_width = profile.trapezoid_bottom_width;
        profile.trapezoid_bottom_width = tmp;
    }
    float base_half_width = profile.rect_half_width;
    float base_half_height = profile.rect_half_height;
    if(shape_kind == SHAPE_KIND_CIRCLE) {
        base_half_width = profile.circle_radius_x;
        base_half_height = profile.circle_radius_y;
    } else if(shape_kind == SHAPE_KIND_DIAMOND) {
        base_half_width = profile.diamond_radius;
        base_half_height = profile.diamond_radius;
    } else if(shape_kind == SHAPE_KIND_TRIANGLE) {
        base_half_width = profile.triangle_bottom_width * SHAPE_CENTER;
        base_half_height = SHAPE_CENTER;
    } else if(shape_kind == SHAPE_KIND_TRAPEZOID) {
        float max_width = max(profile.trapezoid_top_width, profile.trapezoid_bottom_width);
        base_half_width = max_width * SHAPE_CENTER;
        base_half_height = SHAPE_CENTER;
    }
    float abs_cos = abs(profile.rotate_cos);
    float abs_sin = abs(profile.rotate_sin);
    float max_allowed = SHAPE_CENTER - SHAPE_EDGE_INSET;
    float max_allowed_effective = max(max_allowed - SHAPE_EXTENT_SAFETY_MARGIN, 0.01);
    float extent_coeff_x = (abs_cos * base_half_width) + (abs_sin * base_half_height);
    float extent_coeff_y = (abs_sin * base_half_width) + (abs_cos * base_half_height);
    float max_extent_coeff = max(extent_coeff_x, extent_coeff_y);
    float safe_extent_max = SHAPE_EXTENT_MAX;
    if(max_extent_coeff > 0.0) {
        safe_extent_max = min(safe_extent_max, max_allowed_effective / max_extent_coeff);
    }
    safe_extent_max = max(safe_extent_max, 0.01);
    float safe_extent_min = min(SHAPE_EXTENT_MIN, safe_extent_max);
    float extent_x = db_float_range_from_u32(db_mix_u32(seed_base ^ 0xD1B54A35u), safe_extent_min, safe_extent_max);
    float extent_y = db_float_range_from_u32(db_mix_u32(seed_base ^ 0x94D049BBu), safe_extent_min, safe_extent_max);
    float bound_x = (abs_cos * base_half_width * extent_x) +
        (abs_sin * base_half_height * extent_y);
    float bound_y = (abs_sin * base_half_width * extent_x) +
        (abs_cos * base_half_height * extent_y);
    float max_bound = max(bound_x, bound_y);
    if((max_bound > max_allowed) && (max_bound > 0.0)) {
        float scale = max_allowed / max_bound;
        extent_x *= scale;
        extent_y *= scale;
    }
    profile.extent_x = extent_x;
    profile.extent_y = extent_y;
    return profile;
}

db_snake_shape_desc_t db_snake_shape_desc(
    uint pattern_seed,
    uint shape_index,
    uint rows_u,
    uint cols_u,
    uint shape_kind
) {
    db_snake_shape_desc_t shape_desc;
    shape_desc.region = db_snake_region_desc(pattern_seed, shape_index, rows_u, cols_u);
    shape_desc.profile = db_snake_shape_profile_from_index(pattern_seed, shape_index, shape_kind);
    shape_desc.kind = shape_kind;
    return shape_desc;
}

bool db_shape_contains(
    db_snake_shape_desc_t shape_desc,
    uint row_u,
    uint col_u
) {
    const float SHAPE_CENTER = 0.5;
    const float SHAPE_EDGE_INSET = 0.05;
    if(!db_snake_region_contains(shape_desc.region, row_u, col_u)) {
        return false;
    }
    float fx = (float((col_u - shape_desc.region.x)) + SHAPE_CENTER) /
        float(max(shape_desc.region.width, 1u));
    float fy = (float((row_u - shape_desc.region.y)) + SHAPE_CENTER) /
        float(max(shape_desc.region.height, 1u));
    float dx = fx - SHAPE_CENTER;
    float dy = fy - SHAPE_CENTER;
    db_snake_shape_profile_t profile = shape_desc.profile;
    float rot_x = (dx * profile.rotate_cos) - (dy * profile.rotate_sin);
    float rot_y = (dx * profile.rotate_sin) + (dy * profile.rotate_cos);
    float norm_x = rot_x / profile.extent_x;
    float norm_y = rot_y / profile.extent_y;
    float norm_fx = norm_x + SHAPE_CENTER;
    float norm_fy = norm_y + SHAPE_CENTER;
    if(shape_desc.kind == SHAPE_KIND_RECT) {
        return (abs(norm_x) <= profile.rect_half_width) &&
            (abs(norm_y) <= profile.rect_half_height);
    }
    if(shape_desc.kind == SHAPE_KIND_CIRCLE) {
        float inv_rx = 1.0 / max(profile.circle_radius_x, 0.01);
        float inv_ry = 1.0 / max(profile.circle_radius_y, 0.01);
        float cx = norm_x * inv_rx;
        float cy = norm_y * inv_ry;
        return ((cx * cx) + (cy * cy)) <= 1.0;
    }
    if(shape_desc.kind == SHAPE_KIND_DIAMOND) {
        return (abs(norm_x) + abs(norm_y)) <= profile.diamond_radius;
    }
    if(shape_desc.kind == SHAPE_KIND_TRIANGLE) {
        float width = profile.triangle_bottom_width * norm_fy;
        float half_width = width * 0.5;
        if(profile.triangle_variant == 1u) {
            float left = -SHAPE_CENTER + SHAPE_EDGE_INSET;
            float right = left + width;
            return (norm_x >= left) && (norm_x <= right) && (norm_fy >= 0.0) &&
                (norm_fy <= 1.0);
        }
        if(profile.triangle_variant == 2u) {
            float right = SHAPE_CENTER - SHAPE_EDGE_INSET;
            float left = right - width;
            return (norm_x >= left) && (norm_x <= right) && (norm_fy >= 0.0) &&
                (norm_fy <= 1.0);
        }
        float left = SHAPE_CENTER - half_width;
        float right = SHAPE_CENTER + half_width;
        return (norm_fx >= left) && (norm_fx <= right) && (norm_fy >= 0.0) &&
            (norm_fy <= 1.0);
    }
    float width = profile.trapezoid_top_width +
        ((profile.trapezoid_bottom_width - profile.trapezoid_top_width) *
        norm_fy);
    float half_width = width * 0.5;
    float left = SHAPE_CENTER - half_width;
    float right = SHAPE_CENTER + half_width;
    return (norm_fx >= left) && (norm_fx <= right) && (norm_fy >= 0.0) &&
        (norm_fy <= 1.0);
}

db_snake_region_desc_t db_full_grid_region(uint rows_u, uint cols_u) {
    db_snake_region_desc_t region;
    region.x = 0u;
    region.y = 0u;
    region.width = cols_u;
    region.height = rows_u;
    region.color = vec3(0.0);
    return region;
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
    db_snake_shape_desc_t shape_desc,
    bool apply_shape_clip,
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
    if(apply_shape_clip) {
        if(!db_shape_contains(shape_desc, row_u, col_u)) {
            return db_rgba(prior_color);
        }
    } else if(!db_snake_region_contains(shape_desc.region, row_u, col_u)) {
        return db_rgba(prior_color);
    }
    uint step = db_snake_region_step(shape_desc.region, row_u, col_u);
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
    const uint RENDER_MODE_SNAKE_RECT = 4u;
    const uint RENDER_MODE_SNAKE_SHAPES = 5u;
    if(u_render_mode == RENDER_MODE_BANDS) {
        out_color = db_rgba(db_band_color(db_band_index_from_frag_coord_x(), max(u_band_count, 1u), u_time_s));
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
        (u_render_mode != RENDER_MODE_SNAKE_RECT) &&
        (u_render_mode != RENDER_MODE_SNAKE_SHAPES)) {
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
    if((u_render_mode == RENDER_MODE_SNAKE_RECT) ||
        (u_render_mode == RENDER_MODE_SNAKE_SHAPES)) {
        uint shape_kind = db_snake_shapes_kind(u_pattern_seed, u_snake_shape_index);
        db_snake_shape_desc_t shape_desc = db_snake_shape_desc(u_pattern_seed, u_snake_shape_index, rows_u, cols_u, shape_kind);
        out_color = db_snake_color(shape_desc, (u_render_mode == RENDER_MODE_SNAKE_SHAPES), row_u, col_u, prior_color, shape_desc.region.color, u_snake_cursor, batch_size_u);
    } else {
        db_snake_shape_desc_t shape_desc;
        shape_desc.region = db_full_grid_region(rows_u, cols_u);
        shape_desc.profile = db_snake_shape_profile_from_index(u_pattern_seed, 0u, SHAPE_KIND_RECT);
        shape_desc.kind = SHAPE_KIND_RECT;
        bool clearing = (u_mode_phase_flag != 0);
        vec3 target_color = db_target_color_for_phase(clearing);
        out_color = db_snake_color(shape_desc, false, row_u, col_u, prior_color, target_color, u_snake_cursor, batch_size_u);
    }
}
