#ifndef DRIVERBENCH_RENDERER_SNAKE_SHAPE_COMMON_H
#define DRIVERBENCH_RENDERER_SNAKE_SHAPE_COMMON_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "../core/db_core.h"
#include "../core/db_hash.h"

#define DB_SNAKE_SHAPE_CENTER_F 0.5F
#define DB_SNAKE_SHAPE_CIRCLE_RADIUS_MAX_F 0.50F
#define DB_SNAKE_SHAPE_CIRCLE_RADIUS_MIN_F 0.30F
#define DB_SNAKE_SHAPE_DIAMOND_RADIUS_MAX_F 0.60F
#define DB_SNAKE_SHAPE_DIAMOND_RADIUS_MIN_F 0.40F
#define DB_SNAKE_SHAPE_EDGE_INSET_F 0.05F
#define DB_SNAKE_SHAPE_EXTENT_MAX_F 0.98F
#define DB_SNAKE_SHAPE_EXTENT_MIN_F 0.82F
#define DB_SNAKE_SHAPE_EXTENT_EPSILON_F 0.01F
#define DB_SNAKE_SHAPE_EXTENT_SAFETY_MARGIN_F 0.000001F
#define DB_SNAKE_SHAPE_INTERSECT_EPSILON_F 0.000001F
#define DB_SNAKE_SHAPE_INTERSECT_EPSILON_D 0.000000001
#define DB_SNAKE_SHAPE_HALF_F 0.5F
#define DB_SNAKE_SHAPE_RECT_HALF_HEIGHT_MAX_F 0.50F
#define DB_SNAKE_SHAPE_RECT_HALF_HEIGHT_MIN_F 0.30F
#define DB_SNAKE_SHAPE_RECT_HALF_WIDTH_MAX_F 0.50F
#define DB_SNAKE_SHAPE_RECT_HALF_WIDTH_MIN_F 0.30F
#define DB_SNAKE_SHAPE_ROTATION_FULL_TURN_RAD_F 6.2831853F
#define DB_SNAKE_SHAPE_SALT_CIRCLE_RX 0x7FEB352DU
#define DB_SNAKE_SHAPE_SALT_CIRCLE_RY 0x846CA68BU
#define DB_SNAKE_SHAPE_SALT_DIAMOND_RADIUS 0x6C8E9CF5U
#define DB_SNAKE_SHAPE_SALT_EXTENT_X 0xD1B54A35U
#define DB_SNAKE_SHAPE_SALT_EXTENT_Y 0x94D049BBU
#define DB_SNAKE_SHAPE_SALT_RECT_HALF_HEIGHT 0xCF1BBCDDU
#define DB_SNAKE_SHAPE_SALT_RECT_HALF_WIDTH 0x9E3779B9U
#define DB_SNAKE_SHAPE_SALT_ROTATE_ENABLE 0xCA5A826BU
#define DB_SNAKE_SHAPE_SALT_ROTATION 0xC6BC2796U
#define DB_SNAKE_SHAPE_SALT_TRAP_BOTTOM_WIDTH 0x27D4EB2FU
#define DB_SNAKE_SHAPE_SALT_TRAP_TOP_WIDTH 0xC2B2AE35U
#define DB_SNAKE_SHAPE_SALT_TRI_BOTTOM_WIDTH 0x1B56C4E9U
#define DB_SNAKE_SHAPE_SALT_TRI_VARIANT 0xB5297A4DU
#define DB_SNAKE_SHAPE_TRAP_BOTTOM_WIDTH_MAX_F 1.00F
#define DB_SNAKE_SHAPE_TRAP_BOTTOM_WIDTH_MIN_F 0.55F
#define DB_SNAKE_SHAPE_TRAP_TOP_WIDTH_MAX_F 0.75F
#define DB_SNAKE_SHAPE_TRAP_TOP_WIDTH_MIN_F 0.20F
#define DB_SNAKE_SHAPE_TRI_BOTTOM_WIDTH_MAX_F 0.90F
#define DB_SNAKE_SHAPE_TRI_BOTTOM_WIDTH_MIN_F 0.70F
#define DB_SNAKE_SHAPE_TRIANGLE_VARIANT_COUNT 3U

