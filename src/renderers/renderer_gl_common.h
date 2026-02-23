#ifndef DRIVERBENCH_RENDERER_GL_COMMON_H
#define DRIVERBENCH_RENDERER_GL_COMMON_H

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../core/db_core.h"
#include "../displays/bench_config.h"
#include "renderer_benchmark_common.h"

#define DB_GL_PROBE_PREFIX_BYTES 64U
#define DB_GL_MAP_RANGE_PROBE_XOR_SEED 0xA5U

typedef void (*db_gl_generic_proc_t)(void);
typedef db_gl_generic_proc_t (*db_gl_get_proc_address_fn_t)(const char *name);

typedef struct {
    int use_map_buffer_upload;
    int use_map_range_upload;
    int use_persistent_upload;
    void *persistent_mapped_ptr;
} db_gl_upload_probe_result_t;

typedef struct {
    size_t dst_offset_bytes;
    size_t src_offset_bytes;
    size_t size_bytes;
} db_gl_upload_range_t;

typedef struct {
    float *vertices;
    size_t vertex_stride;
    db_pattern_t pattern;
    uint32_t work_unit_count;
    uint32_t draw_vertex_count;
    db_gl_upload_probe_result_t upload;
} db_gl_vertex_init_t;

static inline int db_has_gl_extension_token(const char *exts,
                                            const char *needle) {
    if ((exts == NULL) || (needle == NULL)) {
        return 0;
    }
    const size_t needle_len = strlen(needle);
    const char *ext_ptr = exts;
    while ((ext_ptr = strstr(ext_ptr, needle)) != NULL) {
        if (((ext_ptr == exts) || (ext_ptr[-1] == ' ')) &&
            ((ext_ptr[needle_len] == '\0') || (ext_ptr[needle_len] == ' '))) {
            return 1;
        }
        ext_ptr += needle_len;
    }
    return 0;
}

static inline int db_parse_gl_version_numbers(const char *version_text,
                                              int *major_out, int *minor_out) {
    if ((version_text == NULL) || (major_out == NULL) || (minor_out == NULL)) {
        return 0;
    }

    const char *cursor = version_text;
    while ((*cursor != '\0') && ((*cursor < '0') || (*cursor > '9'))) {
        cursor++;
    }
    if (*cursor == '\0') {
        return 0;
    }

    char *parse_end = NULL;
    const long major_l = strtol(cursor, &parse_end, 10);
    if ((parse_end == cursor) || (*parse_end != '.')) {
        return 0;
    }
    const char *minor_start = parse_end + 1;
    const long minor_l = strtol(minor_start, &parse_end, 10);
    if (parse_end == minor_start) {
        return 0;
    }
    if ((major_l < 0L) || (minor_l < 0L) || (major_l > (long)INT_MAX) ||
        (minor_l > (long)INT_MAX)) {
        return 0;
    }

    *major_out = (int)major_l;
    *minor_out = (int)minor_l;
    return 1;
}

static inline int db_gl_version_text_at_least(const char *version_text,
                                              int req_major, int req_minor) {
    int major = 0;
    int minor = 0;
    if (!db_parse_gl_version_numbers(version_text, &major, &minor)) {
        return 0;
    }
    return (major > req_major) ||
           ((major == req_major) && (minor >= req_minor));
}

static inline int db_gl_is_es_context(const char *version_text) {
    return (version_text != NULL) &&
           (strstr(version_text, "OpenGL ES") != NULL);
}

typedef unsigned int (*db_gl_get_error_fn_t)(void);

static inline void db_gl_clear_errors(db_gl_get_error_fn_t get_error) {
    if (get_error == NULL) {
        return;
    }
    while (get_error() != 0U) {
    }
}

static inline size_t db_gl_probe_size(size_t bytes) {
    return (bytes < DB_GL_PROBE_PREFIX_BYTES) ? bytes
                                              : DB_GL_PROBE_PREFIX_BYTES;
}

static inline void db_gl_fill_probe_pattern(uint8_t *pattern, size_t count) {
    if (pattern == NULL) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        pattern[i] = (uint8_t)(DB_GL_MAP_RANGE_PROBE_XOR_SEED ^ (uint8_t)i);
    }
}

void db_gl_probe_upload_capabilities(size_t bytes,
                                     const float *initial_vertices,
                                     int allow_persistent_upload,
                                     db_gl_upload_probe_result_t *out);
