#include "../../config/benchmark_config.h"
#include "../../core/db_core.h"
#include "../renderer_benchmark_runtime.h"
#include "../renderer_benchmark_types.h"
#include "../renderer_gl_api.h"
#include "../renderer_gl_common.h"
#include "../renderer_history_common.h"
#include "../renderer_snake_collect.h"
#include "../renderer_snake_shape_common.h"
#include "../renderer_snake_types.h"
#include "../renderer_viewport_common.h"
#include "renderer_opengl_gl1_5_gles1_1_internal.h"
#include "renderer_opengl_gl1_5_gles1_1_util.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

renderer_state_t g_gl1_state = {0};

void db_renderer_opengl_gl1_5_gles1_1_init(void) {
    db_gl_set_depth_test_enabled(0);
    db_gl_set_cull_face_enabled(0);
    db_gl_set_dither_enabled(0);

    g_state.is_es_context = db_gl_is_es_context(db_gl_get_version_string());
    g_state.vertex.vertex_stride = (g_state.is_es_context != 0)
                                       ? DB_ES_VERTEX_FLOAT_STRIDE
                                       : DB_VERTEX_FLOAT_STRIDE;
    db_benchmark_runtime_init_t runtime_state = {0};
    if (!db_init_benchmark_runtime_common(BACKEND_NAME, &runtime_state)) {
        failf("failed to initialize benchmark runtime");
    }
    const db_history_pattern_mode_flags_t runtime_flags =
        db_history_runtime_mode_flags(&runtime_state);
    const int use_snake_compact_only_init =
        (runtime_flags.is_snake_history_texture != 0);
    if ((use_snake_compact_only_init != 0)
            ? (!db_gl1_init_runtime_metadata_only(&runtime_state,
                                                  g_state.vertex.vertex_stride))
            : (!db_init_vertices_for_mode(&runtime_state,
                                          g_state.vertex.vertex_stride))) {
        failf("failed to allocate benchmark vertex buffers");
    }
    g_state.snake_replay.prev_draw_blocks = NULL;
    g_state.snake_replay.prev_draw_block_count = 0U;
    g_state.snake_replay.draw_block_capacity = 0U;
    g_state.snake_replay.replay_mode = DB_GL1_SNAKE_REPLAY_NONE;
    g_state.snake_scratch.damage.blocks = NULL;
    g_state.snake_scratch.damage.capacity = 0U;
    g_state.snake_scratch.compact.blocks = NULL;
    g_state.snake_scratch.compact.capacity = 0U;
    g_state.snake_scratch.shape.row_bounds = NULL;
    g_state.snake_scratch.shape.row_bounds_capacity = 0U;
    g_state.snake_color_state = NULL;
    g_state.snake_color_capacity = 0U;
    g_state.snake_shadow_rgba8 = NULL;
    g_state.snake_shadow_rgba16f = NULL;
    g_state.snake_shadow_pixel_capacity = 0U;
    g_state.snake_shadow_pixel_width = 0U;
    g_state.snake_shadow_pixel_height = 0U;
    g_state.snake_shadow_upload_blocks = NULL;
    g_state.snake_shadow_upload_block_capacity = 0U;
    g_state.snake_replay.prev_draw_blocks = NULL;
    g_state.snake_replay.prev_draw_block_count = 0U;
    g_state.snake_replay.draw_block_capacity = 0U;
    g_state.snake_replay.replay_mode = DB_GL1_SNAKE_REPLAY_NONE;
    g_state.snake_shadow_present = (db_gl_shadow_present_state_t){0};
    g_state.snake_shadow_backing_format = DB_GL_SHADOW_PRESENT_TEXTURE_RGBA8;
    g_state.snake_shadow_logged_fallback_mask = 0U;
    g_state.gradient_row_y_ndc = NULL;
    g_state.gradient_row_y_ndc_rows = 0U;
    g_state.gradient_row_y_ndc_viewport_h = 0;
    g_state.bands_x_cache_viewport_w = 0;
    g_state.bands_x_cache_cols = 0U;
    g_state.bands_x_cache_count = 0U;
    db_history_snake_backbuffer_state_reset(&g_state.snake_backbuffer_state, 0);
    g_state.runtime_flags = runtime_flags;
    if (g_state.runtime_flags.is_snake_history_texture != 0) {
        db_gl_shadow_present_init_runtime(&g_state.snake_shadow_present, 1, 1);
        g_state.snake_shadow_backing_format =
            g_state.snake_shadow_present.selected_texture_format;
        g_state.snake_shadow_present_logged = 0;
        db_history_snake_active_cache_init(&g_state.snake_scratch, BACKEND_NAME,
                                           BENCH_SNAKE_PHASE_WINDOW_TILES,
                                           DB_GL_COLOR_COMPONENT_COUNT);
        g_state.snake_color_capacity = (size_t)g_state.runtime.work_unit_count *
                                       DB_GL_COLOR_COMPONENT_COUNT;
        g_state.snake_color_state = (float *)db_alloc_array_or_fail(
            BACKEND_NAME, "snake_color_state", g_state.snake_color_capacity,
            sizeof(float));
        db_gl1_init_snake_color_state_from_vertices();
        const size_t snake_damage_block_capacity =
            db_snake_scratch_capacity_from_work_units(
                g_state.runtime.work_unit_count);
        const size_t snake_compact_rect_capacity =
            db_gl1_snake_compact_rect_capacity();
        const size_t snake_shadow_upload_block_capacity =
            (snake_damage_block_capacity > snake_compact_rect_capacity)
                ? snake_damage_block_capacity
                : snake_compact_rect_capacity;
        g_state.snake_scratch.damage.blocks =
            (db_grid_block_t *)db_alloc_array_or_fail(
                BACKEND_NAME, "snake_damage_blocks",
                snake_damage_block_capacity,
                sizeof(*g_state.snake_scratch.damage.blocks));
        if (snake_compact_rect_capacity > 0U) {
            g_state.snake_scratch.compact.blocks =
                (db_snake_compact_block_t *)db_alloc_array_or_fail(
                    BACKEND_NAME, "snake_compact_blocks",
                    snake_compact_rect_capacity,
                    sizeof(*g_state.snake_scratch.compact.blocks));
        } else {
            g_state.snake_scratch.compact.blocks = NULL;
        }
        g_state.snake_scratch.damage.capacity = snake_damage_block_capacity;
        g_state.snake_scratch.compact.capacity = snake_compact_rect_capacity;
        if (snake_compact_rect_capacity > 0U) {
            g_state.snake_replay.prev_draw_blocks =
                (db_snake_compact_block_t *)db_alloc_array_or_fail(
                    BACKEND_NAME, "snake_prev_draw_blocks",
                    snake_compact_rect_capacity,
                    sizeof(*g_state.snake_replay.prev_draw_blocks));
        } else {
            g_state.snake_replay.prev_draw_blocks = NULL;
        }
        g_state.snake_replay.draw_block_capacity = snake_compact_rect_capacity;
        g_state.snake_replay.replay_mode = DB_GL1_SNAKE_REPLAY_NONE;
        g_state.snake_shadow_upload_blocks =
            (db_damage_block_t *)db_alloc_array_or_fail(
                BACKEND_NAME, "snake_shadow_upload_blocks",
                snake_shadow_upload_block_capacity,
                sizeof(*g_state.snake_shadow_upload_blocks));
        g_state.snake_shadow_upload_block_capacity =
            snake_shadow_upload_block_capacity;
        if (g_state.runtime_flags.is_snake_shapes != 0) {
            g_state.snake_scratch.shape.row_bounds =
                (db_snake_shape_row_bounds_t *)db_alloc_array_or_fail(
                    BACKEND_NAME, "snake_row_bounds", db_grid_rows_effective(),
                    sizeof(*g_state.snake_scratch.shape.row_bounds));
            g_state.snake_scratch.shape.row_bounds_capacity =
                (size_t)db_grid_rows_effective();
        }
    }
    g_state.gradient_row_y_ndc = (float *)db_alloc_array_or_fail(
        BACKEND_NAME, "gradient_row_y_ndc",
        (size_t)db_grid_rows_effective() + 1U, sizeof(float));

    db_gl_upload_probe_result_t probe_result = {0};

    g_state.vertex.upload = (db_gl_upload_probe_result_t){0};
    g_state.buffers.vbo = 0U;
    g_state.backbuffer_valid = 0;
    db_history_gradient_replay_state_reset(&g_state.gradient_prev_frame);

    g_state.capability_mode[0] = '\0';
    db_gl1_refresh_capability_mode();

    db_gl_set_client_state_vertex_array_enabled(1);
    db_gl_set_client_state_color_array_enabled(1);
    g_state.client_arrays_configured = 0;
    g_state.vbo_arrays_configured = 0;
    const size_t full_mesh_bytes = (size_t)g_state.vertex.draw_vertex_count *
                                   g_state.vertex.vertex_stride * sizeof(float);
    const size_t compact_rect_capacity =
        (g_state.runtime_flags.is_snake_history_texture != 0)
            ? db_gl1_snake_compact_rect_capacity()
            : 0U;
    const size_t compact_only_vbo_bytes =
        compact_rect_capacity *
        db_rect_tile_bytes(g_state.vertex.vertex_stride);

    if ((g_state.runtime_flags.is_snake_history_texture != 0) &&
        (compact_only_vbo_bytes > 0U)) {
        db_gl_compact_vbo_init_standalone_or_fail(
            BACKEND_NAME, &g_state.compact_vbo, compact_only_vbo_bytes,
            g_state.vertex.vertex_stride);
        unsigned int vbo_u32 = 0U;
        if (db_gl_vbo_create_or_zero(&vbo_u32) != 0) {
            g_state.buffers.vbo = vbo_u32;
        }
        if (g_state.buffers.vbo != 0U) {
            if (db_gl_bind_array_buffer_cached(
                    g_state.buffers.vbo, &g_state.buffers.bound_array_buffer) ==
                0) {
                db_gl_vbo_delete_if_valid(g_state.buffers.vbo);
                g_state.buffers.vbo = 0U;
            }
        }
        if (g_state.buffers.vbo != 0U) {
            if (db_gl_vbo_init_data(compact_only_vbo_bytes, NULL,
                                    GL_DYNAMIC_DRAW) == 0) {
                db_gl_vbo_delete_if_valid(g_state.buffers.vbo);
                g_state.buffers.vbo = 0U;
            }
        }
        if (g_state.buffers.vbo != 0U) {
            db_gl_context_probe_upload_capabilities(
                compact_only_vbo_bytes,
                (const void *)g_state.compact_vbo.scratch_vertices,
                &probe_result);
            g_state.vertex.upload = probe_result;
        }
        if (g_state.buffers.vbo != 0U) {
            g_state.vbo_arrays_configured = 0;
            g_state.client_arrays_configured = 0;
            db_gl1_configure_vbo_arrays_if_needed();
            db_gl1_refresh_capability_mode();
            db_log_renderer_capability_mode(
                BACKEND_NAME,
                db_renderer_opengl_gl1_5_gles1_1_capability_mode());
            db_gl1_log_backbuffer_strategy();
            return;
        }
        db_gl_compact_vbo_free(&g_state.compact_vbo);
    } else {
        const size_t total_vbo_bytes =
            db_gl_compact_vbo_total_bytes(full_mesh_bytes);
        if (total_vbo_bytes == 0U) {
            failf("GL1 VBO size overflow");
        }
        unsigned int vbo_u32 = 0U;
        if (db_gl_vbo_create_or_zero(&vbo_u32) != 0) {
            g_state.buffers.vbo = vbo_u32;
        }
        if (g_state.buffers.vbo != 0U) {
            if (db_gl_bind_array_buffer_cached(
                    g_state.buffers.vbo, &g_state.buffers.bound_array_buffer) ==
                0) {
                db_gl_vbo_delete_if_valid(g_state.buffers.vbo);
                g_state.buffers.vbo = 0U;
            }
        }
        if (g_state.buffers.vbo != 0U) {
            if (db_gl_vbo_init_data(total_vbo_bytes, NULL, GL_DYNAMIC_DRAW) ==
                0) {
                db_gl_vbo_delete_if_valid(g_state.buffers.vbo);
                g_state.buffers.vbo = 0U;
            }
        }
        if (g_state.buffers.vbo != 0U) {
            db_gl_context_probe_upload_capabilities(
                full_mesh_bytes, (const void *)g_state.vertex.vertices,
                &probe_result);
            g_state.vertex.upload = probe_result;
            db_gl_upload_buffer(g_state.vertex.vertices, full_mesh_bytes,
                                g_state.vertex.upload.use_persistent_upload,
                                g_state.vertex.upload.persistent_mapped_ptr,
                                g_state.vertex.upload.use_map_range_upload,
                                g_state.vertex.upload.use_map_buffer_upload);
            db_gl_compact_vbo_init_or_fail(BACKEND_NAME, &g_state.compact_vbo,
                                           full_mesh_bytes,
                                           g_state.vertex.vertex_stride);
            g_state.vbo_arrays_configured = 0;
            db_gl1_configure_vbo_arrays_if_needed();
            db_gl1_refresh_capability_mode();
            db_log_renderer_capability_mode(
                BACKEND_NAME,
                db_renderer_opengl_gl1_5_gles1_1_capability_mode());
            db_gl1_log_backbuffer_strategy();
            return;
        }
    }

    if ((g_state.runtime_flags.is_snake_history_texture != 0) &&
        (g_state.compact_vbo.scratch_vertices == NULL)) {
        db_gl_compact_vbo_init_or_fail(BACKEND_NAME, &g_state.compact_vbo,
                                       compact_only_vbo_bytes,
                                       g_state.vertex.vertex_stride);
    }

    db_gl1_configure_client_arrays_if_needed();
    db_gl1_refresh_capability_mode();
    db_log_renderer_capability_mode(
        BACKEND_NAME, db_renderer_opengl_gl1_5_gles1_1_capability_mode());
    db_gl1_log_backbuffer_strategy();
}