typedef enum {
    DB_SNAKE_SHAPE_RECT = 0,
    DB_SNAKE_SHAPE_CIRCLE = 1,
    DB_SNAKE_SHAPE_DIAMOND = 2,
    DB_SNAKE_SHAPE_TRIANGLE = 3,
    DB_SNAKE_SHAPE_TRAPEZOID = 4,
    DB_SNAKE_SHAPE_COUNT = 5,
} db_snake_shape_kind_t;

typedef struct {
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
    uint32_t rotate_enabled;
    uint32_t triangle_variant;
} db_snake_shape_profile_t;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    float color_r;
    float color_g;
    float color_b;
} db_snake_region_t;

typedef struct {
    db_snake_region_t region;
    db_snake_shape_kind_t shape_kind;
    db_snake_shape_profile_t shape_profile;
} db_snake_shape_desc_t;

typedef struct {
    uint32_t col_start;
    uint32_t col_end;
    int has_coverage;
} db_snake_shape_row_bounds_t;

typedef struct {
    db_snake_shape_desc_t desc;
    db_snake_shape_row_bounds_t *row_bounds;
    size_t row_bounds_count;
    size_t row_bounds_capacity;
} db_snake_shape_cache_t;

static inline db_snake_shape_kind_t
db_snake_shapes_kind_from_index(uint32_t seed, uint32_t shape_index,
                                uint32_t shape_salt) {
    const uint32_t mixed = db_mix_u32(seed ^ (shape_index * shape_salt));
    const uint32_t kind = mixed % DB_SNAKE_SHAPE_COUNT;
    return (db_snake_shape_kind_t)kind;
}

