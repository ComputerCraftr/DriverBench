#include "support/test_harness.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "benchmarks/db_benchmark_checkpoint_internal.h"
#include "benchmarks/db_benchmark_core.h"
#include "benchmarks/db_benchmark_mode_flags.h"
#include "benchmarks/db_benchmark_runtime.h"
#include "benchmarks/db_benchmark_runtime_internal.h"
#include "benchmarks/db_benchmark_types_internal.h"
#include "benchmarks/db_snake_collect_internal.h"
#include "benchmarks/db_snake_shape_internal.h"
#include "config/benchmark_config.h"
#include "core/db_benchmark_model.h"
#include "core/db_frame_plan.h"
#include "core/db_hash.h"
#include "core/db_numeric.h"
#include "core/db_render_ir.h"
#include "core/db_render_ir_surface.h"
#include "core/db_render_result.h"
#include "core/db_render_types.h"

enum {
    DB_TEST_CHECKPOINT_FRAME_COUNT = 96U,
    DB_TEST_SNAKE_BOUNDARY_TICK_COUNT = 4U,
    DB_TEST_SNAKE_BOUNDARY_OFFSET = 2U,
    DB_TEST_SNAKE_PRIOR_START_OFFSET = 66U,
};

static db_frame_plan_status_t
db_test_benchmark_generate_plan(db_benchmark_core_t *core, uint32_t frame_index,
                                const db_frame_plan_request_t *request,
                                db_frame_plan_t *plan) {
    db_frame_requirements_t requirements = {0};
    db_frame_plan_status_t status =
        db_benchmark_core_probe_frame(core, frame_index, &requirements);
    if ((status == DB_FRAME_PLAN_CHECKPOINT_REQUIRED) ||
        ((status == DB_FRAME_PLAN_OK) &&
         (requirements.checkpoint_required != 0))) {
        db_frame_checkpoint_binding_t binding = {0};
        status = db_benchmark_core_provision_requirements(core, &requirements,
                                                          &binding);
    }
    if (status != DB_FRAME_PLAN_OK) {
        return status;
    }
    return db_benchmark_core_generate_plan(core, frame_index, request, plan);
}

static void
db_test_seeding_returns_gray_for_all_snakes(db_test_state_t *state) {
    db_benchmark_runtime_init_t runtime = {0};
    double rgb[3];

    // Snake Grid Phase 0
    runtime.pattern = DB_PATTERN_SNAKE_GRID;
    runtime.snake.grid_phase_flag = 0;
    db_benchmark_seed_background_color_rgb3(&runtime, rgb);
    DB_TEST_EXPECT_DOUBLE_EQUAL(state, rgb[0], BENCH_GRID_PHASE0_R);

    // Snake Grid Phase 1 (returns Phase 1 color if flag is set)
    runtime.snake.grid_phase_flag = 1;
    db_benchmark_seed_background_color_rgb3(&runtime, rgb);
    DB_TEST_EXPECT_DOUBLE_EQUAL(state, rgb[0], BENCH_GRID_PHASE1_R);

    // Snake Rect (should return gray Phase 0 even if it doesn't alternate)
    runtime.pattern = DB_PATTERN_SNAKE_RECT;
    db_benchmark_seed_background_color_rgb3(&runtime, rgb);
    DB_TEST_EXPECT_DOUBLE_EQUAL(state, rgb[0], BENCH_GRID_PHASE0_R);

    // Snake Shapes (should return gray Phase 0)
    runtime.pattern = DB_PATTERN_SNAKE_SHAPES;
    db_benchmark_seed_background_color_rgb3(&runtime, rgb);
    DB_TEST_EXPECT_DOUBLE_EQUAL(state, rgb[0], BENCH_GRID_PHASE0_R);
}

static void db_test_pattern_mode_flags_correct(db_test_state_t *state) {
    db_pattern_mode_flags_t flags;

    flags = db_pattern_mode_flags(DB_PATTERN_SNAKE_GRID);
    DB_TEST_EXPECT_TRUE(state, flags.is_snake != 0);
    DB_TEST_EXPECT_EQ_INT(state, flags.is_snake_region_mode, 0);

    flags = db_pattern_mode_flags(DB_PATTERN_SNAKE_RECT);
    DB_TEST_EXPECT_TRUE(state, flags.is_snake != 0);
    DB_TEST_EXPECT_TRUE(state, flags.is_snake_region_mode != 0);

    flags = db_pattern_mode_flags(DB_PATTERN_BANDS);
    DB_TEST_EXPECT_TRUE(state, flags.is_bands != 0);
    DB_TEST_EXPECT_EQ_INT(state, flags.is_snake, 0);
}

