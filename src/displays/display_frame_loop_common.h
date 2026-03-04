#ifndef DRIVERBENCH_DISPLAY_FRAME_LOOP_COMMON_H
#define DRIVERBENCH_DISPLAY_FRAME_LOOP_COMMON_H

#include <stdint.h>

#include "../core/db_core.h"

typedef enum {
    DB_DISPLAY_FRAME_LOOP_CONTINUE = 0,
    DB_DISPLAY_FRAME_LOOP_STOP = 1,
    DB_DISPLAY_FRAME_LOOP_RETRY = 2,
} db_display_frame_loop_result_t;

typedef int (*db_display_frame_loop_should_continue_fn_t)(void *user_data);
typedef void (*db_display_frame_loop_pre_frame_fn_t)(void *user_data,
                                                     uint32_t frame_index);
typedef db_display_frame_loop_result_t (*db_display_frame_loop_frame_fn_t)(
    void *user_data, uint32_t frame_index, double elapsed_ms);

typedef struct {
    const char *backend;
    double fps_cap;
    uint32_t frame_limit;
    void *user_data;
    db_display_frame_loop_should_continue_fn_t should_continue_fn;
    db_display_frame_loop_pre_frame_fn_t pre_frame_fn;
    db_display_frame_loop_frame_fn_t frame_fn;
} db_display_frame_loop_t;

typedef struct {
    uint64_t frames;
    double elapsed_ms;
} db_display_frame_loop_run_result_t;

static inline db_display_frame_loop_run_result_t
db_display_run_frame_loop(const db_display_frame_loop_t *loop) {
    db_display_frame_loop_run_result_t result = {
        .frames = 0U,
        .elapsed_ms = 0.0,
    };
    if ((loop == NULL) || (loop->frame_fn == NULL)) {
        return result;
    }

    const uint64_t bench_start_ns = db_now_ns_monotonic();
    while (!db_should_stop()) {
        if ((loop->frame_limit > 0U) && (result.frames >= loop->frame_limit)) {
            break;
        }
        if ((loop->should_continue_fn != NULL) &&
            (loop->should_continue_fn(loop->user_data) == 0)) {
            break;
        }

        const uint64_t frame_start_ns = db_now_ns_monotonic();
        if (loop->pre_frame_fn != NULL) {
            loop->pre_frame_fn(loop->user_data, (uint32_t)result.frames);
        }
        const double elapsed_ms =
            (double)(frame_start_ns - bench_start_ns) / DB_NS_PER_MS;
        const db_display_frame_loop_result_t frame_result = loop->frame_fn(
            loop->user_data, (uint32_t)result.frames, elapsed_ms);
        if (frame_result == DB_DISPLAY_FRAME_LOOP_STOP) {
            break;
        }

        db_sleep_to_fps_cap(loop->backend, frame_start_ns, loop->fps_cap);
        if (frame_result != DB_DISPLAY_FRAME_LOOP_RETRY) {
            result.frames++;
        }
    }

    result.elapsed_ms =
        (double)(db_now_ns_monotonic() - bench_start_ns) / DB_NS_PER_MS;
    return result;
}

#endif