int db_gl_has_vbo_support(void);
void db_gl_set_proc_address_loader(
    db_gl_get_proc_address_fn_t get_proc_address);

void db_gl_upload_ranges(const void *source_base, size_t total_bytes,
                         int use_persistent_upload, void *persistent_mapped_ptr,
                         int use_map_range_upload, int use_map_buffer_upload,
                         const db_gl_upload_range_t *ranges,
                         size_t range_count);

void db_gl_upload_buffer(const void *source, size_t bytes,
                         int use_persistent_upload, void *persistent_mapped_ptr,
                         int use_map_range_upload, int use_map_buffer_upload);

static inline int db_init_band_vertices_common(db_gl_vertex_init_t *out_state,
                                               size_t vertex_stride) {
    const size_t vertex_count = (size_t)BENCH_BANDS * DB_RECT_VERTEX_COUNT;
    const size_t float_count = vertex_count * vertex_stride;

    float *vertices = (float *)calloc(float_count, sizeof(float));
    if (vertices == NULL) {
        return 0;
    }

    *out_state = (db_gl_vertex_init_t){0};
    out_state->vertices = vertices;
    out_state->vertex_stride = vertex_stride;
    out_state->pattern = DB_PATTERN_BANDS;
    out_state->work_unit_count = BENCH_BANDS;
    out_state->draw_vertex_count = db_checked_size_to_u32(
        DB_BENCH_COMMON_BACKEND, "bands_draw_vertex_count", vertex_count);
    return 1;
}

static inline int db_init_grid_vertices_common(db_gl_vertex_init_t *out_state,
                                               size_t vertex_stride) {
    const uint64_t tile_count_u64 =
        (uint64_t)db_pattern_work_unit_count(DB_PATTERN_SNAKE_GRID);
    if ((tile_count_u64 == 0U) || (tile_count_u64 > UINT32_MAX)) {
        return 0;
    }

    const uint64_t vertex_count_u64 = tile_count_u64 * DB_RECT_VERTEX_COUNT;
    if (vertex_count_u64 > (uint64_t)INT32_MAX) {
        return 0;
    }

    const uint64_t float_count_u64 = vertex_count_u64 * (uint64_t)vertex_stride;
    if (float_count_u64 > ((uint64_t)SIZE_MAX / sizeof(float))) {
        return 0;
    }

    const size_t float_count = (size_t)float_count_u64;
    const uint32_t tile_count = db_checked_u64_to_u32(
        DB_BENCH_COMMON_BACKEND, "grid_tile_count", tile_count_u64);
    float *vertices = (float *)calloc(float_count, sizeof(float));
    if (vertices == NULL) {
        return 0;
    }

    for (uint32_t tile_index = 0; tile_index < tile_count; tile_index++) {
        float x0 = 0.0F;
        float y0 = 0.0F;
        float x1 = 0.0F;
        float y1 = 0.0F;
        db_grid_tile_bounds_ndc(tile_index, &x0, &y0, &x1, &y1);
        const size_t base =
            (size_t)tile_index * DB_RECT_VERTEX_COUNT * vertex_stride;
        float *unit = &vertices[base];
        db_fill_rect_unit_pos(unit, x0, y0, x1, y1, vertex_stride);
        db_set_rect_unit_rgb(
            unit, vertex_stride, DB_VERTEX_POSITION_FLOAT_COUNT,
            BENCH_GRID_PHASE0_R, BENCH_GRID_PHASE0_G, BENCH_GRID_PHASE0_B);
        if (vertex_stride == DB_ES_VERTEX_FLOAT_STRIDE) {
            db_set_rect_unit_alpha(unit, vertex_stride,
                                   DB_VERTEX_POSITION_FLOAT_COUNT +
                                       DB_VERTEX_COLOR_FLOAT_COUNT,
                                   1.0F);
        }
    }

    *out_state = (db_gl_vertex_init_t){0};
    out_state->vertices = vertices;
    out_state->vertex_stride = vertex_stride;
    out_state->work_unit_count = tile_count;
    out_state->draw_vertex_count = db_checked_u64_to_u32(
        DB_BENCH_COMMON_BACKEND, "grid_draw_vertex_count", vertex_count_u64);
    return 1;
}