static void
db_test_benchmark_core_plan_owns_expected_state_hash(db_test_state_t *state) {
    db_benchmark_runtime_init_t runtime = {0};
    DB_TEST_EXPECT_TRUE(
        state, db_init_benchmark_runtime_from_options(
                   "test",
                   &(const db_benchmark_runtime_options_t){
                       .benchmark_mode_text = DB_BENCHMARK_MODE_SNAKE_GRID,
                       .bench_speed_text = "1",
                       .random_seed_text = "123456",
                       .backbuffer_draw_full = 1,
                   },
                   &runtime) != 0);

    db_benchmark_core_t core = {0};
    db_benchmark_core_init(&core, &runtime, DB_PIXEL_FORMAT_RGBA16F);
    db_frame_plan_t plan = {0};
    db_test_benchmark_generate_plan(&core, 0U, NULL, &plan);
    DB_TEST_EXPECT_TRUE(state, plan.rebuild_metadata.instance_count > 0U);
    int found_seed_color = 0;
    db_render_ir_rect_iterator_t iterator = {0};
    db_render_ir_rect_iterator_begin(&iterator, &plan.rebuild_ir);
    db_render_ir_fill_t fill = {0};
    while (db_render_ir_rect_iterator_next(&iterator, &fill) != 0) {
        const double value = fill.color.rgba[0];
        if ((value <= BENCH_GRID_PHASE0_R) && (value >= BENCH_GRID_PHASE0_R)) {
            found_seed_color = 1;
        }
    }
    DB_TEST_EXPECT_TRUE(state, found_seed_color != 0);
    DB_TEST_EXPECT_EQ_U32(state, plan.grid_cols, db_grid_cols_effective());
    DB_TEST_EXPECT_EQ_U32(state, plan.grid_rows, db_grid_rows_effective());
    DB_TEST_EXPECT_TRUE(
        state,
        plan.expected_state_hash ==
            db_benchmark_runtime_state_hash_cross_renderer(
                &core.pending_runtime, plan.pixel_width, plan.pixel_height));

    db_benchmark_core_shutdown(&core);
}

static void
db_test_snake_grid_speed_batches_match_single_ticks(db_test_state_t *state) {
    static const uint32_t single_frame_count = 40U;
    static const uint32_t batch_frame_count = 2U;
    db_benchmark_runtime_init_t single_runtime = {0};
    db_benchmark_runtime_init_t batch_runtime = {0};
    const db_benchmark_runtime_options_t single_options = {
        .benchmark_mode_text = DB_BENCHMARK_MODE_SNAKE_GRID,
        .bench_speed_text = "1",
        .random_seed_text = "123456",
        .backbuffer_draw_full = 1,
    };
    const db_benchmark_runtime_options_t batch_options = {
        .benchmark_mode_text = DB_BENCHMARK_MODE_SNAKE_GRID,
        .bench_speed_text = "20",
        .random_seed_text = "123456",
        .backbuffer_draw_full = 1,
    };
    DB_TEST_EXPECT_TRUE(
        state, db_init_benchmark_runtime_from_options("test", &single_options,
                                                      &single_runtime) != 0);
    DB_TEST_EXPECT_TRUE(
        state, db_init_benchmark_runtime_from_options("test", &batch_options,
                                                      &batch_runtime) != 0);

    db_benchmark_core_t single_core = {0};
    db_benchmark_core_t batch_core = {0};
    db_benchmark_core_init(&single_core, &single_runtime,
                           DB_PIXEL_FORMAT_RGBA16F);
    db_benchmark_core_init(&batch_core, &batch_runtime,
                           DB_PIXEL_FORMAT_RGBA16F);
    for (uint32_t frame = 0U; frame < single_frame_count; frame++) {
        db_frame_plan_t plan = {0};
        db_test_benchmark_generate_plan(&single_core, frame, NULL, &plan);
        db_benchmark_core_apply_plan(&single_core, &plan,
                                     &(const db_render_result_t){.success = 1});
    }
    for (uint32_t frame = 0U; frame < batch_frame_count; frame++) {
        db_frame_plan_t plan = {0};
        db_test_benchmark_generate_plan(&batch_core, frame, NULL, &plan);
        DB_TEST_EXPECT_EQ_U32(state, plan.simulation_tick_count, 20U);
        DB_TEST_EXPECT_EQ_U32(state, plan.simulation_chunk_count, 1U);
        DB_TEST_EXPECT_EQ_U32(state, plan.simulation_boundary_count, 0U);
        DB_TEST_EXPECT_TRUE(state, plan.simulation_terminal_item_count <= 64U);
        db_benchmark_core_apply_plan(&batch_core, &plan,
                                     &(const db_render_result_t){.success = 1});
    }

    DB_TEST_EXPECT_EQ_U32(state, single_core.runtime.snake.cursor,
                          batch_core.runtime.snake.cursor);
    DB_TEST_EXPECT_EQ_U32(state, single_core.runtime.snake.prev_start,
                          batch_core.runtime.snake.prev_start);
    DB_TEST_EXPECT_EQ_U32(state, single_core.runtime.snake.prev_count,
                          batch_core.runtime.snake.prev_count);
    const db_frame_plan_request_t rebuild_request = {.force_rebuild = 1};
    db_frame_plan_t single_rebuild = {0};
    db_frame_plan_t batch_rebuild = {0};
    db_test_benchmark_generate_plan(&single_core, single_frame_count,
                                    &rebuild_request, &single_rebuild);
    db_test_benchmark_generate_plan(&batch_core, batch_frame_count,
                                    &rebuild_request, &batch_rebuild);
    DB_TEST_EXPECT_EQ_U32(state, single_rebuild.rebuild_metadata.instance_count,
                          batch_rebuild.rebuild_metadata.instance_count);

    db_benchmark_core_shutdown(&single_core);
    db_benchmark_core_shutdown(&batch_core);
}