static inline db_snake_shape_profile_t
db_snake_shape_profile_from_index(uint32_t pattern_seed, uint32_t shape_index,
                                  uint32_t shape_salt,
                                  db_snake_shape_kind_t shape_kind) {
    db_snake_shape_profile_t profile = {0};
    const uint32_t seed_base =
        db_mix_u32(pattern_seed ^ (shape_index * shape_salt));
    profile.circle_radius_x = db_u32_to_range_f32(
        db_mix_u32(seed_base ^ DB_SNAKE_SHAPE_SALT_CIRCLE_RX),
        DB_SNAKE_SHAPE_CIRCLE_RADIUS_MIN_F, DB_SNAKE_SHAPE_CIRCLE_RADIUS_MAX_F);
    profile.circle_radius_y = db_u32_to_range_f32(
        db_mix_u32(seed_base ^ DB_SNAKE_SHAPE_SALT_CIRCLE_RY),
        DB_SNAKE_SHAPE_CIRCLE_RADIUS_MIN_F, DB_SNAKE_SHAPE_CIRCLE_RADIUS_MAX_F);
    profile.diamond_radius = db_u32_to_range_f32(
        db_mix_u32(seed_base ^ DB_SNAKE_SHAPE_SALT_DIAMOND_RADIUS),
        DB_SNAKE_SHAPE_DIAMOND_RADIUS_MIN_F,
        DB_SNAKE_SHAPE_DIAMOND_RADIUS_MAX_F);
    profile.triangle_bottom_width = db_u32_to_range_f32(
        db_mix_u32(seed_base ^ DB_SNAKE_SHAPE_SALT_TRI_BOTTOM_WIDTH),
        DB_SNAKE_SHAPE_TRI_BOTTOM_WIDTH_MIN_F,
        DB_SNAKE_SHAPE_TRI_BOTTOM_WIDTH_MAX_F);
    profile.trapezoid_top_width = db_u32_to_range_f32(
        db_mix_u32(seed_base ^ DB_SNAKE_SHAPE_SALT_TRAP_TOP_WIDTH),
        DB_SNAKE_SHAPE_TRAP_TOP_WIDTH_MIN_F,
        DB_SNAKE_SHAPE_TRAP_TOP_WIDTH_MAX_F);
    profile.trapezoid_bottom_width = db_u32_to_range_f32(
        db_mix_u32(seed_base ^ DB_SNAKE_SHAPE_SALT_TRAP_BOTTOM_WIDTH),
        DB_SNAKE_SHAPE_TRAP_BOTTOM_WIDTH_MIN_F,
        DB_SNAKE_SHAPE_TRAP_BOTTOM_WIDTH_MAX_F);
    profile.rect_half_width = db_u32_to_range_f32(
        db_mix_u32(seed_base ^ DB_SNAKE_SHAPE_SALT_RECT_HALF_WIDTH),
        DB_SNAKE_SHAPE_RECT_HALF_WIDTH_MIN_F,
        DB_SNAKE_SHAPE_RECT_HALF_WIDTH_MAX_F);
    profile.rect_half_height = db_u32_to_range_f32(
        db_mix_u32(seed_base ^ DB_SNAKE_SHAPE_SALT_RECT_HALF_HEIGHT),
        DB_SNAKE_SHAPE_RECT_HALF_HEIGHT_MIN_F,
        DB_SNAKE_SHAPE_RECT_HALF_HEIGHT_MAX_F);
    const uint32_t rotate_seed =
        db_mix_u32(seed_base ^ DB_SNAKE_SHAPE_SALT_ROTATE_ENABLE);
    profile.rotate_enabled = ((rotate_seed & 3U) == 0U) ? 1U : 0U;
    if (profile.rotate_enabled != 0U) {
        const float angle_unit = db_u32_to_unit_f32(
            db_mix_u32(seed_base ^ DB_SNAKE_SHAPE_SALT_ROTATION));
        const float angle =
            angle_unit * DB_SNAKE_SHAPE_ROTATION_FULL_TURN_RAD_F;
        profile.rotate_cos = cosf(angle);
        profile.rotate_sin = sinf(angle);
    } else {
        profile.rotate_cos = 1.0F;
        profile.rotate_sin = 0.0F;
    }
    profile.triangle_variant =
        db_mix_u32(seed_base ^ DB_SNAKE_SHAPE_SALT_TRI_VARIANT) %
        DB_SNAKE_SHAPE_TRIANGLE_VARIANT_COUNT;
    if (profile.trapezoid_top_width > profile.trapezoid_bottom_width) {
        const float tmp = profile.trapezoid_top_width;
        profile.trapezoid_top_width = profile.trapezoid_bottom_width;
        profile.trapezoid_bottom_width = tmp;
    }
    float base_half_width = profile.rect_half_width;
    float base_half_height = profile.rect_half_height;
    if (shape_kind == DB_SNAKE_SHAPE_CIRCLE) {
        base_half_width = profile.circle_radius_x;
        base_half_height = profile.circle_radius_y;
    } else if (shape_kind == DB_SNAKE_SHAPE_DIAMOND) {
        base_half_width = profile.diamond_radius;
        base_half_height = profile.diamond_radius;
    } else if (shape_kind == DB_SNAKE_SHAPE_TRIANGLE) {
        base_half_width =
            profile.triangle_bottom_width * DB_SNAKE_SHAPE_CENTER_F;
        base_half_height = DB_SNAKE_SHAPE_CENTER_F;
    } else if (shape_kind == DB_SNAKE_SHAPE_TRAPEZOID) {
        const float max_width =
            fmaxf(profile.trapezoid_top_width, profile.trapezoid_bottom_width);
        base_half_width = max_width * DB_SNAKE_SHAPE_CENTER_F;
        base_half_height = DB_SNAKE_SHAPE_CENTER_F;
    }
    const float abs_cos = fabsf(profile.rotate_cos);
    const float abs_sin = fabsf(profile.rotate_sin);
    const float max_allowed =
        DB_SNAKE_SHAPE_CENTER_F - DB_SNAKE_SHAPE_EDGE_INSET_F;
    const float max_allowed_effective =
        fmaxf(max_allowed - DB_SNAKE_SHAPE_EXTENT_SAFETY_MARGIN_F,
              DB_SNAKE_SHAPE_EXTENT_EPSILON_F);
    const float extent_coeff_x =
        (abs_cos * base_half_width) + (abs_sin * base_half_height);
    const float extent_coeff_y =
        (abs_sin * base_half_width) + (abs_cos * base_half_height);
    const float max_extent_coeff = fmaxf(extent_coeff_x, extent_coeff_y);
    float safe_extent_max = DB_SNAKE_SHAPE_EXTENT_MAX_F;
    if (max_extent_coeff > 0.0F) {
        safe_extent_max =
            fminf(safe_extent_max, max_allowed_effective / max_extent_coeff);
    }
    safe_extent_max = fmaxf(safe_extent_max, DB_SNAKE_SHAPE_EXTENT_EPSILON_F);
    const float safe_extent_min =
        fminf(DB_SNAKE_SHAPE_EXTENT_MIN_F, safe_extent_max);
    float extent_x = db_u32_to_range_f32(
        db_mix_u32(seed_base ^ DB_SNAKE_SHAPE_SALT_EXTENT_X), safe_extent_min,
        safe_extent_max);
    float extent_y = db_u32_to_range_f32(
        db_mix_u32(seed_base ^ DB_SNAKE_SHAPE_SALT_EXTENT_Y), safe_extent_min,
        safe_extent_max);
    const float bound_x = (abs_cos * base_half_width * extent_x) +
                          (abs_sin * base_half_height * extent_y);
    const float bound_y = (abs_sin * base_half_width * extent_x) +
                          (abs_cos * base_half_height * extent_y);
    const float max_bound = fmaxf(bound_x, bound_y);
    if ((max_bound > max_allowed) && (max_bound > 0.0F)) {
        const float scale = max_allowed / max_bound;
        extent_x *= scale;
        extent_y *= scale;
    }
    profile.extent_x = extent_x;
    profile.extent_y = extent_y;
    return profile;
}

