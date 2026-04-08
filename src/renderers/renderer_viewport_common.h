#ifndef DRIVERBENCH_RENDERER_VIEWPORT_COMMON_H
#define DRIVERBENCH_RENDERER_VIEWPORT_COMMON_H

#include "renderer_benchmark_gradient.h"

typedef struct {
    int has_viewport;
    int viewport_changed;
    int viewport_height_px;
    int viewport_width_px;
} db_renderer_viewport_state_t;

static inline int db_renderer_viewport_changed(int previous_width,
                                               int previous_height,
                                               int next_width,
                                               int next_height) {
    return (previous_width != next_width) || (previous_height != next_height);
}

static inline int db_renderer_update_tracked_viewport(int next_width,
                                                      int next_height,
                                                      int *io_last_width,
                                                      int *io_last_height) {
    if ((io_last_width == NULL) || (io_last_height == NULL)) {
        return 0;
    }
    const int changed = db_renderer_viewport_changed(
        *io_last_width, *io_last_height, next_width, next_height);
    if (changed != 0) {
        *io_last_width = next_width;
        *io_last_height = next_height;
    }
    return changed;
}

static inline void db_renderer_resolve_viewport_or_grid(
    const char *backend, int *viewport_width_px, int *viewport_height_px) {
    if ((viewport_width_px == NULL) || (viewport_height_px == NULL)) {
        return;
    }
    if ((*viewport_width_px > 0) && (*viewport_height_px > 0)) {
        return;
    }
    *viewport_width_px = db_checked_u32_to_i32(backend, "viewport_width_px",
                                               db_grid_cols_effective());
    *viewport_height_px = db_checked_u32_to_i32(backend, "viewport_height_px",
                                                db_grid_rows_effective());
}

static inline int db_renderer_resolve_and_track_viewport(
    const char *backend, int *io_viewport_width_px, int *io_viewport_height_px,
    int *io_last_width, int *io_last_height) {
    if ((io_viewport_width_px == NULL) || (io_viewport_height_px == NULL)) {
        return 0;
    }
    db_renderer_resolve_viewport_or_grid(backend, io_viewport_width_px,
                                         io_viewport_height_px);
    return db_renderer_update_tracked_viewport(*io_viewport_width_px,
                                               *io_viewport_height_px,
                                               io_last_width, io_last_height);
}

static inline db_renderer_viewport_state_t db_renderer_resolve_viewport_state(
    const char *backend_name, int *io_viewport_width_px,
    int *io_viewport_height_px, int *io_last_viewport_width_px,
    int *io_last_viewport_height_px) {
    db_renderer_viewport_state_t state = {
        .has_viewport = 0,
        .viewport_changed = 0,
        .viewport_height_px = 0,
        .viewport_width_px = 0,
    };
    state.viewport_changed = db_renderer_resolve_and_track_viewport(
        backend_name, io_viewport_width_px, io_viewport_height_px,
        io_last_viewport_width_px, io_last_viewport_height_px);
    if ((io_viewport_width_px == NULL) || (io_viewport_height_px == NULL)) {
        return state;
    }
    state.viewport_width_px = *io_viewport_width_px;
    state.viewport_height_px = *io_viewport_height_px;
    state.has_viewport =
        (state.viewport_width_px > 0) && (state.viewport_height_px > 0);
    return state;
}

#endif