static void
db_test_snake_grid_batch_continues_after_phase(db_test_state_t *state) {
    db_benchmark_runtime_init_t single_runtime = {0};
    db_benchmark_runtime_init_t batch_runtime = {0};
    DB_TEST_EXPECT_TRUE(
        state, db_init_benchmark_runtime_from_options(
                   "test",
                   &(const db_benchmark_runtime_options_t){
                       .benchmark_mode_text = DB_BENCHMARK_MODE_SNAKE_GRID,
                       .bench_speed_text = "1",
                       .random_seed_text = "123456",
                   },
                   &single_runtime) != 0);
    batch_runtime = single_runtime;
    batch_runtime.bench_speed_step = DB_TEST_SNAKE_BOUNDARY_TICK_COUNT;
    const uint32_t target_count = single_runtime.work_unit_count;
    single_runtime.snake.cursor = target_count - DB_TEST_SNAKE_BOUNDARY_OFFSET;
    single_runtime.snake.prev_start =
        target_count - DB_TEST_SNAKE_PRIOR_START_OFFSET;
    single_runtime.snake.prev_count = 64U;
    batch_runtime.snake = single_runtime.snake;

    db_benchmark_core_t single_core = {0};
    db_benchmark_core_t batch_core = {0};
    db_benchmark_core_init(&single_core, &single_runtime,
                           DB_PIXEL_FORMAT_RGBA16F);
    db_benchmark_core_init(&batch_core, &batch_runtime,
                           DB_PIXEL_FORMAT_RGBA16F);
    for (uint32_t frame = 0U; frame < DB_TEST_SNAKE_BOUNDARY_TICK_COUNT;
         frame++) {
        db_frame_plan_t plan = {0};
        db_test_benchmark_generate_plan(&single_core, frame, NULL, &plan);
        db_benchmark_core_apply_plan(&single_core, &plan,
                                     &(const db_render_result_t){.success = 1});
    }

    db_frame_plan_t batch_plan = {0};
    db_test_benchmark_generate_plan(&batch_core, 0U, NULL, &batch_plan);
    DB_TEST_EXPECT_EQ_U32(state, batch_plan.simulation_tick_count,
                          DB_TEST_SNAKE_BOUNDARY_TICK_COUNT);
    DB_TEST_EXPECT_EQ_U32(state, batch_plan.simulation_chunk_count, 3U);
    DB_TEST_EXPECT_EQ_U32(state, batch_plan.simulation_boundary_count, 1U);
    DB_TEST_EXPECT_EQ_U32(state, batch_core.pending_runtime.snake.cursor,
                          single_core.runtime.snake.cursor);
    DB_TEST_EXPECT_EQ_INT(state,
                          batch_core.pending_runtime.snake.grid_phase_flag,
                          single_core.runtime.snake.grid_phase_flag);
    DB_TEST_EXPECT_EQ_U32(state, batch_core.pending_runtime.snake.cursor, 1U);
    DB_TEST_EXPECT_TRUE(state, batch_plan.update_metadata.instance_count > 1U);

    db_benchmark_core_shutdown(&single_core);
    db_benchmark_core_shutdown(&batch_core);
}