static inline db_snake_shape_desc_t db_snake_shape_desc_from_index(
    uint32_t pattern_seed, uint32_t shape_index, uint32_t shape_salt,
    const db_snake_region_t *region, db_snake_shape_kind_t shape_kind) {
    db_snake_shape_desc_t shape_desc = {0};
    if (region != NULL) {
        shape_desc.region = *region;
    }
    shape_desc.shape_kind = shape_kind;
    shape_desc.shape_profile = db_snake_shape_profile_from_index(
        pattern_seed, shape_index, shape_salt, shape_kind);
    return shape_desc;
}

static inline void db_snake_shape_transform_norm_vertex_to_local(
    const db_snake_shape_profile_t *profile, float norm_x, float norm_y,
    float *out_local_x, float *out_local_y) {
    const float scaled_x = norm_x * profile->extent_x;
    const float scaled_y = norm_y * profile->extent_y;
    const float local_x =
        (scaled_x * profile->rotate_cos) + (scaled_y * profile->rotate_sin);
    const float local_y =
        (-scaled_x * profile->rotate_sin) + (scaled_y * profile->rotate_cos);
    *out_local_x = local_x + DB_SNAKE_SHAPE_CENTER_F;
    *out_local_y = local_y + DB_SNAKE_SHAPE_CENTER_F;
}

