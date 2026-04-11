#include "cpu_renderer.h"
#include "core/db_format_contract.h"
#include "core/db_log.h"
#include "core/db_renderer_runtime_contract.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../core/db_alloc_policy.h"
#include "../../core/db_core.h"
#include "../../core/db_frame_plan.h"
#include "../../core/db_geometry.h"
#include "../../core/db_numeric.h"
#include "../damage_trace.h"
#include "core/db_render_result.h"
#include "core/db_render_types.h"
#include "core/db_renderer_support.h"

#define BACKEND_NAME "renderer_cpu_renderer"
#define DB_CAP_MODE_CPU_SURFACE "cpu_surface"
#define DB_CAP_MODE_CPU_SURFACE_HDR "cpu_surface_hdr_rgba16f"

typedef struct {
    db_damage_block_t *damage_blocks;
    size_t damage_block_capacity;
    size_t damage_block_count;
    void *replace_surface_pixels;
    size_t replace_surface_pixel_capacity;
    int replace_surface_valid;
} db_cpu_surface_workspace_t;

typedef struct {
    db_renderer_execution_config_t runtime;
    int target_uses_rgba16f;
} db_cpu_renderer_config_t;

typedef struct {
    db_cpu_surface_workspace_t workspace;
    db_cpu_renderer_config_t config;
    db_renderer_frame_stats_t frame;
    int initialized;
} db_cpu_renderer_state_t;

static db_cpu_renderer_state_t g_state = {0};

static uint32_t db_cpu_surface_pixel_bytes(const db_pixel_surface_t *surface) {
    return (uint32_t)db_pixel_surface_pixel_bytes(surface);
}

static int db_cpu_surface_is_valid(const db_pixel_surface_t *surface) {
    return DB_BOOL((surface != NULL) && (surface->pixels != NULL) &&
                   (surface->pixel_width > 0U) && (surface->pixel_height > 0U));
}

static void
cpu_validate_render_surface_or_fail(const db_pixel_surface_t *surface) {
    if (db_cpu_surface_is_valid(surface) == 0) {
        DB_RUNTIME_FAIL(BACKEND_NAME, "invalid cpu render target surface");
    }
    if (surface->pixel_width != g_state.config.runtime.grid_cols) {
        DB_RUNTIME_FAIL(BACKEND_NAME,
                        "cpu render surface width mismatch: got=%u expected=%u",
                        surface->pixel_width, g_state.config.runtime.grid_cols);
    }
    if (surface->pixel_height != g_state.config.runtime.grid_rows) {
        DB_RUNTIME_FAIL(
            BACKEND_NAME,
            "cpu render surface height mismatch: got=%u expected=%u",
            surface->pixel_height, g_state.config.runtime.grid_rows);
    }
    const int surface_is_hdr = db_pixel_surface_uses_rgba16f(surface);
    if (surface_is_hdr != g_state.config.target_uses_rgba16f) {
        DB_RUNTIME_FAIL(
            BACKEND_NAME,
            "cpu render surface format mismatch: got_hdr=%d expected_hdr=%d",
            surface_is_hdr, g_state.config.target_uses_rgba16f);
    }
}

static void cpu_surface_fill_damage_block_rgb(
    const db_pixel_surface_t *surface, uint32_t row_start, uint32_t row_count,
    uint32_t col_start, uint32_t col_count, const double *rgb) {
    if ((surface == NULL) || (rgb == NULL) ||
        (row_start >= surface->pixel_height) || (row_count == 0U) ||
        (col_count == 0U)) {
        return;
    }
    if (col_start >= surface->pixel_width) {
        return;
    }
    db_rgb_pixels_fill_damage_block_f64(
        surface->pixel_width, surface->pixel_height, surface->pixels,
        surface->format, row_start, row_count, col_start, col_count, rgb);
}

static void
cpu_fill_surface_seed_background(const db_pixel_surface_t *surface) {
    db_rgb_pixels_fill_solid_f64(surface->pixel_width, surface->pixel_height,
                                 surface->pixels, surface->format,
                                 g_state.config.runtime.seed_rgba_f64);
}