static void
db_test_snake_rect_batch_preserves_completed_rect(db_test_state_t *state) {
    db_benchmark_runtime_init_t single_runtime = {0};
    DB_TEST_EXPECT_TRUE(
        state, db_init_benchmark_runtime_from_options(
                   "test",
                   &(const db_benchmark_runtime_options_t){
                       .benchmark_mode_text = DB_BENCHMARK_MODE_SNAKE_RECT,
                       .bench_speed_text = "1",
                       .random_seed_text = "123456",
                   },
                   &single_runtime) != 0);
    const db_snake_region_t first_region =
        db_snake_region_from_index(single_runtime.pattern_seed, 0U);
    const uint32_t target_count = first_region.width * first_region.height;
    single_runtime.snake.cursor = target_count - DB_TEST_SNAKE_BOUNDARY_OFFSET;
    single_runtime.snake.prev_start =
        target_count - DB_TEST_SNAKE_PRIOR_START_OFFSET;
    single_runtime.snake.prev_count = 64U;
    db_benchmark_runtime_init_t batch_runtime = single_runtime;
    batch_runtime.bench_speed_step = DB_TEST_SNAKE_BOUNDARY_TICK_COUNT;

    db_benchmark_core_t single_core = {0};
    db_benchmark_core_t batch_core = {0};
    db_benchmark_core_init(&single_core, &single_runtime,
                           DB_PIXEL_FORMAT_RGBA8);
    db_benchmark_core_init(&batch_core, &batch_runtime, DB_PIXEL_FORMAT_RGBA8);
    for (uint32_t frame = 0U; frame < DB_TEST_SNAKE_BOUNDARY_TICK_COUNT;
         frame++) {
        db_frame_plan_t plan = {0};
        db_test_benchmark_generate_plan(&single_core, frame, NULL, &plan);
        db_benchmark_core_apply_plan(&single_core, &plan,
                                     &(const db_render_result_t){.success = 1});
    }

    db_frame_plan_t batch_plan = {0};
    db_test_benchmark_generate_plan(&batch_core, 0U, NULL, &batch_plan);
    db_benchmark_core_apply_plan(&batch_core, &batch_plan,
                                 &(const db_render_result_t){.success = 1});
    DB_TEST_EXPECT_EQ_U32(state, batch_plan.simulation_tick_count,
                          DB_TEST_SNAKE_BOUNDARY_TICK_COUNT);
    DB_TEST_EXPECT_EQ_U32(state, batch_plan.simulation_chunk_count, 3U);
    DB_TEST_EXPECT_EQ_U32(state, batch_plan.simulation_boundary_count, 1U);
    DB_TEST_EXPECT_EQ_U32(state, batch_core.runtime.snake.shape_index,
                          single_core.runtime.snake.shape_index);
    DB_TEST_EXPECT_EQ_U32(state, batch_core.runtime.snake.cursor,
                          single_core.runtime.snake.cursor);
    DB_TEST_EXPECT_TRUE(state,
                        memcmp(batch_core.checkpoint.surface.pixels,
                               single_core.checkpoint.surface.pixels,
                               batch_core.checkpoint.surface_size_bytes) == 0);

    db_benchmark_core_shutdown(&single_core);
    db_benchmark_core_shutdown(&batch_core);
}

static void
db_test_forced_rebuild_uses_authoritative_geometry(db_test_state_t *state) {
    db_benchmark_runtime_init_t runtime = {0};
    const db_benchmark_runtime_options_t options = {
        .benchmark_mode_text = DB_BENCHMARK_MODE_SNAKE_GRID,
        .bench_speed_text = "1",
        .random_seed_text = "123456",
        .backbuffer_draw_full = 0,
    };
    DB_TEST_EXPECT_TRUE(state, db_init_benchmark_runtime_from_options(
                                   "test", &options, &runtime) != 0);
    db_benchmark_core_t core = {0};
    db_benchmark_core_init(&core, &runtime, DB_PIXEL_FORMAT_RGBA16F);
    db_frame_plan_t startup = {0};
    db_test_benchmark_generate_plan(&core, 0U, NULL, &startup);
    db_benchmark_core_apply_plan(&core, &startup,
                                 &(const db_render_result_t){.success = 1});

    const db_frame_plan_request_t request = {
        .pixel_width = 2000U,
        .pixel_height = 1200U,
        .force_rebuild = 1,
    };
    db_frame_plan_t rebuild = {0};
    db_test_benchmark_generate_plan(&core, 1U, &request, &rebuild);
    DB_TEST_EXPECT_EQ_U32(state, rebuild.pixel_width, request.pixel_width);
    DB_TEST_EXPECT_EQ_U32(state, rebuild.pixel_height, request.pixel_height);
    DB_TEST_EXPECT_TRUE(state, rebuild.rebuild_required != 0);
    DB_TEST_EXPECT_EQ_INT(state, rebuild.rebuild_reason,
                          DB_FRAME_REBUILD_EXPLICIT);
    DB_TEST_EXPECT_TRUE(state, rebuild.rebuild_ir.command_count > 0U);
    DB_TEST_EXPECT_TRUE(
        state,
        rebuild.expected_state_hash ==
            db_benchmark_runtime_state_hash_cross_renderer(
                &core.pending_runtime, rebuild.grid_cols, rebuild.grid_rows));
    db_benchmark_core_shutdown(&core);
}