static inline int db_snake_shape_polygon_row_interval_local(
    const float *verts_x, const float *verts_y, size_t vert_count, float row_y,
    float *out_min_x, float *out_max_x) {
    if ((verts_x == NULL) || (verts_y == NULL) || (vert_count < 3U) ||
        (out_min_x == NULL) || (out_max_x == NULL)) {
        return 0;
    }
    int has_intersection = 0;
    float min_x = 0.0F;
    float max_x = 0.0F;
    for (size_t edge_index = 0U; edge_index < vert_count; edge_index++) {
        const size_t next_index = (edge_index + 1U) % vert_count;
        const float x0 = verts_x[edge_index];
        const float y0 = verts_y[edge_index];
        const float x1 = verts_x[next_index];
        const float y1 = verts_y[next_index];
        const float edge_dy = y1 - y0;
        if (fabsf(edge_dy) <= DB_SNAKE_SHAPE_INTERSECT_EPSILON_F) {
            if (fabsf(row_y - y0) <= DB_SNAKE_SHAPE_INTERSECT_EPSILON_F) {
                if (has_intersection == 0) {
                    min_x = fminf(x0, x1);
                    max_x = fmaxf(x0, x1);
                    has_intersection = 1;
                } else {
                    min_x = fminf(min_x, fminf(x0, x1));
                    max_x = fmaxf(max_x, fmaxf(x0, x1));
                }
            }
            continue;
        }
        const float min_y = fminf(y0, y1) - DB_SNAKE_SHAPE_INTERSECT_EPSILON_F;
        const float max_y = fmaxf(y0, y1) + DB_SNAKE_SHAPE_INTERSECT_EPSILON_F;
        if ((row_y < min_y) || (row_y > max_y)) {
            continue;
        }
        const float edge_t = (row_y - y0) / edge_dy;
        if ((edge_t < -DB_SNAKE_SHAPE_INTERSECT_EPSILON_F) ||
            (edge_t > (1.0F + DB_SNAKE_SHAPE_INTERSECT_EPSILON_F))) {
            continue;
        }
        const float x_at_row = x0 + ((x1 - x0) * edge_t);
        if (has_intersection == 0) {
            min_x = x_at_row;
            max_x = x_at_row;
            has_intersection = 1;
        } else {
            min_x = fminf(min_x, x_at_row);
            max_x = fmaxf(max_x, x_at_row);
        }
    }
    if (has_intersection == 0) {
        return 0;
    }
    *out_min_x = min_x;
    *out_max_x = max_x;
    return 1;
}

static inline int db_snake_shape_circle_row_interval_local(
    const db_snake_shape_profile_t *profile, float row_y, float *out_min_x,
    float *out_max_x) {
    if ((profile == NULL) || (out_min_x == NULL) || (out_max_x == NULL)) {
        return 0;
    }
    const float dy = row_y - DB_SNAKE_SHAPE_CENTER_F;
    const float extent_x = fmaxf(profile->extent_x, 0.000001F);
    const float extent_y = fmaxf(profile->extent_y, 0.000001F);
    const float radius_x = fmaxf(profile->circle_radius_x, 0.01F);
    const float radius_y = fmaxf(profile->circle_radius_y, 0.01F);
    const float coeff_x = profile->rotate_cos / (extent_x * radius_x);
    const float coeff_y = profile->rotate_sin / (extent_y * radius_y);
    const float term0 = (-profile->rotate_sin * dy) / (extent_x * radius_x);
    const float term1 = (profile->rotate_cos * dy) / (extent_y * radius_y);
    const float quad_a = (coeff_x * coeff_x) + (coeff_y * coeff_y);
    if (quad_a <= 0.0F) {
        return 0;
    }
    const float quad_b = 2.0F * ((coeff_x * term0) + (coeff_y * term1));
    const float quad_c = (term0 * term0) + (term1 * term1) - 1.0F;
    const float disc = (quad_b * quad_b) - (4.0F * quad_a * quad_c);
    if (disc < -DB_SNAKE_SHAPE_INTERSECT_EPSILON_F) {
        return 0;
    }
    const float disc_clamped = fmaxf(disc, 0.0F);
    const float sqrt_disc = sqrtf(disc_clamped);
    const float inv_denom = 0.5F / quad_a;
    const float dx0 = (-quad_b - sqrt_disc) * inv_denom;
    const float dx1 = (-quad_b + sqrt_disc) * inv_denom;
    *out_min_x = DB_SNAKE_SHAPE_CENTER_F + fminf(dx0, dx1);
    *out_max_x = DB_SNAKE_SHAPE_CENTER_F + fmaxf(dx0, dx1);
    return 1;
}

