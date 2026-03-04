#ifndef DRIVERBENCH_RENDERER_SNAKE_SHAPE_COMMON_H
#define DRIVERBENCH_RENDERER_SNAKE_SHAPE_COMMON_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "../core/db_core.h"
#include "../core/db_hash.h"
#include "../core/db_numeric.h"

#define DB_SNAKE_SHAPE_CENTER 0.5
#define DB_SNAKE_SHAPE_CIRCLE_RADIUS_MAX 0.50
#define DB_SNAKE_SHAPE_CIRCLE_RADIUS_MIN 0.30
#define DB_SNAKE_SHAPE_DIAMOND_RADIUS_MAX 0.60
#define DB_SNAKE_SHAPE_DIAMOND_RADIUS_MIN 0.40
#define DB_SNAKE_SHAPE_EDGE_INSET 0.05
#define DB_SNAKE_SHAPE_EXTENT_MAX 0.98
#define DB_SNAKE_SHAPE_EXTENT_MIN 0.82
#define DB_SNAKE_SHAPE_EXTENT_EPSILON 0.01
#define DB_SNAKE_SHAPE_INTERSECT_EPSILON 0.000000001
#define DB_SNAKE_SHAPE_HALF DB_SNAKE_SHAPE_CENTER
#define DB_SNAKE_SHAPE_RECT_HALF_HEIGHT_MAX 0.50
#define DB_SNAKE_SHAPE_RECT_HALF_HEIGHT_MIN 0.30
#define DB_SNAKE_SHAPE_RECT_HALF_WIDTH_MAX 0.50
#define DB_SNAKE_SHAPE_RECT_HALF_WIDTH_MIN 0.30
#define DB_SNAKE_SHAPE_ROTATION_FULL_TURN_RAD 0x1.921fb54442d18p+2
#define DB_SNAKE_SHAPE_SALT_CIRCLE_RX DB_U32_MIX_MUL_A
#define DB_SNAKE_SHAPE_SALT_CIRCLE_RY DB_U32_MIX_MUL_B
#define DB_SNAKE_SHAPE_SALT_DIAMOND_RADIUS 0x6C8E9CF5U
#define DB_SNAKE_SHAPE_SALT_EXTENT_X 0xD1B54A35U
#define DB_SNAKE_SHAPE_SALT_EXTENT_Y 0x94D049BBU
#define DB_SNAKE_SHAPE_SALT_RECT_HALF_HEIGHT 0xCF1BBCDDU
#define DB_SNAKE_SHAPE_SALT_RECT_HALF_WIDTH DB_U32_GOLDEN_RATIO
#define DB_SNAKE_SHAPE_SALT_ROTATE_ENABLE 0xCA5A826BU
#define DB_SNAKE_SHAPE_SALT_ROTATION 0xC6BC2796U
#define DB_SNAKE_SHAPE_SALT_TRAP_BOTTOM_WIDTH DB_U32_SALT_COLOR_R
#define DB_SNAKE_SHAPE_SALT_TRAP_TOP_WIDTH DB_U32_SALT_ORIGIN_Y
#define DB_SNAKE_SHAPE_SALT_TRI_BOTTOM_WIDTH 0x1B56C4E9U
#define DB_SNAKE_SHAPE_SALT_TRI_VARIANT 0xB5297A4DU
#define DB_SNAKE_SHAPE_TRAP_BOTTOM_WIDTH_MAX 1.00
#define DB_SNAKE_SHAPE_TRAP_BOTTOM_WIDTH_MIN 0.55
#define DB_SNAKE_SHAPE_TRAP_TOP_WIDTH_MAX 0.75
#define DB_SNAKE_SHAPE_TRAP_TOP_WIDTH_MIN 0.20
#define DB_SNAKE_SHAPE_TRI_BOTTOM_WIDTH_MAX 0.90
#define DB_SNAKE_SHAPE_TRI_BOTTOM_WIDTH_MIN 0.70
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
    double circle_radius_x;
    double circle_radius_y;
    double diamond_radius;
    double triangle_bottom_width;
    double trapezoid_top_width;
    double trapezoid_bottom_width;
    double rect_half_width;
    double rect_half_height;
    double extent_x;
    double extent_y;
    double rotate_cos;
    double rotate_sin;
    uint32_t rotate_enabled;
    uint32_t triangle_variant;
} db_snake_shape_profile_t;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    double color_r;
    double color_g;
    double color_b;
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
    const double circle_radius_x = db_u32_to_range_f64(
        db_mix_u32(seed_base ^ DB_SNAKE_SHAPE_SALT_CIRCLE_RX),
        DB_SNAKE_SHAPE_CIRCLE_RADIUS_MIN, DB_SNAKE_SHAPE_CIRCLE_RADIUS_MAX);
    const double circle_radius_y = db_u32_to_range_f64(
        db_mix_u32(seed_base ^ DB_SNAKE_SHAPE_SALT_CIRCLE_RY),
        DB_SNAKE_SHAPE_CIRCLE_RADIUS_MIN, DB_SNAKE_SHAPE_CIRCLE_RADIUS_MAX);
    const double diamond_radius = db_u32_to_range_f64(
        db_mix_u32(seed_base ^ DB_SNAKE_SHAPE_SALT_DIAMOND_RADIUS),
        DB_SNAKE_SHAPE_DIAMOND_RADIUS_MIN, DB_SNAKE_SHAPE_DIAMOND_RADIUS_MAX);
    const double triangle_bottom_width = db_u32_to_range_f64(
        db_mix_u32(seed_base ^ DB_SNAKE_SHAPE_SALT_TRI_BOTTOM_WIDTH),
        DB_SNAKE_SHAPE_TRI_BOTTOM_WIDTH_MIN,
        DB_SNAKE_SHAPE_TRI_BOTTOM_WIDTH_MAX);
    double trapezoid_top_width = db_u32_to_range_f64(
        db_mix_u32(seed_base ^ DB_SNAKE_SHAPE_SALT_TRAP_TOP_WIDTH),
        DB_SNAKE_SHAPE_TRAP_TOP_WIDTH_MIN, DB_SNAKE_SHAPE_TRAP_TOP_WIDTH_MAX);
    double trapezoid_bottom_width = db_u32_to_range_f64(
        db_mix_u32(seed_base ^ DB_SNAKE_SHAPE_SALT_TRAP_BOTTOM_WIDTH),
        DB_SNAKE_SHAPE_TRAP_BOTTOM_WIDTH_MIN,
        DB_SNAKE_SHAPE_TRAP_BOTTOM_WIDTH_MAX);
    const double rect_half_width = db_u32_to_range_f64(
        db_mix_u32(seed_base ^ DB_SNAKE_SHAPE_SALT_RECT_HALF_WIDTH),
        DB_SNAKE_SHAPE_RECT_HALF_WIDTH_MIN, DB_SNAKE_SHAPE_RECT_HALF_WIDTH_MAX);
    const double rect_half_height = db_u32_to_range_f64(
        db_mix_u32(seed_base ^ DB_SNAKE_SHAPE_SALT_RECT_HALF_HEIGHT),
        DB_SNAKE_SHAPE_RECT_HALF_HEIGHT_MIN,
        DB_SNAKE_SHAPE_RECT_HALF_HEIGHT_MAX);
    profile.circle_radius_x = circle_radius_x;
    profile.circle_radius_y = circle_radius_y;
    profile.diamond_radius = diamond_radius;
    profile.triangle_bottom_width = triangle_bottom_width;
    profile.trapezoid_top_width = trapezoid_top_width;
    profile.trapezoid_bottom_width = trapezoid_bottom_width;
    profile.rect_half_width = rect_half_width;
    profile.rect_half_height = rect_half_height;
    const uint32_t rotate_seed =
        db_mix_u32(seed_base ^ DB_SNAKE_SHAPE_SALT_ROTATE_ENABLE);
    profile.rotate_enabled = ((rotate_seed & 3U) == 0U) ? 1U : 0U;
    if (profile.rotate_enabled != 0U) {
        const double angle_unit = db_u32_to_unit_f64(
            db_mix_u32(seed_base ^ DB_SNAKE_SHAPE_SALT_ROTATION));
        const double angle_value =
            angle_unit * DB_SNAKE_SHAPE_ROTATION_FULL_TURN_RAD;
        profile.rotate_cos = cos(angle_value);
        profile.rotate_sin = sin(angle_value);
    } else {
        profile.rotate_cos = 1.0;
        profile.rotate_sin = 0.0;
    }
    profile.triangle_variant =
        db_mix_u32(seed_base ^ DB_SNAKE_SHAPE_SALT_TRI_VARIANT) %
        DB_SNAKE_SHAPE_TRIANGLE_VARIANT_COUNT;
    if (trapezoid_top_width > trapezoid_bottom_width) {
        const double tmp = trapezoid_top_width;
        trapezoid_top_width = trapezoid_bottom_width;
        trapezoid_bottom_width = tmp;
        profile.trapezoid_top_width = trapezoid_top_width;
        profile.trapezoid_bottom_width = trapezoid_bottom_width;
    }
    double base_half_width = rect_half_width;
    double base_half_height = rect_half_height;
    if (shape_kind == DB_SNAKE_SHAPE_CIRCLE) {
        base_half_width = circle_radius_x;
        base_half_height = circle_radius_y;
    } else if (shape_kind == DB_SNAKE_SHAPE_DIAMOND) {
        base_half_width = diamond_radius;
        base_half_height = diamond_radius;
    } else if (shape_kind == DB_SNAKE_SHAPE_TRIANGLE) {
        base_half_width = triangle_bottom_width * DB_SNAKE_SHAPE_CENTER;
        base_half_height = DB_SNAKE_SHAPE_CENTER;
    } else if (shape_kind == DB_SNAKE_SHAPE_TRAPEZOID) {
        const double max_width =
            fmax(trapezoid_top_width, trapezoid_bottom_width);
        base_half_width = max_width * DB_SNAKE_SHAPE_CENTER;
        base_half_height = DB_SNAKE_SHAPE_CENTER;
    }
    const double abs_cos = fabs(profile.rotate_cos);
    const double abs_sin = fabs(profile.rotate_sin);
    const double max_allowed =
        DB_SNAKE_SHAPE_CENTER - DB_SNAKE_SHAPE_EDGE_INSET;
    const double max_allowed_effective =
        fmax(max_allowed, DB_SNAKE_SHAPE_EXTENT_EPSILON);
    const double extent_coeff_x =
        (abs_cos * base_half_width) + (abs_sin * base_half_height);
    const double extent_coeff_y =
        (abs_sin * base_half_width) + (abs_cos * base_half_height);
    const double max_extent_coeff = fmax(extent_coeff_x, extent_coeff_y);
    double safe_extent_max = DB_SNAKE_SHAPE_EXTENT_MAX;
    if (max_extent_coeff > 0.0) {
        safe_extent_max =
            fmin(safe_extent_max, max_allowed_effective / max_extent_coeff);
    }
    safe_extent_max = fmax(safe_extent_max, DB_SNAKE_SHAPE_EXTENT_EPSILON);
    const double safe_extent_min =
        fmin(DB_SNAKE_SHAPE_EXTENT_MIN, safe_extent_max);
    double extent_x = db_u32_to_range_f64(
        db_mix_u32(seed_base ^ DB_SNAKE_SHAPE_SALT_EXTENT_X), safe_extent_min,
        safe_extent_max);
    double extent_y = db_u32_to_range_f64(
        db_mix_u32(seed_base ^ DB_SNAKE_SHAPE_SALT_EXTENT_Y), safe_extent_min,
        safe_extent_max);
    const double bound_x = (abs_cos * base_half_width * extent_x) +
                           (abs_sin * base_half_height * extent_y);
    const double bound_y = (abs_sin * base_half_width * extent_x) +
                           (abs_cos * base_half_height * extent_y);
    const double max_bound = fmax(bound_x, bound_y);
    if ((max_bound > max_allowed) && (max_bound > 0.0)) {
        const double scale = max_allowed / max_bound;
        extent_x *= scale;
        extent_y *= scale;
    }
    const double scaled_bound_x = (abs_cos * base_half_width * extent_x) +
                                  (abs_sin * base_half_height * extent_y);
    const double scaled_bound_y = (abs_sin * base_half_width * extent_x) +
                                  (abs_cos * base_half_height * extent_y);
    const double scaled_max_bound = fmax(scaled_bound_x, scaled_bound_y);
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
    const db_snake_shape_profile_t *profile, double norm_x, double norm_y,
    double *out_local_x, double *out_local_y) {
    const double scaled_x = norm_x * profile->extent_x;
    const double scaled_y = norm_y * profile->extent_y;
    const double local_x =
        (scaled_x * profile->rotate_cos) + (scaled_y * profile->rotate_sin);
    const double local_y =
        (-scaled_x * profile->rotate_sin) + (scaled_y * profile->rotate_cos);
    *out_local_x = local_x + DB_SNAKE_SHAPE_CENTER;
    *out_local_y = local_y + DB_SNAKE_SHAPE_CENTER;
}