static void db_test_benchmark_model_commits_only_successful_results(
    db_test_state_t *state) {
    db_benchmark_runtime_init_t runtime = {0};
    const db_benchmark_runtime_options_t options = {
        .benchmark_mode_text = DB_BENCHMARK_MODE_SNAKE_GRID,
        .bench_speed_text = "1",
        .random_seed_text = "123456",
    };
    DB_TEST_EXPECT_TRUE(state, db_init_benchmark_runtime_from_options(
                                   "test", &options, &runtime) != 0);
    db_benchmark_model_t model = {0};
    DB_TEST_EXPECT_TRUE(
        state, db_benchmark_model_init(
                   &model, &(const db_benchmark_model_config_t){
                               .benchmark_configuration = &runtime,
                               .working_format = DB_PIXEL_FORMAT_RGBA16F,
                           }) != 0);

    db_frame_plan_t first = {0};
    (void)db_benchmark_model_generate(&model, 0U, NULL, &first);
    db_benchmark_model_commit(&model, &first,
                              &(const db_render_result_t){.success = 0});
    db_frame_plan_t retry = {0};
    (void)db_benchmark_model_generate(&model, 0U, NULL, &retry);
    DB_TEST_EXPECT_TRUE(state,
                        retry.expected_state_hash == first.expected_state_hash);

    db_benchmark_model_commit(&model, &retry,
                              &(const db_render_result_t){.success = 1});
    db_frame_plan_t next = {0};
    (void)db_benchmark_model_generate(&model, 1U, NULL, &next);
    DB_TEST_EXPECT_TRUE(state,
                        next.expected_state_hash != retry.expected_state_hash);
    db_benchmark_model_shutdown(&model);
}

static db_benchmark_runtime_init_t
db_test_overlapping_runtime(db_test_state_t *state, const char *mode) {
    db_benchmark_runtime_init_t runtime = {0};
    DB_TEST_EXPECT_TRUE(state, db_init_benchmark_runtime_from_options(
                                   "test",
                                   &(const db_benchmark_runtime_options_t){
                                       .benchmark_mode_text = mode,
                                       .bench_speed_text = "1024",
                                       .random_seed_text = "123456",
                                       .backbuffer_draw_full = 0,
                                   },
                                   &runtime) != 0);
    return runtime;
}

static void
db_test_snake_fast_forward_is_boundary_bounded(db_test_state_t *state) {
    static const char *const modes[] = {
        DB_BENCHMARK_MODE_SNAKE_GRID,
        DB_BENCHMARK_MODE_SNAKE_RECT,
        DB_BENCHMARK_MODE_SNAKE_SHAPES,
    };
    for (size_t index = 0U; index < sizeof(modes) / sizeof(modes[0]); index++) {
        db_benchmark_runtime_init_t runtime =
            db_test_overlapping_runtime(state, modes[index]);
        db_benchmark_core_t core = {0};
        db_benchmark_core_init(&core, &runtime, DB_PIXEL_FORMAT_RGBA16F);
        db_frame_plan_t plan = {0};
        db_test_benchmark_generate_plan(&core, 0U, NULL, &plan);
        DB_TEST_EXPECT_EQ_U32(state, plan.simulation_tick_count, 1024U);
        DB_TEST_EXPECT_TRUE(state, plan.simulation_chunk_count <
                                       plan.simulation_tick_count);
        DB_TEST_EXPECT_TRUE(state,
                            plan.simulation_chunk_count <=
                                (plan.simulation_boundary_count * 2U) + 1U);
        DB_TEST_EXPECT_TRUE(state, plan.simulation_terminal_item_count <= 64U);
        db_benchmark_core_shutdown(&core);
    }
}

