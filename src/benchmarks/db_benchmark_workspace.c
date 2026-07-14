#include "db_benchmark_core.h"

#include "benchmarks/db_benchmark_checkpoint_internal.h"
#include "benchmarks/db_benchmark_mode_runtime_internal.h"
#include "benchmarks/db_benchmark_runtime.h"
#include "benchmarks/db_benchmark_runtime_internal.h"
#include "benchmarks/db_snake_progression_internal.h"
#include "benchmarks/db_snake_shape_internal.h"
#include "benchmarks/db_snake_types_internal.h"
#include "core/db_core.h"
#include "core/db_geometry.h"
#include "core/db_render_ir.h"
#include "core/db_render_types.h"

#include <stddef.h>
#include <stdlib.h>

enum {
    DB_BENCHMARK_IR_COMMAND_BYTES = 4096U,
    DB_BENCHMARK_IR_RESOURCE_CAPACITY = 2U,
};

static void allocate_ir_store(db_render_ir_store_t *store, const char *label,
                              size_t fill_capacity) {
    const size_t command_elements =
        DB_BENCHMARK_IR_COMMAND_BYTES / sizeof(max_align_t);
    *store = (db_render_ir_store_t){
        .commands = (max_align_t *)db_malloc_or_fail(
            "benchmark_core", label, command_elements, sizeof(max_align_t)),
        .command_capacity = DB_BENCHMARK_IR_COMMAND_BYTES,
        .fills = (db_render_ir_fill_t *)db_malloc_or_fail(
            "benchmark_core", "ir_fills", fill_capacity,
            sizeof(db_render_ir_fill_t)),
        .fill_capacity = fill_capacity,
        .resources = (db_render_ir_resource_t *)db_malloc_or_fail(
            "benchmark_core", "ir_resources", DB_BENCHMARK_IR_RESOURCE_CAPACITY,
            sizeof(db_render_ir_resource_t)),
        .resource_capacity = DB_BENCHMARK_IR_RESOURCE_CAPACITY,
        .regions = (db_render_ir_region_t *)db_malloc_or_fail(
            "benchmark_core", "ir_regions", fill_capacity,
            sizeof(db_render_ir_region_t)),
        .region_capacity = fill_capacity,
        .bands = (db_render_ir_band_t *)db_malloc_or_fail(
            "benchmark_core", "ir_bands", fill_capacity,
            sizeof(db_render_ir_band_t)),
        .band_capacity = fill_capacity,
        .spans = (db_render_ir_span_t *)db_malloc_or_fail(
            "benchmark_core", "ir_spans", fill_capacity,
            sizeof(db_render_ir_span_t)),
        .span_capacity = fill_capacity,
    };
}

static void free_ir_store(db_render_ir_store_t *store) {
    if (store == NULL) {
        return;
    }
    free(store->commands);
    free(store->fills);
    free(store->resources);
    free(store->regions);
    free(store->bands);
    free(store->spans);
    *store = (db_render_ir_store_t){0};
}
void db_benchmark_core_init(db_benchmark_core_t *core,
                            const db_benchmark_runtime_init_t *init_state,
                            db_pixel_format_t working_format) {
    if (core == NULL || init_state == NULL) {
        return;
    }
    *core = (db_benchmark_core_t){0};
    core->runtime = *init_state;
    core->runtime_flags = db_benchmark_mode_runtime_flags(&core->runtime);
    core->working_format = working_format;
    core->initialized = 1;

    core->geometry.capacity = DB_CANONICAL_GEOMETRY_BLOCK_CAPACITY;
    core->geometry.logical_blocks = (db_grid_block_t *)db_malloc_or_fail(
        "benchmark_core", "emitted_logical_blocks", core->geometry.capacity,
        sizeof(*core->geometry.logical_blocks));
    core->geometry.rebuild_logical_blocks =
        (db_grid_block_t *)db_malloc_or_fail(
            "benchmark_core", "rebuild_logical_blocks", core->geometry.capacity,
            sizeof(*core->geometry.rebuild_logical_blocks));
    core->ir.fill_capacity = core->geometry.capacity;
    allocate_ir_store(&core->ir.raw, "raw_ir_commands", core->ir.fill_capacity);
    allocate_ir_store(&core->ir.optimized, "optimized_ir_commands",
                      core->ir.fill_capacity);
    allocate_ir_store(&core->ir.rebuild, "rebuild_ir_commands",
                      core->ir.fill_capacity);
    core->ir.optimizer_primary = (db_render_ir_fill_t *)db_malloc_or_fail(
        "benchmark_core", "ir_optimizer_primary", core->ir.fill_capacity,
        sizeof(*core->ir.optimizer_primary));
    core->ir.optimizer_secondary = (db_render_ir_fill_t *)db_malloc_or_fail(
        "benchmark_core", "ir_optimizer_secondary", core->ir.fill_capacity,
        sizeof(*core->ir.optimizer_secondary));
    core->ir.rebuild_fills = (db_render_ir_fill_t *)db_malloc_or_fail(
        "benchmark_core", "ir_rebuild_fills", core->ir.fill_capacity,
        sizeof(*core->ir.rebuild_fills));
    core->ir.rebuild_status = DB_RENDER_IR_OK;

    if (core->runtime_flags.pattern.is_snake != 0) {
        db_snake_progression_workspace_init(
            &core->snake.progression, "benchmark_core",
            DB_SNAKE_OPTIMIZER_COMPACT_BLOCK_LIMIT);
        if (core->runtime_flags.pattern.is_snake_shapes != 0) {
            core->snake.progression.shape.row_bounds =
                (db_snake_shape_row_bounds_t *)db_malloc_or_fail(
                    "benchmark_core", "snake_shape_row_bounds",
                    db_grid_rows_effective(),
                    sizeof(*core->snake.progression.shape.row_bounds));
            core->snake.progression.shape.row_bounds_capacity =
                db_checked_u32_to_size("benchmark_core",
                                       "snake_shape_row_bounds_capacity",
                                       db_grid_rows_effective());
        }

        core->snake.progression.damage.blocks =
            (db_grid_block_t *)db_malloc_or_fail(
                "benchmark_core", "snake_damage_blocks",
                DB_SNAKE_OPTIMIZER_COMPACT_BLOCK_LIMIT,
                sizeof(*core->snake.progression.damage.blocks));
        core->snake.progression.damage.capacity =
            DB_SNAKE_OPTIMIZER_COMPACT_BLOCK_LIMIT;
    }
}
void db_benchmark_core_shutdown(db_benchmark_core_t *core) {
    if (core == NULL || core->initialized == 0) {
        return;
    }
    if (core->runtime_flags.pattern.is_snake != 0) {
        free(core->snake.progression.damage.blocks);
        free(core->snake.progression.shape.row_bounds);
        db_snake_progression_workspace_free(&core->snake.progression);
    }
    db_benchmark_checkpoint_shutdown(&core->checkpoint);
    free(core->geometry.logical_blocks);
    free(core->geometry.rebuild_logical_blocks);
    free_ir_store(&core->ir.raw);
    free_ir_store(&core->ir.optimized);
    free_ir_store(&core->ir.rebuild);
    free(core->ir.optimizer_primary);
    free(core->ir.optimizer_secondary);
    free(core->ir.rebuild_fills);
    core->geometry.logical_blocks = NULL;
    core->geometry.rebuild_logical_blocks = NULL;
    core->geometry.capacity = 0U;
    core->ir = (db_benchmark_ir_workspace_t){0};
    core->initialized = 0;
}