static inline int db_snake_shape_polygon_row_interval_local(
    const double *verts_x, const double *verts_y, size_t vert_count,
    double row_y, double *out_min_x, double *out_max_x) {
    if ((verts_x == NULL) || (verts_y == NULL) || (vert_count < 3U) ||
        (out_min_x == NULL) || (out_max_x == NULL)) {
        return 0;
    }
    int has_intersection = 0;
    double min_x = 0.0;
    double max_x = 0.0;
    for (size_t edge_index = 0U; edge_index < vert_count; edge_index++) {
        const size_t next_index = (edge_index + 1U) % vert_count;
        const double x0 = verts_x[edge_index];
        const double y0 = verts_y[edge_index];
        const double x1 = verts_x[next_index];
        const double y1 = verts_y[next_index];
        const double edge_dy = y1 - y0;
        if (fabs(edge_dy) <= DB_SNAKE_SHAPE_INTERSECT_EPSILON) {
            if (fabs(row_y - y0) <= DB_SNAKE_SHAPE_INTERSECT_EPSILON) {
                if (has_intersection == 0) {
                    min_x = fmin(x0, x1);
                    max_x = fmax(x0, x1);
                    has_intersection = 1;
                } else {
                    min_x = fmin(min_x, fmin(x0, x1));
                    max_x = fmax(max_x, fmax(x0, x1));
                }
            }
            continue;
        }
        const double min_y = fmin(y0, y1) - DB_SNAKE_SHAPE_INTERSECT_EPSILON;
        const double max_y = fmax(y0, y1) + DB_SNAKE_SHAPE_INTERSECT_EPSILON;
        if ((row_y < min_y) || (row_y > max_y)) {
            continue;
        }
        const double edge_t = (row_y - y0) / edge_dy;
        if ((edge_t < -DB_SNAKE_SHAPE_INTERSECT_EPSILON) ||
            (edge_t > (1.0 + DB_SNAKE_SHAPE_INTERSECT_EPSILON))) {
            continue;
        }
        const double x_at_row = x0 + ((x1 - x0) * edge_t);
        if (has_intersection == 0) {
            min_x = x_at_row;
            max_x = x_at_row;
            has_intersection = 1;
        } else {
            min_x = fmin(min_x, x_at_row);
            max_x = fmax(max_x, x_at_row);
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
    const db_snake_shape_profile_t *profile, double row_y, double *out_min_x,
    double *out_max_x) {
    if ((profile == NULL) || (out_min_x == NULL) || (out_max_x == NULL)) {
        return 0;
    }
    const double dy = row_y - DB_SNAKE_SHAPE_CENTER;
    const double extent_x = fmax(profile->extent_x, 0.000001);
    const double extent_y = fmax(profile->extent_y, 0.000001);
    const double radius_x = fmax(profile->circle_radius_x, 0.01);
    const double radius_y = fmax(profile->circle_radius_y, 0.01);
    const double coeff_x = profile->rotate_cos / (extent_x * radius_x);
    const double coeff_y = profile->rotate_sin / (extent_y * radius_y);
    const double term0 = (-profile->rotate_sin * dy) / (extent_x * radius_x);
    const double term1 = (profile->rotate_cos * dy) / (extent_y * radius_y);
    const double quad_a = (coeff_x * coeff_x) + (coeff_y * coeff_y);
    if (quad_a <= 0.0) {
        return 0;
    }
    const double quad_b = 2.0 * ((coeff_x * term0) + (coeff_y * term1));
    const double quad_c = (term0 * term0) + (term1 * term1) - 1.0;
    const double disc = (quad_b * quad_b) - (4.0 * quad_a * quad_c);
    if (disc < -DB_SNAKE_SHAPE_INTERSECT_EPSILON) {
        return 0;
    }
    const double disc_clamped = fmax(disc, 0.0);
    const double sqrt_disc = sqrt(disc_clamped);
    const double inv_denom = 0.5 / quad_a;
    const double dx0 = (-quad_b - sqrt_disc) * inv_denom;
    const double dx1 = (-quad_b + sqrt_disc) * inv_denom;
    *out_min_x = DB_SNAKE_SHAPE_CENTER + fmin(dx0, dx1);
    *out_max_x = DB_SNAKE_SHAPE_CENTER + fmax(dx0, dx1);
    return 1;
}

static inline int db_snake_shape_local_interval_to_col_bounds(
    double interval_min_x, double interval_max_x, uint32_t width,
    uint32_t *out_col_start, uint32_t *out_col_end) {
    if ((out_col_start == NULL) || (out_col_end == NULL) || (width == 0U)) {
        return 0;
    }
    double min_x = fmax(fmin(interval_min_x, interval_max_x), 0.0);
    double max_x = fmin(fmax(interval_min_x, interval_max_x), 1.0);
    if (max_x < min_x) {
        return 0;
    }
    const double col_min_value =
        ceil((min_x * (double)width) - 0.5 - DB_SNAKE_SHAPE_INTERSECT_EPSILON);
    const double col_max_value =
        floor((max_x * (double)width) - 0.5 + DB_SNAKE_SHAPE_INTERSECT_EPSILON);
    int64_t col_min = (int64_t)col_min_value;
    int64_t col_max = (int64_t)col_max_value;
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
    const db_snake_shape_profile_t *profile = &shape_desc->shape_profile;
    double verts_x[4] = {0.0, 0.0, 0.0, 0.0};
    double verts_y[4] = {0.0, 0.0, 0.0, 0.0};
    size_t vert_count = 0U;
    if (shape_desc->shape_kind == DB_SNAKE_SHAPE_RECT) {
        const double x0 = -profile->rect_half_width;
        const double x1 = profile->rect_half_width;
        const double y0 = -profile->rect_half_height;
        const double y1 = profile->rect_half_height;
        const double nx[4] = {x0, x1, x1, x0};
        const double ny[4] = {y0, y0, y1, y1};
        vert_count = 4U;
        for (size_t idx = 0U; idx < vert_count; idx++) {
            db_snake_shape_transform_norm_vertex_to_local(
                profile, nx[idx], ny[idx], &verts_x[idx], &verts_y[idx]);
        }
    } else if (shape_desc->shape_kind == DB_SNAKE_SHAPE_DIAMOND) {
        const double radius = profile->diamond_radius;
        const double nx[4] = {0.0, radius, 0.0, -radius};
        const double ny[4] = {-radius, 0.0, radius, 0.0};
        vert_count = 4U;
        for (size_t idx = 0U; idx < vert_count; idx++) {
            db_snake_shape_transform_norm_vertex_to_local(
                profile, nx[idx], ny[idx], &verts_x[idx], &verts_y[idx]);
        }
    } else if (shape_desc->shape_kind == DB_SNAKE_SHAPE_TRIANGLE) {
        double left_top = 0.0;
        double right_top = 0.0;
        double left_bottom = 0.0;
        double right_bottom = 0.0;
        if (profile->triangle_variant == 1U) {
            left_top = -DB_SNAKE_SHAPE_CENTER + DB_SNAKE_SHAPE_EDGE_INSET;
            right_top = left_top;
            left_bottom = left_top;
            right_bottom = left_top + profile->triangle_bottom_width;
        } else if (profile->triangle_variant == 2U) {
            right_top = DB_SNAKE_SHAPE_CENTER - DB_SNAKE_SHAPE_EDGE_INSET;
            left_top = right_top;
            right_bottom = right_top;
            left_bottom = right_top - profile->triangle_bottom_width;
        } else {
            left_top = 0.0;
            right_top = 0.0;
            left_bottom = -profile->triangle_bottom_width * DB_SNAKE_SHAPE_HALF;
            right_bottom = profile->triangle_bottom_width * DB_SNAKE_SHAPE_HALF;
        }
        const double nx[4] = {left_top, right_top, right_bottom, left_bottom};
        const double ny[4] = {-DB_SNAKE_SHAPE_CENTER, -DB_SNAKE_SHAPE_CENTER,
                              DB_SNAKE_SHAPE_CENTER, DB_SNAKE_SHAPE_CENTER};
        vert_count = 4U;
        for (size_t idx = 0U; idx < vert_count; idx++) {
            db_snake_shape_transform_norm_vertex_to_local(
                profile, nx[idx], ny[idx], &verts_x[idx], &verts_y[idx]);
        }
    } else if (shape_desc->shape_kind == DB_SNAKE_SHAPE_TRAPEZOID) {
        const double top_half =
            profile->trapezoid_top_width * DB_SNAKE_SHAPE_HALF;
        const double bottom_half =
            profile->trapezoid_bottom_width * DB_SNAKE_SHAPE_HALF;
        const double nx[4] = {-top_half, top_half, bottom_half, -bottom_half};
        const double ny[4] = {-DB_SNAKE_SHAPE_CENTER, -DB_SNAKE_SHAPE_CENTER,
                              DB_SNAKE_SHAPE_CENTER, DB_SNAKE_SHAPE_CENTER};
        vert_count = 4U;
        for (size_t idx = 0U; idx < vert_count; idx++) {
            db_snake_shape_transform_norm_vertex_to_local(
                profile, nx[idx], ny[idx], &verts_x[idx], &verts_y[idx]);
        }
    }
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
        const double row_center_y =
            ((double)local_row + DB_SNAKE_SHAPE_CENTER) /
            (double)region->height;
        double interval_min_x = 0.0;
        double interval_max_x = 0.0;
        int has_interval = 0;
        if (shape_desc->shape_kind == DB_SNAKE_SHAPE_CIRCLE) {
            has_interval = db_snake_shape_circle_row_interval_local(
                profile, row_center_y, &interval_min_x, &interval_max_x);
        } else if (vert_count > 0U) {
            has_interval = db_snake_shape_polygon_row_interval_local(
                verts_x, verts_y, vert_count, row_center_y, &interval_min_x,
                &interval_max_x);
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