static void
db_test_checkpoint_scope_and_working_format(db_test_state_t *state) {
    db_benchmark_runtime_init_t shapes =
        db_test_overlapping_runtime(state, DB_BENCHMARK_MODE_SNAKE_SHAPES);
    db_benchmark_core_t shape_core = {0};
    db_benchmark_core_init(&shape_core, &shapes, DB_PIXEL_FORMAT_RGBA16F);
    DB_TEST_EXPECT_EQ_INT(state, shape_core.checkpoint.enabled, 0);
    db_frame_plan_t shape_plan = {0};
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_test_benchmark_generate_plan(&shape_core, 0U, NULL, &shape_plan),
        DB_FRAME_PLAN_OK);
    DB_TEST_EXPECT_TRUE(state, shape_core.checkpoint.enabled != 0);
    DB_TEST_EXPECT_EQ_INT(state, shape_core.checkpoint.surface.format,
                          DB_PIXEL_FORMAT_RGBA16F);
    const size_t pixel_count =
        (size_t)db_grid_cols_effective() * db_grid_rows_effective();
    const size_t dirty_span_extent =
        pixel_count + (size_t)db_grid_rows_effective();
    const size_t dirty_span_capacity =
        (dirty_span_extent / 2U) + (dirty_span_extent % 2U);
    DB_TEST_EXPECT_EQ_SIZE(
        state, shape_core.checkpoint.allocation_size_bytes,
        (2U * pixel_count * DB_RGBA16F_BYTES_PER_PIXEL) +
            (pixel_count * sizeof(uint32_t)) +
            (dirty_span_capacity * sizeof(db_benchmark_checkpoint_span_t)));
    db_benchmark_core_shutdown(&shape_core);

    db_benchmark_runtime_init_t grid =
        db_test_overlapping_runtime(state, DB_BENCHMARK_MODE_SNAKE_GRID);
    db_benchmark_core_t grid_core = {0};
    db_benchmark_core_init(&grid_core, &grid, DB_PIXEL_FORMAT_RGBA16F);
    DB_TEST_EXPECT_EQ_INT(state, grid_core.checkpoint.enabled, 0);
    db_frame_plan_t grid_plan = {0};
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_test_benchmark_generate_plan(&grid_core, 0U, NULL, &grid_plan),
        DB_FRAME_PLAN_OK);
    DB_TEST_EXPECT_EQ_INT(state, grid_core.checkpoint.enabled, 0);
    db_benchmark_core_shutdown(&grid_core);

    db_benchmark_runtime_init_t gradient = {0};
    DB_TEST_EXPECT_TRUE(
        state, db_init_benchmark_runtime_from_options(
                   "test",
                   &(const db_benchmark_runtime_options_t){
                       .benchmark_mode_text = DB_BENCHMARK_MODE_GRADIENT_FILL,
                       .bench_speed_text = "1",
                       .random_seed_text = "123456",
                   },
                   &gradient) != 0);
    db_benchmark_core_t gradient_core = {0};
    db_benchmark_core_init(&gradient_core, &gradient, DB_PIXEL_FORMAT_RGBA16F);
    for (uint32_t frame = 0U; frame < DB_TEST_CHECKPOINT_FRAME_COUNT; frame++) {
        db_frame_plan_t gradient_plan = {0};
        DB_TEST_EXPECT_EQ_INT(state,
                              db_test_benchmark_generate_plan(
                                  &gradient_core, frame, NULL, &gradient_plan),
                              DB_FRAME_PLAN_OK);
        db_benchmark_core_apply_plan(&gradient_core, &gradient_plan,
                                     &(const db_render_result_t){.success = 1});
        DB_TEST_EXPECT_EQ_INT(state, gradient_core.checkpoint.enabled, 0);
    }
    db_benchmark_core_shutdown(&gradient_core);
}

static void db_test_apply_plan_to_surface(const db_frame_plan_t *plan,
                                          db_pixel_surface_t *surface) {
    if ((plan->rebuild_required != 0) &&
        (plan->rebuild_ir.command_count > 0U)) {
        (void)db_render_ir_rasterize_surface_with_bindings(
            &plan->rebuild_ir, plan->external_bindings, plan->grid_cols,
            plan->grid_rows, surface);
    }
    (void)db_render_ir_rasterize_surface(&plan->update_ir, plan->grid_cols,
                                         plan->grid_rows, surface);
}

typedef struct {
    uint64_t state_hash;
    uint64_t framebuffer_hash;
} db_test_equal_work_result_t;

static db_test_equal_work_result_t
db_test_run_equal_work_schedule(db_test_state_t *state,
                                const char *benchmark_mode, const char *speed,
                                uint32_t frame_count) {
    db_benchmark_runtime_init_t runtime = {0};
    DB_TEST_EXPECT_TRUE(state, db_init_benchmark_runtime_from_options(
                                   "test",
                                   &(const db_benchmark_runtime_options_t){
                                       .benchmark_mode_text = benchmark_mode,
                                       .bench_speed_text = speed,
                                       .random_seed_text = "123456",
                                       .backbuffer_draw_full = 1,
                                   },
                                   &runtime) != 0);
    db_benchmark_core_t core = {0};
    db_benchmark_core_init(&core, &runtime, DB_PIXEL_FORMAT_RGBA16F);
    db_pixel_surface_t surface = {
        .pixel_width = db_grid_cols_effective(),
        .pixel_height = db_grid_rows_effective(),
        .format = DB_PIXEL_FORMAT_RGBA16F,
    };
    const size_t stride_bytes =
        (size_t)surface.pixel_width * db_pixel_surface_pixel_bytes(&surface);
    const size_t allocation_size = stride_bytes * (size_t)surface.pixel_height;
    surface.pixels = calloc(1U, allocation_size);
    DB_TEST_EXPECT_TRUE(state, surface.pixels != NULL);

    db_test_equal_work_result_t result = {0};
    for (uint32_t frame = 0U; frame < frame_count; frame++) {
        db_frame_plan_t plan = {0};
        db_test_benchmark_generate_plan(&core, frame, NULL, &plan);
        db_test_apply_plan_to_surface(&plan, &surface);
        result.state_hash = plan.expected_state_hash;
        db_benchmark_core_apply_plan(&core, &plan,
                                     &(const db_render_result_t){.success = 1});
    }
    const db_pixel_surface_t *const authoritative_surface =
        (core.checkpoint.enabled != 0) ? &core.checkpoint.surface : &surface;
    if (authoritative_surface->pixels != NULL) {
        const size_t authoritative_stride =
            (size_t)authoritative_surface->pixel_width *
            db_pixel_surface_pixel_bytes(authoritative_surface);
        result.framebuffer_hash = db_hash_working_rgba8(
            authoritative_surface->pixels, authoritative_surface->format,
            authoritative_surface->pixel_width,
            authoritative_surface->pixel_height, authoritative_stride, 0);
    }
    free(surface.pixels);
    db_benchmark_core_shutdown(&core);
    return result;
}