static inline int db_snake_shape_local_interval_to_col_bounds(
    float interval_min_x, float interval_max_x, uint32_t width,
    uint32_t *out_col_start, uint32_t *out_col_end) {
    if ((out_col_start == NULL) || (out_col_end == NULL) || (width == 0U)) {
        return 0;
    }
    float min_x = fmaxf(fminf(interval_min_x, interval_max_x), 0.0F);
    float max_x = fminf(fmaxf(interval_min_x, interval_max_x), 1.0F);
    if (max_x < min_x) {
        return 0;
    }
    const double col_min_d = ceil(((double)min_x * (double)width) - 0.5 -
                                  DB_SNAKE_SHAPE_INTERSECT_EPSILON_D);
    const double col_max_d = floor(((double)max_x * (double)width) - 0.5 +
                                   DB_SNAKE_SHAPE_INTERSECT_EPSILON_D);
    int64_t col_min = (int64_t)col_min_d;
    int64_t col_max = (int64_t)col_max_d;
    if (col_max < col_min) {
        return 0;
    }
    if (col_min < 0) {
        col_min = 0;
    }
    if (col_max >= (int64_t)width) {
        col_max = (int64_t)width - 1;
    }
    if (col_max < col_min) {
        return 0;
    }
    *out_col_start = (uint32_t)col_min;
    *out_col_end = (uint32_t)(col_max + 1);
    return (*out_col_end > *out_col_start) ? 1 : 0;
}

static inline size_t db_snake_shape_build_exact_row_bounds(
    const db_snake_shape_desc_t *shape_desc,
    db_snake_shape_row_bounds_t *row_bounds_cache, size_t cache_capacity) {
    if ((shape_desc == NULL) || (row_bounds_cache == NULL) ||
        (cache_capacity == 0U)) {
        return 0U;
    }
    const db_snake_region_t *region = &shape_desc->region;
    const size_t row_count =
        (size_t)db_u32_min(region->height, (uint32_t)cache_capacity);
    for (size_t i = 0U; i < row_count; i++) {
        row_bounds_cache[i] = (db_snake_shape_row_bounds_t){
            .col_start = 0U,
            .col_end = 0U,
            .has_coverage = 0,
        };
        const uint32_t local_row = (uint32_t)i;
        const uint32_t width = region->width;
        if ((width == 0U) || (region->height == 0U)) {
            continue;
        }
        const float row_center_y =
            ((float)local_row + DB_SNAKE_SHAPE_CENTER_F) /
            (float)region->height;
        float interval_min_x = 0.0F;
        float interval_max_x = 0.0F;
        int has_interval = 0;
        const db_snake_shape_profile_t *profile = &shape_desc->shape_profile;
        if (shape_desc->shape_kind == DB_SNAKE_SHAPE_CIRCLE) {
            has_interval = db_snake_shape_circle_row_interval_local(
                profile, row_center_y, &interval_min_x, &interval_max_x);
        } else {
            float verts_x[4] = {0.0F, 0.0F, 0.0F, 0.0F};
            float verts_y[4] = {0.0F, 0.0F, 0.0F, 0.0F};
            size_t vert_count = 0U;
            if (shape_desc->shape_kind == DB_SNAKE_SHAPE_RECT) {
                const float x0 = -profile->rect_half_width;
                const float x1 = profile->rect_half_width;
                const float y0 = -profile->rect_half_height;
                const float y1 = profile->rect_half_height;
                const float nx[4] = {x0, x1, x1, x0};
                const float ny[4] = {y0, y0, y1, y1};
                vert_count = 4U;
                for (size_t idx = 0U; idx < vert_count; idx++) {
                    db_snake_shape_transform_norm_vertex_to_local(
                        profile, nx[idx], ny[idx], &verts_x[idx],
                        &verts_y[idx]);
                }
            } else if (shape_desc->shape_kind == DB_SNAKE_SHAPE_DIAMOND) {
                const float radius = profile->diamond_radius;
                const float nx[4] = {0.0F, radius, 0.0F, -radius};
                const float ny[4] = {-radius, 0.0F, radius, 0.0F};
                vert_count = 4U;
                for (size_t idx = 0U; idx < vert_count; idx++) {
                    db_snake_shape_transform_norm_vertex_to_local(
                        profile, nx[idx], ny[idx], &verts_x[idx],
                        &verts_y[idx]);
                }
            } else if (shape_desc->shape_kind == DB_SNAKE_SHAPE_TRIANGLE) {
                float left_top = 0.0F;
                float right_top = 0.0F;
                float left_bottom = 0.0F;
                float right_bottom = 0.0F;
                if (profile->triangle_variant == 1U) {
                    left_top =
                        -DB_SNAKE_SHAPE_CENTER_F + DB_SNAKE_SHAPE_EDGE_INSET_F;
                    right_top = left_top;
                    left_bottom = left_top;
                    right_bottom = left_top + profile->triangle_bottom_width;
                } else if (profile->triangle_variant == 2U) {
                    right_top =
                        DB_SNAKE_SHAPE_CENTER_F - DB_SNAKE_SHAPE_EDGE_INSET_F;
                    left_top = right_top;
                    right_bottom = right_top;
                    left_bottom = right_top - profile->triangle_bottom_width;
                } else {
                    left_top = 0.0F;
                    right_top = 0.0F;
                    left_bottom =
                        -profile->triangle_bottom_width * DB_SNAKE_SHAPE_HALF_F;
                    right_bottom =
                        profile->triangle_bottom_width * DB_SNAKE_SHAPE_HALF_F;
                }
                const float nx[4] = {left_top, right_top, right_bottom,
                                     left_bottom};
                const float ny[4] = {
                    -DB_SNAKE_SHAPE_CENTER_F, -DB_SNAKE_SHAPE_CENTER_F,
                    DB_SNAKE_SHAPE_CENTER_F, DB_SNAKE_SHAPE_CENTER_F};
                vert_count = 4U;
                for (size_t idx = 0U; idx < vert_count; idx++) {
                    db_snake_shape_transform_norm_vertex_to_local(
                        profile, nx[idx], ny[idx], &verts_x[idx],
                        &verts_y[idx]);
                }
            } else if (shape_desc->shape_kind == DB_SNAKE_SHAPE_TRAPEZOID) {
                const float top_half =
                    profile->trapezoid_top_width * DB_SNAKE_SHAPE_HALF_F;
                const float bottom_half =
                    profile->trapezoid_bottom_width * DB_SNAKE_SHAPE_HALF_F;
                const float nx[4] = {-top_half, top_half, bottom_half,
                                     -bottom_half};
                const float ny[4] = {
                    -DB_SNAKE_SHAPE_CENTER_F, -DB_SNAKE_SHAPE_CENTER_F,
                    DB_SNAKE_SHAPE_CENTER_F, DB_SNAKE_SHAPE_CENTER_F};
                vert_count = 4U;
                for (size_t idx = 0U; idx < vert_count; idx++) {
                    db_snake_shape_transform_norm_vertex_to_local(
                        profile, nx[idx], ny[idx], &verts_x[idx],
                        &verts_y[idx]);
                }
            }
            if (vert_count > 0U) {
                has_interval = db_snake_shape_polygon_row_interval_local(
                    verts_x, verts_y, vert_count, row_center_y, &interval_min_x,
                    &interval_max_x);
            }
        }
        uint32_t col_start = 0U;
        uint32_t col_end = 0U;
        if ((has_interval != 0) && (db_snake_shape_local_interval_to_col_bounds(
                                        interval_min_x, interval_max_x, width,
                                        &col_start, &col_end) != 0)) {
            row_bounds_cache[i].col_start = col_start;
            row_bounds_cache[i].col_end = col_end;
            row_bounds_cache[i].has_coverage = 1;
        }
    }
    return row_count;
}