static db_pixel_surface_t
db_cpu_replace_surface_or_fail(const db_pixel_surface_t *surface) {
    if (surface == NULL) {
        DB_RUNTIME_FAIL(BACKEND_NAME, "missing replace surface");
    }
    const size_t pixel_capacity =
        db_checked_mul_size(BACKEND_NAME, "replace_surface_pixel_capacity",
                            db_checked_u32_to_size(BACKEND_NAME, "pixel_width",
                                                   surface->pixel_width),
                            db_checked_u32_to_size(BACKEND_NAME, "pixel_height",
                                                   surface->pixel_height));
    if (pixel_capacity == 0U) {
        DB_RUNTIME_FAIL(BACKEND_NAME, "invalid replace surface capacity");
    }
    if (g_state.config.target_uses_rgba16f != 0) {
        const size_t channel_capacity = db_checked_mul_size(
            BACKEND_NAME, "replace_surface_channel_capacity", pixel_capacity,
            DB_RGBA16F_CHANNELS_PER_PIXEL);
        db_reserve_array_capacity_or_fail(
            &g_state.workspace.replace_surface_pixels,
            &g_state.workspace.replace_surface_pixel_capacity, channel_capacity,
            channel_capacity, sizeof(uint16_t), BACKEND_NAME,
            "replace_surface_rgba16f");
        return (db_pixel_surface_t){
            .pixel_width = surface->pixel_width,
            .pixel_height = surface->pixel_height,
            .pixels = g_state.workspace.replace_surface_pixels,
            .format = DB_PIXEL_FORMAT_RGBA16F,
        };
    }
    db_reserve_array_capacity_or_fail(
        &g_state.workspace.replace_surface_pixels,
        &g_state.workspace.replace_surface_pixel_capacity, pixel_capacity,
        pixel_capacity, sizeof(uint32_t), BACKEND_NAME,
        "replace_surface_rgba8");
    return (db_pixel_surface_t){
        .pixel_width = surface->pixel_width,
        .pixel_height = surface->pixel_height,
        .pixels = g_state.workspace.replace_surface_pixels,
        .format = DB_PIXEL_FORMAT_RGBA8,
    };
}

static void db_cpu_copy_surface_or_fail(const db_pixel_surface_t *dst,
                                        const db_pixel_surface_t *src) {
    if ((dst == NULL) || (src == NULL) ||
        (dst->pixel_width != src->pixel_width) ||
        (dst->pixel_height != src->pixel_height) ||
        (dst->format != src->format)) {
        DB_RUNTIME_FAIL(BACKEND_NAME, "invalid surface copy request");
    }
    if ((dst->pixels == NULL) || (src->pixels == NULL)) {
        DB_RUNTIME_FAIL(BACKEND_NAME, "missing surface pixels");
    }
    const size_t copy_bytes = (size_t)src->pixel_width *
                              (size_t)src->pixel_height *
                              (size_t)db_cpu_surface_pixel_bytes(src);
    memcpy(dst->pixels, src->pixels, copy_bytes);
}

static void db_cpu_set_full_damage(const db_pixel_surface_t *surface) {
    if ((surface == NULL) || (g_state.workspace.damage_blocks == NULL) ||
        (g_state.workspace.damage_block_capacity == 0U) ||
        (surface->pixel_height == 0U) || (surface->pixel_width == 0U)) {
        g_state.workspace.damage_block_count = 0U;
        return;
    }
    g_state.workspace.damage_blocks[0] = (db_damage_block_t){
        .row_start = 0U,
        .row_count = surface->pixel_height,
        .col_start = 0U,
        .col_count = surface->pixel_width,
    };
    g_state.workspace.damage_block_count = 1U;
}

static void cpu_publish_grid_blocks(const db_grid_block_t *blocks,
                                    size_t block_count) {
    if ((blocks == NULL) || (block_count == 0U)) {
        g_state.workspace.damage_block_count = 0U;
        return;
    }
    if ((g_state.workspace.damage_blocks == NULL) ||
        (g_state.workspace.damage_block_capacity == 0U)) {
        g_state.workspace.damage_block_count = 0U;
        return;
    }
    g_state.workspace.damage_block_count =
        db_damage_blocks_from_grid_blocks_or_full(
            blocks, block_count, g_state.config.runtime.grid_rows,
            g_state.config.runtime.grid_cols, g_state.workspace.damage_blocks,
            g_state.workspace.damage_block_capacity);
}

static void
db_cpu_apply_plan_colored_blocks(const db_pixel_surface_t *surface,
                                 db_colored_f64_block_view_t colored_view) {
    if ((surface == NULL) || (colored_view.blocks == NULL)) {
        return;
    }
    const uint32_t cols = g_state.config.runtime.grid_cols;
    const uint32_t rows = g_state.config.runtime.grid_rows;
    for (size_t block_index = 0U; block_index < colored_view.count;
         block_index++) {
        const db_colored_f64_block_t *const block =
            &colored_view.blocks[block_index];
        const db_grid_block_t grid_block = {
            .row_start = block->row_start,
            .row_count = block->row_count,
            .col_start = block->col_start,
            .col_count = block->col_count,
        };
        db_damage_block_t pixel_block = {0};
        if (db_grid_block_to_pixel_block(
                cols, rows, &grid_block, surface->pixel_width,
                surface->pixel_height, &pixel_block) == 0) {
            continue;
        }
        cpu_surface_fill_damage_block_rgb(
            surface, pixel_block.row_start, pixel_block.row_count,
            pixel_block.col_start, pixel_block.col_count, block->rgb);
    }
}