static void
db_test_all_benchmarks_are_equal_work_associative(db_test_state_t *state) {
    static const char *const benchmarks[] = {
        DB_BENCHMARK_MODE_BANDS,          DB_BENCHMARK_MODE_GRADIENT_FILL,
        DB_BENCHMARK_MODE_GRADIENT_SWEEP, DB_BENCHMARK_MODE_SNAKE_GRID,
        DB_BENCHMARK_MODE_SNAKE_RECT,     DB_BENCHMARK_MODE_SNAKE_SHAPES,
    };
    for (size_t index = 0U; index < sizeof(benchmarks) / sizeof(benchmarks[0]);
         index++) {
        const db_test_equal_work_result_t single =
            db_test_run_equal_work_schedule(state, benchmarks[index], "1", 40U);
        const db_test_equal_work_result_t batched =
            db_test_run_equal_work_schedule(state, benchmarks[index], "20", 2U);
        const db_test_equal_work_result_t one_frame =
            db_test_run_equal_work_schedule(state, benchmarks[index], "40", 1U);
        DB_TEST_EXPECT_EQ_U64(state, single.state_hash, batched.state_hash);
        DB_TEST_EXPECT_EQ_U64(state, single.state_hash, one_frame.state_hash);
        DB_TEST_EXPECT_EQ_U64(state, single.framebuffer_hash,
                              batched.framebuffer_hash);
        DB_TEST_EXPECT_EQ_U64(state, single.framebuffer_hash,
                              one_frame.framebuffer_hash);
    }
}

static void
db_test_checkpoint_rebuild_matches_incremental(db_test_state_t *state) {
    db_benchmark_runtime_init_t runtime =
        db_test_overlapping_runtime(state, DB_BENCHMARK_MODE_SNAKE_SHAPES);
    db_benchmark_core_t core = {0};
    db_benchmark_core_init(&core, &runtime, DB_PIXEL_FORMAT_RGBA16F);
    db_frame_plan_t initial_plan = {0};
    DB_TEST_EXPECT_EQ_INT(
        state, db_test_benchmark_generate_plan(&core, 0U, NULL, &initial_plan),
        DB_FRAME_PLAN_OK);
    const size_t byte_count = core.checkpoint.surface_size_bytes;
    db_pixel_surface_t reference = core.checkpoint.surface;
    reference.pixels = malloc(byte_count);
    DB_TEST_EXPECT_TRUE(state, reference.pixels != NULL);
    memcpy(reference.pixels, core.checkpoint.surface.pixels, byte_count);
    void *const allocation = core.checkpoint.surface.pixels;

    for (uint32_t frame = 0U; frame < DB_TEST_CHECKPOINT_FRAME_COUNT; frame++) {
        const int force_rebuild = DB_BOOL((frame % 17U) == 0U);
        const db_frame_plan_request_t request = {
            .force_rebuild = force_rebuild,
            .rebuild_reason = force_rebuild != 0 ? DB_FRAME_REBUILD_EXPLICIT
                                                 : DB_FRAME_REBUILD_NONE,
        };
        db_frame_plan_t plan = {0};
        db_test_benchmark_generate_plan(&core, frame, &request, &plan);
        if (force_rebuild != 0) {
            DB_TEST_EXPECT_EQ_SIZE(state, plan.external_bindings.count, 1U);
        }
        db_test_apply_plan_to_surface(&plan, &reference);
        const uint64_t expected_hash =
            db_hash_working_rgba8(reference.pixels, reference.format,
                                  reference.pixel_width, reference.pixel_height,
                                  (size_t)reference.pixel_width *
                                      db_pixel_surface_pixel_bytes(&reference),
                                  0);
        db_benchmark_core_apply_plan(&core, &plan,
                                     &(const db_render_result_t){
                                         .success = 1,
                                         .working_hash = expected_hash,
                                         .working_hash_valid = 1,
                                     });
        if (force_rebuild != 0) {
            DB_TEST_EXPECT_TRUE(state, memcmp(reference.pixels,
                                              core.checkpoint.surface.pixels,
                                              byte_count) == 0);
        }
        DB_TEST_EXPECT_TRUE(state,
                            core.checkpoint.surface.pixels == allocation);
        DB_TEST_EXPECT_EQ_SIZE(state, core.checkpoint.surface_size_bytes,
                               byte_count);
    }
    free(reference.pixels);
    db_benchmark_core_shutdown(&core);
}