void db_renderer_opengl_gl1_5_gles1_1_render_frame(
    uint32_t frame_index, int viewport_width_px, int viewport_height_px,
    uint32_t preserved_framebuffer_count) {
    const db_renderer_viewport_state_t viewport_state =
        db_renderer_resolve_viewport_state(BACKEND_NAME, &viewport_width_px,
                                           &viewport_height_px,
                                           &g_state.viewport.last_viewport_w,
                                           &g_state.viewport.last_viewport_h);

    if (viewport_state.viewport_changed != 0) {
        size_t previous_replay_marker =
            (g_state.snake_replay.replay_mode == DB_GL1_SNAKE_REPLAY_COMPACT)
                ? 1U
                : 0U;
        // Keep GL viewport in sync with drawable pixels (HiDPI-safe)
        // without querying GL state.
        db_gl_set_viewport_px(viewport_width_px, viewport_height_px);
        db_gl1_refresh_tile_positions_for_viewport(viewport_width_px,
                                                   viewport_height_px);
        db_gl1_refresh_gradient_row_ndc_cache(viewport_height_px);
        db_history_invalidate_snake_backbuffer_on_resize(
            preserved_framebuffer_count, &g_state.backbuffer_valid,
            &previous_replay_marker, &g_state.snake_backbuffer_state);
        g_state.snake_replay.replay_mode = DB_GL1_SNAKE_REPLAY_NONE;
        g_state.snake_replay.prev_draw_block_count = 0U;
        g_state.snake_shadow_present.backing_valid = 0;
        g_state.snake_shadow_present.texture_valid = 0;
        g_state.snake_shadow_present.texture_needs_full_upload = 1;
    }

    // When the display path advertises preserved backbuffer behavior, snake
    // dirty draws operate directly on the default framebuffer. Display-side
    // fallback can pass `preserved_framebuffer_count=0` to disable that
    // assumption.

    // Use a known viewport size without querying GL state.
    // Pixel-space ops must use drawable pixels; tile math stays on grid
    // cols/rows.
    const int viewport_w = viewport_state.viewport_width_px;
    const int viewport_h = viewport_state.viewport_height_px;
    const int has_viewport = viewport_state.has_viewport;
    if (g_state.runtime_flags.is_snake_history_texture != 0) {
        db_gl1_snake_frame_state_t snake_frame = {0};
        db_gl1_prepare_snake_frame_state(
            &snake_frame, preserved_framebuffer_count,
            g_state.runtime_flags.uses_dirty_backbuffer_mode, has_viewport);
        db_gl1_render_snake_draw_pass(
            &snake_frame, g_state.runtime_flags.uses_dirty_backbuffer_mode,
            viewport_w, viewport_h);
    } else if (g_state.runtime_flags.is_gradient != 0) {
        db_gl1_render_gradient_frame(viewport_w, viewport_h,
                                     preserved_framebuffer_count);
    } else if (g_state.runtime_flags.is_bands != 0) {
        db_gl1_draw_bands_compact(db_grid_cols_effective(), BENCH_BANDS,
                                  frame_index, viewport_w, viewport_h);
        db_history_record_draw_stats_for_work(&g_state.frame.full_draw_frames,
                                              &g_state.frame.dirty_draw_frames,
                                              1, 0, BENCH_BANDS);
    }
    db_history_finalize_frame(&g_state.frame, &g_state.runtime,
                              db_grid_cols_effective(),
                              db_grid_rows_effective());
}