void db_cpu_init(const db_renderer_runtime_contract_t *resolved_runtime) {
    if (g_state.initialized != 0) {
        return;
    }
    if (resolved_runtime == NULL) {
        DB_RUNTIME_FAIL(BACKEND_NAME, "missing resolved runtime");
    }
    const db_renderer_execution_config_t init_state =
        resolved_runtime->execution;

    db_damage_block_t *damage_blocks = NULL;
    const size_t damage_block_capacity = DB_CANONICAL_GEOMETRY_BLOCK_CAPACITY;
    damage_blocks = (db_damage_block_t *)db_malloc_or_fail(
        BACKEND_NAME, "damage_blocks", damage_block_capacity,
        sizeof(*damage_blocks));

    g_state = (db_cpu_renderer_state_t){0};
    g_state.initialized = 1;
    g_state.config.target_uses_rgba16f = db_pixel_format_uses_rgba16f(
        resolved_runtime->format.surface_pixel_format);
    g_state.config.runtime = init_state;
    g_state.workspace.damage_blocks = damage_blocks;
    g_state.workspace.damage_block_capacity = damage_block_capacity;
}

const db_damage_block_t *db_cpu_render_frame_to_surface_mode(
    const db_frame_plan_t *plan, const db_pixel_surface_t *surface,
    db_cpu_render_target_mode_t target_mode, size_t *out_damage_count) {
    if (out_damage_count != NULL) {
        *out_damage_count = 0U;
    }
    if (g_state.initialized == 0 || plan == NULL) {
        return NULL;
    }

    cpu_validate_render_surface_or_fail(surface);
    db_damage_trace_emit_frame_plan(DB_DAMAGE_TRACE_BACKEND_CPU, "cpu_surface",
                                    1U, plan);
    g_state.workspace.damage_block_count = 0U;
    const int use_replace_surface =
        DB_BOOL(target_mode == DB_CPU_RENDER_TARGET_REPLACE_SURFACE);
    db_pixel_surface_t replace_surface = {0};
    const db_pixel_surface_t *render_surface = surface;
    if (use_replace_surface != 0) {
        replace_surface = db_cpu_replace_surface_or_fail(surface);
        render_surface = &replace_surface;
        if (g_state.workspace.replace_surface_valid == 0) {
            cpu_fill_surface_seed_background(render_surface);
            g_state.workspace.replace_surface_valid = 1;
        }
    } else if (plan->frame_index == 0U) {
        cpu_fill_surface_seed_background(surface);
        db_cpu_set_full_damage(surface);
    }

    if (plan->geometry_overflowed != 0) {
        DB_RUNTIME_FAIL(BACKEND_NAME, "canonical geometry emitter overflow");
    }
    const int rebuild =
        DB_BOOL((plan->frame_index == 0U) || (plan->rebuild_required != 0));
    const int use_raster_seed =
        DB_BOOL((rebuild != 0) &&
                (plan->rebuild_seed.kind == DB_FRAME_REBUILD_SEED_RASTER));
    if (use_raster_seed != 0) {
        db_cpu_copy_surface_or_fail(render_surface, &plan->rebuild_seed.raster);
        db_cpu_apply_plan_colored_blocks(render_surface,
                                         plan->geometry.current_blocks);
    } else {
        const int use_rebuild_geometry =
            DB_BOOL((rebuild != 0) && (plan->rebuild_seed.kind ==
                                       DB_FRAME_REBUILD_SEED_GEOMETRY));
        db_cpu_apply_plan_colored_blocks(render_surface,
                                         use_rebuild_geometry != 0
                                             ? plan->rebuild_seed.geometry
                                             : plan->geometry.current_blocks);
    }
    if ((use_replace_surface != 0) || (plan->rebuild_required != 0) ||
        (plan->frame_index == 0U)) {
        db_cpu_set_full_damage(render_surface);
    } else {
        cpu_publish_grid_blocks(plan->geometry.logical_damage.blocks,
                                plan->geometry.logical_damage.count);
    }

    if (use_replace_surface != 0) {
        db_cpu_copy_surface_or_fail(surface, render_surface);
        db_cpu_set_full_damage(surface);
    }

    g_state.frame.state_hash = plan->expected_state_hash;
    g_state.frame.frame_index++;
    if (out_damage_count != NULL) {
        *out_damage_count = g_state.workspace.damage_block_count;
    }
    if (db_damage_trace_enabled() != 0) {
        const size_t pixel_bytes = db_pixel_surface_pixel_bytes(surface);
        size_t bytes_touched = 0U;
        for (size_t index = 0U; index < g_state.workspace.damage_block_count;
             index++) {
            const db_damage_block_t block =
                g_state.workspace.damage_blocks[index];
            const size_t block_pixels =
                db_checked_mul_size(BACKEND_NAME, "trace_damage_pixels",
                                    block.col_count, block.row_count);
            bytes_touched += db_checked_mul_size(
                BACKEND_NAME, "trace_damage_bytes", block_pixels, pixel_bytes);
        }
        (void)db_damage_trace_emit_grid(
            &(const db_damage_trace_event_t){
                .frame_index = plan->frame_index,
                .backend = DB_DAMAGE_TRACE_BACKEND_CPU,
                .stage = DB_DAMAGE_TRACE_STAGE_LOGICAL,
                .operation = DB_DAMAGE_TRACE_OP_COPY,
                .source = DB_DAMAGE_TRACE_BUFFER_LOGICAL_PLAN,
                .destination = DB_DAMAGE_TRACE_BUFFER_LOGICAL_PLAN,
                .space = DB_DAMAGE_TRACE_SPACE_GRID,
                .width = g_state.config.runtime.grid_cols,
                .height = g_state.config.runtime.grid_rows,
                .pixel_format = surface->format,
                .result = DB_DAMAGE_TRACE_RESULT_EXECUTED,
                .target = "cpu_surface",
                .target_generation = 1U,
            },
            plan->geometry.logical_damage.blocks,
            plan->geometry.logical_damage.count);
        (void)db_damage_trace_emit(&(const db_damage_trace_event_t){
            .frame_index = plan->frame_index,
            .backend = DB_DAMAGE_TRACE_BACKEND_CPU,
            .stage = DB_DAMAGE_TRACE_STAGE_NORMALIZED,
            .operation = DB_DAMAGE_TRACE_OP_COPY,
            .source = DB_DAMAGE_TRACE_BUFFER_LOGICAL_PLAN,
            .destination = DB_DAMAGE_TRACE_BUFFER_CPU_SURFACE,
            .space = DB_DAMAGE_TRACE_SPACE_PIXEL,
            .width = surface->pixel_width,
            .height = surface->pixel_height,
            .pixel_format = surface->format,
            .blocks = g_state.workspace.damage_blocks,
            .block_count = g_state.workspace.damage_block_count,
            .result = DB_DAMAGE_TRACE_RESULT_EXECUTED,
            .target = "cpu_surface",
            .target_generation = 1U,
        });
        (void)db_damage_trace_emit(&(const db_damage_trace_event_t){
            .frame_index = plan->frame_index,
            .backend = DB_DAMAGE_TRACE_BACKEND_CPU,
            .stage = DB_DAMAGE_TRACE_STAGE_RENDERER_WRITE,
            .operation = (plan->frame_index == 0U)
                             ? DB_DAMAGE_TRACE_OP_SEED
                             : DB_DAMAGE_TRACE_OP_INCREMENTAL,
            .source = DB_DAMAGE_TRACE_BUFFER_LOGICAL_PLAN,
            .destination = DB_DAMAGE_TRACE_BUFFER_CPU_SURFACE,
            .space = DB_DAMAGE_TRACE_SPACE_PIXEL,
            .width = surface->pixel_width,
            .height = surface->pixel_height,
            .pixel_format = surface->format,
            .row_stride_bytes =
                db_checked_mul_size(BACKEND_NAME, "trace_stride",
                                    surface->pixel_width, pixel_bytes),
            .blocks = g_state.workspace.damage_blocks,
            .block_count = g_state.workspace.damage_block_count,
            .transfer_size_bytes = bytes_touched,
            .destination_hash = db_damage_trace_surface_hash(surface),
            .result = DB_DAMAGE_TRACE_RESULT_EXECUTED,
            .target = "cpu_surface",
            .target_generation = 1U,
        });
    }
    return g_state.workspace.damage_blocks;
}

const db_damage_block_t *
db_cpu_render_frame_to_surface(const db_frame_plan_t *plan,
                               const db_pixel_surface_t *surface,
                               size_t *out_damage_count) {
    return db_cpu_render_frame_to_surface_mode(
        plan, surface, DB_CPU_RENDER_TARGET_PRESERVED_SURFACE,
        out_damage_count);
}

uint32_t db_cpu_work_unit_count(void) {
    return (g_state.initialized != 0) ? g_state.config.runtime.work_unit_count
                                      : 0U;
}

const char *db_cpu_capability_mode(void) {
    if (g_state.config.target_uses_rgba16f != 0) {
        return DB_CAP_MODE_CPU_SURFACE_HDR;
    }
    return DB_CAP_MODE_CPU_SURFACE;
}

uint64_t db_cpu_state_hash(void) { return g_state.frame.state_hash; }

void db_cpu_shutdown(void) {
    if (g_state.initialized == 0) {
        return;
    }
    free(g_state.workspace.damage_blocks);
    free(g_state.workspace.replace_surface_pixels);
    g_state = (db_cpu_renderer_state_t){0};
}
