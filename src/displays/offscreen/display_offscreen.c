#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "../../config/benchmark_config.h"
#include "../../core/db_core.h"
#include "../../core/db_hash.h"
#include "../../driverbench_cli.h"
#include "../../renderers/cpu_renderer/renderer_cpu_renderer.h"
#include "../../renderers/renderer_identity.h"
#include "../display_dispatch.h"
#include "../display_hash_common.h"

#define BACKEND_NAME "display_offscreen"

static int db_run_offscreen_cpu(const db_cli_config_t *cfg) {
    db_install_signal_handlers();

    const uint32_t frame_limit = (cfg != NULL) ? cfg->frame_limit : 0U;
    const double fps_cap = (cfg != NULL) ? cfg->fps_cap : BENCH_FPS_CAP_D;
    const db_display_hash_settings_t hash_settings =
        db_display_resolve_hash_settings(
            0, 0, (cfg != NULL) ? cfg->hash_mode : "none");

    db_renderer_cpu_renderer_init();
    const char *capability_mode = db_renderer_cpu_renderer_capability_mode();
    const uint32_t work_unit_count = db_renderer_cpu_renderer_work_unit_count();

    uint64_t frames = 0U;
    double next_progress_log_due_ms = 0.0;
    db_display_hash_tracker_t state_hash_tracker =
        db_display_hash_tracker_create(
            BACKEND_NAME, hash_settings.state_hash_enabled, "state_hash",
            (cfg != NULL) ? cfg->hash_report : "both");
    db_display_hash_tracker_t bo_hash_tracker = db_display_hash_tracker_create(
        BACKEND_NAME, hash_settings.output_hash_enabled, "bo_hash",
        (cfg != NULL) ? cfg->hash_report : "both");

    for (uint32_t frame = 0U; !db_should_stop(); frame++) {
        if ((frame_limit > 0U) && (frame >= frame_limit)) {
            break;
        }
        const uint64_t frame_start_ns = db_now_ns_monotonic();
        const double time_s = (double)frame / BENCH_TARGET_FPS_D;
        db_renderer_cpu_renderer_render_frame(time_s);

        const uint64_t state_hash = db_renderer_cpu_renderer_state_hash();
        db_display_hash_tracker_record(&state_hash_tracker, state_hash);

        uint32_t pixel_width = 0U;
        uint32_t pixel_height = 0U;
        const uint32_t *pixels =
            db_renderer_cpu_renderer_pixels_rgba8(&pixel_width, &pixel_height);
        if (pixels == NULL) {
            db_failf(BACKEND_NAME, "cpu renderer returned NULL framebuffer");
        }
        const uint64_t bo_hash = db_hash_rgba8_pixels_canonical(
            (const uint8_t *)pixels, pixel_width, pixel_height,
            (size_t)pixel_width * 4U, 0);
        db_display_hash_tracker_record(&bo_hash_tracker, bo_hash);

        frames++;
        const double elapsed_ms =
            ((double)frames * DB_MS_PER_SECOND_D) / BENCH_TARGET_FPS_D;
        db_benchmark_log_periodic(
            db_dispatch_api_name(DB_API_CPU), db_renderer_name_cpu(),
            BACKEND_NAME, frames, work_unit_count, elapsed_ms, capability_mode,
            &next_progress_log_due_ms, BENCH_LOG_INTERVAL_MS_D);
        db_sleep_to_fps_cap(BACKEND_NAME, frame_start_ns, fps_cap);
    }

    if (hash_settings.state_hash_enabled != 0) {
        const uint64_t final_hash = db_renderer_cpu_renderer_state_hash();
        state_hash_tracker.final_hash = final_hash;
    }

    uint32_t final_width = 0U;
    uint32_t final_height = 0U;
    const uint32_t *final_pixels =
        db_renderer_cpu_renderer_pixels_rgba8(&final_width, &final_height);
    if (final_pixels == NULL) {
        db_failf(BACKEND_NAME, "cpu renderer returned NULL framebuffer");
    }
    if (hash_settings.output_hash_enabled != 0) {
        bo_hash_tracker.final_hash = db_hash_rgba8_pixels_canonical(
            (const uint8_t *)final_pixels, final_width, final_height,
            (size_t)final_width * 4U, 0);
    }

    const double total_ms =
        ((double)frames * DB_MS_PER_SECOND_D) / BENCH_TARGET_FPS_D;
    db_benchmark_log_final(db_dispatch_api_name(DB_API_CPU),
                           db_renderer_name_cpu(), BACKEND_NAME, frames,
                           work_unit_count, total_ms, capability_mode);
    db_display_hash_tracker_log_final(BACKEND_NAME, &state_hash_tracker);
    db_display_hash_tracker_log_final(BACKEND_NAME, &bo_hash_tracker);
    db_renderer_cpu_renderer_shutdown();
    return EXIT_SUCCESS;
}

int db_run_offscreen(db_api_t api, db_gl_renderer_t renderer,
                     const db_cli_config_t *cfg) {
    if (db_dispatch_display_supports_api(DB_DISPLAY_OFFSCREEN, api) == 0) {
        db_failf(BACKEND_NAME,
                 "requested offscreen/API combination is unavailable in this "
                 "build (api=%d)",
                 (int)api);
    }

    if (api == DB_API_CPU) {
        return db_run_offscreen_cpu(cfg);
    }

#ifdef DB_HAS_GLFW
    db_cli_config_t glfw_cfg = (cfg != NULL) ? *cfg : (db_cli_config_t){0};
    glfw_cfg.offscreen_enabled = 1;
    return db_run_glfw_window(api, renderer, &glfw_cfg);
#else
    (void)renderer;
    db_failf(BACKEND_NAME, "offscreen %s requires GLFW support in this build",
             db_dispatch_api_name(api));
    return EXIT_FAILURE;
#endif
}