static inline int db_snake_shape_row_bounds_contains_tile(
    const db_snake_shape_desc_t *shape_desc,
    const db_snake_shape_row_bounds_t *row_bounds_cache,
    size_t row_bounds_count, uint32_t row, uint32_t col) {
    if ((shape_desc == NULL) || (row_bounds_cache == NULL) ||
        (row_bounds_count == 0U)) {
        return 0;
    }
    const db_snake_region_t *region = &shape_desc->region;
    if ((row < region->y) || (row >= (region->y + region->height)) ||
        (col < region->x) || (col >= (region->x + region->width))) {
        return 0;
    }
    const uint32_t local_row = row - region->y;
    if (((size_t)local_row) >= row_bounds_count) {
        return 0;
    }
    const db_snake_shape_row_bounds_t bounds = row_bounds_cache[local_row];
    if (bounds.has_coverage == 0) {
        return 0;
    }
    const uint32_t col_start = region->x + bounds.col_start;
    const uint32_t col_end = region->x + bounds.col_end;
    return ((col >= col_start) && (col < col_end)) ? 1 : 0;
}

static inline int
db_snake_shape_cache_build(db_snake_shape_cache_t *shape_cache) {
    if ((shape_cache == NULL) || (shape_cache->row_bounds == NULL) ||
        (shape_cache->row_bounds_capacity == 0U)) {
        return 0;
    }
    shape_cache->row_bounds_count = db_snake_shape_build_exact_row_bounds(
        &shape_cache->desc, shape_cache->row_bounds,
        shape_cache->row_bounds_capacity);
    return (shape_cache->row_bounds_count != 0U) ? 1 : 0;
}

