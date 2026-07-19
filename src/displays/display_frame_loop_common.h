#ifndef DRIVERBENCH_DISPLAY_FRAME_LOOP_COMMON_H
#define DRIVERBENCH_DISPLAY_FRAME_LOOP_COMMON_H

#include <stdint.h>
#include <string.h>

#include "../config/runtime_options.h"
#include "../core/db_core.h"
#include "../core/db_numeric.h"
#include "../core/db_run_session.h"

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
    uint32_t initial_frame_index;
    void *user_data;
    db_display_frame_loop_should_continue_fn_t should_continue_fn;
    db_display_frame_loop_pre_frame_fn_t pre_frame_fn;
    db_display_frame_loop_frame_fn_t frame_fn;
} db_display_frame_loop_t;

typedef struct {
    uint64_t frames;
    double elapsed_ms;
    uint64_t retries;
} db_display_frame_loop_run_result_t;

static inline db_display_frame_loop_result_t
db_display_frame_loop_from_run_step(
    const db_run_step_result_t *step,
    db_committed_frame_summary_t *committed_frame) {
    if (step == NULL) {
        return DB_DISPLAY_FRAME_LOOP_STOP;
    }
    switch (step->outcome) {
    case DB_RUN_FRAME_COMMITTED:
        if (committed_frame != NULL) {
            *committed_frame = step->committed_frame;
        }
        return DB_DISPLAY_FRAME_LOOP_CONTINUE;
    case DB_RUN_WAIT:
        db_sleep_until_ns(step->wait_until.nanoseconds);
        return DB_DISPLAY_FRAME_LOOP_RETRY;
    case DB_RUN_PROGRESS:
        return DB_DISPLAY_FRAME_LOOP_RETRY;
    case DB_RUN_COMPLETE:
    case DB_RUN_FAILED:
        return DB_DISPLAY_FRAME_LOOP_STOP;
    }
    return DB_DISPLAY_FRAME_LOOP_STOP;
}

static inline int db_display_dual_metrics_enabled(void) {
    const char *const metrics_mode =
        db_runtime_option_get(DB_RUNTIME_OPT_METRICS_MODE);
    return (metrics_mode != NULL) && (strcmp(metrics_mode, "dual") == 0);
}

static inline uint32_t db_display_frame_index_u32(const char *backend,
                                                  uint64_t frame_index) {
    return db_checked_u64_to_u32((backend != NULL) ? backend
                                                   : "display_frame_loop",
                                 "frame_index", frame_index);
}

static inline db_display_frame_loop_run_result_t
db_display_run_frame_loop(const db_display_frame_loop_t *loop) {
    db_display_frame_loop_run_result_t result = {
        .frames = 0U,
        .elapsed_ms = 0.0,
        .retries = 0U,
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
        const uint32_t frame_index = db_display_frame_index_u32(
            loop->backend, (uint64_t)loop->initial_frame_index + result.frames);
        if (loop->pre_frame_fn != NULL) {
            loop->pre_frame_fn(loop->user_data, frame_index);
        }
        const double elapsed_ms =
            DB_TO_F64(frame_start_ns - bench_start_ns) / DB_NS_PER_MS;
        const db_display_frame_loop_result_t frame_result =
            loop->frame_fn(loop->user_data, frame_index, elapsed_ms);
        if (frame_result == DB_DISPLAY_FRAME_LOOP_STOP) {
            break;
        }

        if (frame_result != DB_DISPLAY_FRAME_LOOP_RETRY) {
            result.frames++;
        } else {
            result.retries++;
        }
    }

    result.elapsed_ms =
        DB_TO_F64(db_now_ns_monotonic() - bench_start_ns) / DB_NS_PER_MS;
    return result;
}

#endif