static void
db_test_snake_rect_checkpoint_allocation_is_stable(db_test_state_t *state) {
    db_benchmark_runtime_init_t runtime =
        db_test_overlapping_runtime(state, DB_BENCHMARK_MODE_SNAKE_RECT);
    db_benchmark_core_t core = {0};
    db_benchmark_core_init(&core, &runtime, DB_PIXEL_FORMAT_RGBA8);
    void *allocation = NULL;
    size_t allocation_bytes = 0U;
    for (uint32_t frame = 0U; frame < DB_TEST_CHECKPOINT_FRAME_COUNT; frame++) {
        db_frame_plan_t plan = {0};
        DB_TEST_EXPECT_EQ_INT(
            state, db_test_benchmark_generate_plan(&core, frame, NULL, &plan),
            DB_FRAME_PLAN_OK);
        if (frame == 0U) {
            allocation = core.checkpoint.surface.pixels;
            allocation_bytes = core.checkpoint.allocation_size_bytes;
        }
        db_benchmark_core_apply_plan(&core, &plan,
                                     &(const db_render_result_t){.success = 1});
        DB_TEST_EXPECT_TRUE(state,
                            core.checkpoint.surface.pixels == allocation);
        DB_TEST_EXPECT_EQ_SIZE(state, core.checkpoint.allocation_size_bytes,
                               allocation_bytes);
        DB_TEST_EXPECT_TRUE(state, allocation_bytes <=
                                       DB_BENCHMARK_CHECKPOINT_MAX_BYTES);
    }
    db_benchmark_core_shutdown(&core);
}

static void
db_test_gradient_rebuild_is_not_steady_state(db_test_state_t *state) {
    db_benchmark_runtime_init_t runtime = {0};
    DB_TEST_EXPECT_TRUE(
        state, db_init_benchmark_runtime_from_options(
                   "test",
                   &(const db_benchmark_runtime_options_t){
                       .benchmark_mode_text = DB_BENCHMARK_MODE_GRADIENT_FILL,
                       .bench_speed_text = "1",
                       .random_seed_text = "123456",
                   },
                   &runtime) != 0);
    db_benchmark_core_t core = {0};
    db_benchmark_core_init(&core, &runtime, DB_PIXEL_FORMAT_RGBA16F);
    db_frame_plan_t startup = {0};
    db_test_benchmark_generate_plan(&core, 0U, NULL, &startup);
    DB_TEST_EXPECT_TRUE(state, startup.rebuild_required != 0);
    DB_TEST_EXPECT_TRUE(state, startup.rebuild_ir.command_count > 0U);
    db_benchmark_core_apply_plan(&core, &startup,
                                 &(const db_render_result_t){.success = 1});

    db_frame_plan_t incremental = {0};
    db_test_benchmark_generate_plan(&core, 1U, NULL, &incremental);
    DB_TEST_EXPECT_EQ_INT(state, incremental.rebuild_required, 0);
    DB_TEST_EXPECT_EQ_U32(state, incremental.rebuild_ir.command_count, 0U);
    DB_TEST_EXPECT_TRUE(state, incremental.update_metadata.instance_count > 0U);
    db_benchmark_core_shutdown(&core);
}

unsigned db_benchmark_seeding_test_run_all(void) {
    static const db_test_case_t cases[] = {
        {"all_benchmarks_are_equal_work_associative",
         db_test_all_benchmarks_are_equal_work_associative},
        {"seeding_gray_snakes", db_test_seeding_returns_gray_for_all_snakes},
        {"pattern_mode_flags", db_test_pattern_mode_flags_correct},
        {"benchmark_core_plan_owns_expected_state_hash",
         db_test_benchmark_core_plan_owns_expected_state_hash},
        {"snake_grid_speed_batches_match_single_ticks",
         db_test_snake_grid_speed_batches_match_single_ticks},
        {"snake_grid_batch_continues_after_phase",
         db_test_snake_grid_batch_continues_after_phase},
        {"snake_rect_batch_preserves_completed_rect",
         db_test_snake_rect_batch_preserves_completed_rect},
        {"snake_fast_forward_is_boundary_bounded",
         db_test_snake_fast_forward_is_boundary_bounded},
        {"forced_rebuild_uses_authoritative_geometry",
         db_test_forced_rebuild_uses_authoritative_geometry},
        {"benchmark_model_commits_only_successful_results",
         db_test_benchmark_model_commits_only_successful_results},
        {"checkpoint_scope_and_working_format",
         db_test_checkpoint_scope_and_working_format},
        {"checkpoint_rebuild_matches_incremental",
         db_test_checkpoint_rebuild_matches_incremental},
        {"snake_rect_checkpoint_allocation_is_stable",
         db_test_snake_rect_checkpoint_allocation_is_stable},
        {"gradient_rebuild_is_not_steady_state",
         db_test_gradient_rebuild_is_not_steady_state},
    };
    return db_test_run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}