static inline int db_init_vertices_for_pattern_common_with_stride(
    const char *backend_name, db_gl_vertex_init_t *out_state,
    db_pattern_t pattern, size_t vertex_stride) {
    const int use_grid_init = (pattern == DB_PATTERN_SNAKE_GRID) ||
                              (pattern == DB_PATTERN_RECT_SNAKE) ||
                              (pattern == DB_PATTERN_GRADIENT_SWEEP) ||
                              (pattern == DB_PATTERN_GRADIENT_FILL);
    const int initialized =
        use_grid_init ? db_init_grid_vertices_common(out_state, vertex_stride)
                      : db_init_band_vertices_common(out_state, vertex_stride);
    if (initialized == 0) {
        db_failf(backend_name, "benchmark mode '%s' initialization failed",
                 db_pattern_mode_name(pattern));
    }
    out_state->pattern = pattern;
    return 1;
}

static inline int db_init_vertices_for_runtime_common_with_stride(
    const char *backend_name, db_gl_vertex_init_t *out_state,
    const db_benchmark_runtime_init_t *runtime_state, size_t vertex_stride) {
    if (runtime_state == NULL) {
        return 0;
    }
    if (!db_init_vertices_for_pattern_common_with_stride(
            backend_name, out_state, runtime_state->pattern, vertex_stride)) {
        return 0;
    }

    out_state->pattern = runtime_state->pattern;
    out_state->work_unit_count = runtime_state->work_unit_count;
    out_state->draw_vertex_count = runtime_state->draw_vertex_count;
    out_state->vertex_stride = vertex_stride;
    if ((runtime_state->pattern == DB_PATTERN_GRADIENT_SWEEP) ||
        (runtime_state->pattern == DB_PATTERN_GRADIENT_FILL)) {
        float source_r = 0.0F;
        float source_g = 0.0F;
        float source_b = 0.0F;
        db_palette_cycle_color_rgb(runtime_state->gradient_cycle, &source_r,
                                   &source_g, &source_b);
        db_fill_grid_all_rgb_stride(
            out_state->vertices, out_state->work_unit_count, vertex_stride,
            DB_VERTEX_POSITION_FLOAT_COUNT, source_r, source_g, source_b);
    }
    return 1;
}

static inline void
db_fill_band_vertices_pos_rgb_stride(float *out_vertices, uint32_t band_count,
                                     double time_s, size_t stride_floats,
                                     size_t color_offset_floats) {
    const float inv_band_count = 1.0F / (float)band_count;
    const float band_x_scale = 2.0F * inv_band_count;
    for (uint32_t band_index = 0; band_index < band_count; band_index++) {
        const float band_f = (float)band_index;
        const float x0 = (band_f * band_x_scale) - 1.0F;
        const float x1 = x0 + band_x_scale;
        float color_r = 0.0F;
        float color_g = 0.0F;
        float color_b = 0.0F;
        db_band_color_rgb(band_index, band_count, time_s, &color_r, &color_g,
                          &color_b);

        const size_t band_base =
            (size_t)band_index * DB_RECT_VERTEX_COUNT * stride_floats;
        float *unit = &out_vertices[band_base];
        db_fill_rect_unit_pos(unit, x0, -1.0F, x1, 1.0F, stride_floats);
        db_set_rect_unit_rgb(unit, stride_floats, color_offset_floats, color_r,
                             color_g, color_b);
        if (stride_floats == DB_ES_VERTEX_FLOAT_STRIDE) {
            db_set_rect_unit_alpha(unit, stride_floats,
                                   DB_VERTEX_POSITION_FLOAT_COUNT +
                                       DB_VERTEX_COLOR_FLOAT_COUNT,
                                   1.0F);
        }
    }
}

static inline void
db_update_band_vertices_rgb_stride(float *out_vertices, uint32_t band_count,
                                   double time_s, size_t stride_floats,
                                   size_t color_offset_floats) {
    for (uint32_t band_index = 0; band_index < band_count; band_index++) {
        float color_r = 0.0F;
        float color_g = 0.0F;
        float color_b = 0.0F;
        db_band_color_rgb(band_index, band_count, time_s, &color_r, &color_g,
                          &color_b);

        const size_t band_base =
            (size_t)band_index * DB_RECT_VERTEX_COUNT * stride_floats;
        float *unit = &out_vertices[band_base];
        db_set_rect_unit_rgb(unit, stride_floats, color_offset_floats, color_r,
                             color_g, color_b);
    }
}

#endif