void db_renderer_opengl_gl1_5_gles1_1_shutdown(void) {
    if (g_state.vertex.upload.persistent_mapped_ptr != NULL) {
        (void)db_gl_bind_array_buffer_cached(
            g_state.buffers.vbo, &g_state.buffers.bound_array_buffer);
        db_gl_unmap_current_array_buffer();
    }
    if (g_state.buffers.vbo != 0U) {
        db_gl_vbo_delete_if_valid(g_state.buffers.vbo);
        g_state.buffers.vbo = 0U;
    }
    db_gl_set_client_state_vertex_array_enabled(0);
    db_gl_set_client_state_color_array_enabled(0);
    free(g_state.snake_replay.prev_draw_blocks);
    db_history_snake_active_cache_free(&g_state.snake_scratch);
    free(g_state.snake_scratch.damage.blocks);
    free(g_state.snake_scratch.compact.blocks);
    free(g_state.snake_scratch.shape.row_bounds);
    free(g_state.gradient_row_y_ndc);
    free(g_state.snake_color_state);
    free(g_state.snake_shadow_rgba8);
    free(g_state.snake_shadow_rgba16f);
    free(g_state.snake_shadow_upload_blocks);
    db_gl_shadow_present_shutdown(&g_state.snake_shadow_present);
    db_gl_compact_vbo_free(&g_state.compact_vbo);
    free(g_state.vertex.vertices);
    g_state = (renderer_state_t){0};
}

