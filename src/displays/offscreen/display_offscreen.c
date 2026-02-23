#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "../../config/benchmark_config.h"
#include "../../core/db_core.h"
#include "../../renderers/cpu_renderer/renderer_cpu_renderer.h"
#include "../../renderers/renderer_benchmark_common.h"
#include "../../renderers/renderer_identity.h"
#include "../display_dispatch.h"
#include "../display_hash_common.h"

#define BACKEND_NAME "display_offscreen"
#define DEFAULT_OFFSCREEN_FRAMES 600U

static uint32_t db_offscreen_frames_from_runtime(void) {
    uint32_t parsed = 0U;
    if (db_parse_u32_positive_value(
            BACKEND_NAME, DB_RUNTIME_OPT_OFFSCREEN_FRAMES,
            db_runtime_option_get(DB_RUNTIME_OPT_OFFSCREEN_FRAMES),
            &parsed) != 0) {
        return parsed;
    }
    if (db_parse_u32_nonnegative_value(
            BACKEND_NAME, DB_RUNTIME_OPT_FRAME_LIMIT,
            db_runtime_option_get(DB_RUNTIME_OPT_FRAME_LIMIT), &parsed) != 0) {
        return parsed;
    }
    return DEFAULT_OFFSCREEN_FRAMES;
}

static int db_run_offscreen_cpu(void) {
    db_install_signal_handlers();

    const uint32_t frame_limit = db_offscreen_frames_from_runtime();
    const double fps_cap =
        db_runtime_resolve_fps_cap(BACKEND_NAME, BENCH_FPS_CAP_D);
    const int hash_every_frame = db_value_is_truthy(
        db_runtime_option_get(DB_RUNTIME_OPT_HASH_EVERY_FRAME));

    db_renderer_cpu_renderer_init();
    const char *capability_mode = db_renderer_cpu_renderer_capability_mode();
    const uint32_t work_unit_count = db_renderer_cpu_renderer_work_unit_count();

    uint64_t frames = 0U;
    double next_progress_log_due_ms = 0.0;
    db_display_hash_tracker_t hash_tracker =
        db_display_hash_tracker_create(1, hash_every_frame, "bo_hash");

    for (uint32_t frame = 0U; !db_should_stop(); frame++) {
        if ((frame_limit > 0U) && (frame >= frame_limit)) {
            break;
        }
        const uint64_t frame_start_ns = db_now_ns_monotonic();
        const double time_s = (double)frame / BENCH_TARGET_FPS_D;
        db_renderer_cpu_renderer_render_frame(time_s);

        uint32_t pixel_width = 0U;
        uint32_t pixel_height = 0U;
        const uint32_t *pixels =
            db_renderer_cpu_renderer_pixels_rgba8(&pixel_width, &pixel_height);
        if (pixels == NULL) {
            db_failf(BACKEND_NAME, "cpu renderer returned NULL framebuffer");
        }
        const size_t byte_count =
            (size_t)((uint64_t)pixel_width * (uint64_t)pixel_height *
                     sizeof(uint32_t));
        const uint64_t frame_hash = db_fnv1a64_bytes(pixels, byte_count);
        db_display_hash_tracker_record(BACKEND_NAME, &hash_tracker, frames,
                                       frame_hash);

        frames++;
        const double elapsed_ms =
            ((double)frames * DB_MS_PER_SECOND_D) / BENCH_TARGET_FPS_D;
        db_benchmark_log_periodic(
            db_dispatch_api_name(DB_API_CPU), db_renderer_name_cpu(),
            BACKEND_NAME, frames, work_unit_count, elapsed_ms, capability_mode,
            &next_progress_log_due_ms, BENCH_LOG_INTERVAL_MS_D);
        db_sleep_to_fps_cap(BACKEND_NAME, frame_start_ns, fps_cap);
    }

    const uint32_t grid_cols = db_grid_cols_effective();
    const uint32_t grid_rows = db_grid_rows_effective();
    const uint32_t *final_pixels =
        db_renderer_cpu_renderer_pixels_rgba8(NULL, NULL);
    const size_t final_byte_count =
        (size_t)((uint64_t)grid_cols * (uint64_t)grid_rows * sizeof(uint32_t));
    const uint64_t final_hash =
        db_fnv1a64_bytes(final_pixels, final_byte_count);
    hash_tracker.final_hash = final_hash;

    const double total_ms =
        ((double)frames * DB_MS_PER_SECOND_D) / BENCH_TARGET_FPS_D;
    db_benchmark_log_final(db_dispatch_api_name(DB_API_CPU),
                           db_renderer_name_cpu(), BACKEND_NAME, frames,
                           work_unit_count, total_ms, capability_mode);
    db_display_hash_tracker_log_final(BACKEND_NAME, &hash_tracker);
    db_infof(BACKEND_NAME, "grid=%ux%u", grid_rows, grid_cols);

    db_renderer_cpu_renderer_shutdown();
    return EXIT_SUCCESS;
}

int db_run_offscreen(db_api_t api, db_gl_renderer_t renderer) {
    if (db_dispatch_display_supports_api(DB_DISPLAY_OFFSCREEN, api) == 0) {
        db_failf(BACKEND_NAME,
                 "requested offscreen/API combination is unavailable in this "
                 "build (api=%d)",
                 (int)api);
    }

    if (api == DB_API_CPU) {
        return db_run_offscreen_cpu();
    }

#ifdef DB_HAS_GLFW
    db_runtime_option_set(DB_RUNTIME_OPT_OFFSCREEN, "1");
    return db_run_glfw_window(api, renderer);
#else
    (void)renderer;
    db_failf(BACKEND_NAME, "offscreen %s requires GLFW support in this build",
             db_dispatch_api_name(api));
    return EXIT_FAILURE;
#endif
}