static inline int db_snake_shape_cache_init_from_index(
    db_snake_shape_cache_t *shape_cache,
    db_snake_shape_row_bounds_t *row_bounds, size_t row_bounds_capacity,
    uint32_t pattern_seed, uint32_t shape_index, uint32_t shape_salt,
    const db_snake_region_t *region, db_snake_shape_kind_t shape_kind) {
    if ((shape_cache == NULL) || (row_bounds == NULL) || (region == NULL) ||
        (row_bounds_capacity == 0U)) {
        return 0;
    }
    *shape_cache = (db_snake_shape_cache_t){
        .desc = db_snake_shape_desc_from_index(pattern_seed, shape_index,
                                               shape_salt, region, shape_kind),
        .row_bounds = row_bounds,
        .row_bounds_count = 0U,
        .row_bounds_capacity = row_bounds_capacity,
    };
    return db_snake_shape_cache_build(shape_cache);
}

static inline int
db_snake_shape_cache_contains_tile(const db_snake_shape_cache_t *shape_cache,
                                   uint32_t row, uint32_t col) {
    if (shape_cache == NULL) {
        return 0;
    }
    return db_snake_shape_row_bounds_contains_tile(
        &shape_cache->desc, shape_cache->row_bounds,
        shape_cache->row_bounds_count, row, col);
}

static inline int
db_snake_shape_cache_get_row_span(const db_snake_shape_cache_t *shape_cache,
                                  uint32_t row, uint32_t *out_start,
                                  uint32_t *out_end) {
    if ((shape_cache == NULL) || (out_start == NULL) || (out_end == NULL)) {
        return 0;
    }
    const db_snake_region_t *region = &shape_cache->desc.region;
    if ((row < region->y) || (row >= (region->y + region->height))) {
        return 0;
    }
    const uint32_t local_row = row - region->y;
    if (((size_t)local_row) >= shape_cache->row_bounds_count) {
        return 0;
    }
    const db_snake_shape_row_bounds_t bounds =
        shape_cache->row_bounds[local_row];
    if (bounds.has_coverage == 0) {
        return 0;
    }
    *out_start = region->x + bounds.col_start;
    *out_end = region->x + bounds.col_end;
    return (*out_end > *out_start) ? 1 : 0;
}

static inline int
db_snake_shape_cache_clip_row_span(const db_snake_shape_cache_t *shape_cache,
                                   uint32_t row, uint32_t *inout_col_start,
                                   uint32_t *inout_col_end) {
    if ((shape_cache == NULL) || (inout_col_start == NULL) ||
        (inout_col_end == NULL) || (*inout_col_end <= *inout_col_start)) {
        return 0;
    }
    uint32_t shape_col_start = 0U;
    uint32_t shape_col_end = 0U;
    if (db_snake_shape_cache_get_row_span(shape_cache, row, &shape_col_start,
                                          &shape_col_end) == 0) {
        return 0;
    }
    const uint32_t clipped_start =
        db_u32_max(*inout_col_start, shape_col_start);
    const uint32_t clipped_end = db_u32_min(*inout_col_end, shape_col_end);
    if (clipped_end <= clipped_start) {
        return 0;
    }
    *inout_col_start = clipped_start;
    *inout_col_end = clipped_end;
    return 1;
}

static inline int
db_snake_shape_cache_row_has_coverage(const db_snake_shape_cache_t *shape_cache,
                                      uint32_t row) {
    uint32_t unused_start = 0U;
    uint32_t unused_end = 0U;
    return db_snake_shape_cache_get_row_span(shape_cache, row, &unused_start,
                                             &unused_end);
}

#endif