const char *db_renderer_opengl_gl1_5_gles1_1_capability_mode(void) {
    if (g_state.capability_mode[0] == '\0') {
        db_gl1_refresh_capability_mode();
    }
    return g_state.capability_mode;
}

uint32_t db_renderer_opengl_gl1_5_gles1_1_work_unit_count(void) {
    return db_runtime_work_unit_count(&g_state.runtime, 1);
}

uint64_t db_renderer_opengl_gl1_5_gles1_1_state_hash(void) {
    return g_state.frame.state_hash;
}

void db_renderer_opengl_gl1_5_gles1_1_draw_stats(uint64_t *full_draw_frames,
                                                 uint64_t *dirty_draw_frames) {
    db_history_copy_draw_stats(&g_state.frame, full_draw_frames,
                               dirty_draw_frames);
    if ((g_state.runtime_flags.is_snake_history_texture != 0) &&
        (g_state.runtime_flags.uses_dirty_backbuffer_mode != 0)) {
        db_infof(
            BACKEND_NAME,
            "snake dirty_mode_stats: compact_success_frames=%llu "
            "shadow_fallback_frames=%llu full_recovery_frames=%llu "
            "compact_attempt_frames=%llu requested_backbuffer_draw_full=%d",
            (unsigned long long)g_state.snake_compact_health.success_frames,
            (unsigned long long)g_state.snake_compact_health.fallback_frames,
            (unsigned long long)
                g_state.snake_compact_health.full_recovery_frames,
            (unsigned long long)g_state.snake_compact_health.attempt_frames,
            g_state.runtime.backbuffer_draw_full);
        db_infof(
            BACKEND_NAME,
            "snake shadow_sync_stats: backing_incremental_frames=%llu "
            "backing_rebuild_frames=%llu texture_partial_upload_frames=%llu "
            "texture_full_upload_frames=%llu recovery_from_shadow_frames=%llu",
            (unsigned long long)
                g_state.snake_shadow_stats.backing_incremental_frames,
            (unsigned long long)
                g_state.snake_shadow_stats.backing_rebuild_frames,
            (unsigned long long)
                g_state.snake_shadow_stats.texture_partial_upload_frames,
            (unsigned long long)
                g_state.snake_shadow_stats.texture_full_upload_frames,
            (unsigned long long)
                g_state.snake_shadow_stats.recovery_from_shadow_frames);
    }
}
