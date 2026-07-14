#include "renderers/opengl_gl1_5_gles1_1/gl1_internal.h"
#include "support/test_harness.h"

#include "core/db_frame_plan.h"
#include "core/db_render_ir.h"
#include "core/db_render_types.h"
#include "core/db_replay_policy.h"

#include <stddef.h>
#include <stdint.h>

enum {
    TEST_COMMAND_BYTES = 1024,
    TEST_CAPACITY = 16,
    TEST_WIDTH = 100,
    TEST_HEIGHT = 80,
};

typedef struct {
    max_align_t commands[TEST_COMMAND_BYTES / sizeof(max_align_t)];
    db_render_ir_fill_t fills[TEST_CAPACITY];
    db_render_ir_resource_t resources[2];
    db_render_ir_region_t regions[TEST_CAPACITY];
    db_render_ir_band_t bands[TEST_CAPACITY];
    db_render_ir_span_t spans[TEST_CAPACITY];
    db_render_ir_store_t store;
} replay_ir_fixture_t;

static void init_ir(replay_ir_fixture_t *fixture, uint32_t frame) {
    *fixture = (replay_ir_fixture_t){0};
    fixture->store = (db_render_ir_store_t){
        .commands = fixture->commands,
        .command_capacity = sizeof(fixture->commands),
        .fills = fixture->fills,
        .fill_capacity = TEST_CAPACITY,
        .resources = fixture->resources,
        .resource_capacity = 2U,
        .regions = fixture->regions,
        .region_capacity = TEST_CAPACITY,
        .bands = fixture->bands,
        .band_capacity = TEST_CAPACITY,
        .spans = fixture->spans,
        .span_capacity = TEST_CAPACITY,
    };
    db_render_ir_resource_id_t target = DB_RENDER_IR_INVALID_ID;
    (void)db_render_ir_add_resource(
        &fixture->store,
        &(const db_render_ir_resource_t){
            .kind = DB_RENDER_IR_RESOURCE_CANONICAL_TARGET,
            .width = TEST_WIDTH,
            .height = TEST_HEIGHT,
            .format = DB_PIXEL_FORMAT_RGBA8,
        },
        &target);
    const db_render_ir_fill_t fill = {
        .rect = {.x = (int32_t)frame, .width = 1, .height = 1},
        .color = {.rgba = {0.25, 0.5, 0.75, 1.0}},
    };
    (void)db_render_ir_fill_rects(&fixture->store, target, &fill, 1U,
                                  DB_RENDER_IR_INVALID_ID);
}

static db_frame_plan_t plan_for(replay_ir_fixture_t *fixture, uint32_t frame,
                                uint32_t replay_depth) {
    return (db_frame_plan_t){
        .frame_index = frame,
        .pixel_width = TEST_WIDTH,
        .pixel_height = TEST_HEIGHT,
        .presentation_replay_depth = replay_depth,
        .update_ir = db_render_ir_store_view(&fixture->store),
    };
}

static void replay_collects_chronological_deep_copies(db_test_state_t *state) {
    g_gl1_state = (renderer_state_t){0};
    g_gl1_state.diagnostics.gl1_replay_capacity = 3U;
    g_gl1_state.backing.format.surface_pixel_format = DB_PIXEL_FORMAT_RGBA8;
    DB_TEST_EXPECT_TRUE(state, db_gl1_replay_init() != 0);
    uint64_t hashes[3] = {0U};
    for (uint32_t frame = 0U; frame < 3U; frame++) {
        replay_ir_fixture_t fixture = {0};
        init_ir(&fixture, frame);
        const db_frame_plan_t plan = plan_for(&fixture, frame, 0U);
        hashes[frame] = db_render_ir_hash(&plan.update_ir);
        DB_TEST_EXPECT_TRUE(
            state, db_gl1_replay_commit(&plan, TEST_WIDTH, TEST_HEIGHT,
                                        DB_PIXEL_FORMAT_RGBA8, 0) != 0);
    }
    replay_ir_fixture_t current = {0};
    init_ir(&current, 3U);
    const db_frame_plan_t plan = plan_for(&current, 3U, 2U);
    db_render_ir_view_t views[DB_REPLAY_CAPACITY_MAX] = {};
    int use_rebuild = 0;
    const size_t count = db_gl1_replay_collect(
        &plan, views, DB_REPLAY_CAPACITY_MAX, &use_rebuild);
    DB_TEST_EXPECT_EQ_SIZE(state, count, 2U);
    DB_TEST_EXPECT_EQ_INT(state, use_rebuild, 0);
    DB_TEST_EXPECT_EQ_U64(state, db_render_ir_hash(&views[0]), hashes[1]);
    DB_TEST_EXPECT_EQ_U64(state, db_render_ir_hash(&views[1]), hashes[2]);
    db_gl1_replay_shutdown();
}

static void
incompatible_history_requires_published_rebuild(db_test_state_t *state) {
    g_gl1_state = (renderer_state_t){0};
    g_gl1_state.diagnostics.gl1_replay_capacity = 1U;
    g_gl1_state.backing.format.surface_pixel_format = DB_PIXEL_FORMAT_RGBA8;
    DB_TEST_EXPECT_TRUE(state, db_gl1_replay_init() != 0);
    replay_ir_fixture_t fixture = {0};
    init_ir(&fixture, 2U);
    const db_frame_plan_t plan = plan_for(&fixture, 2U, 2U);
    db_render_ir_view_t views[DB_REPLAY_CAPACITY_MAX] = {};
    int use_rebuild = 0;
    DB_TEST_EXPECT_EQ_SIZE(state,
                           db_gl1_replay_collect(&plan, views,
                                                 DB_REPLAY_CAPACITY_MAX,
                                                 &use_rebuild),
                           0U);
    DB_TEST_EXPECT_EQ_INT(state, use_rebuild, 1);
    DB_TEST_EXPECT_EQ_U32(
        state, g_gl1_state.replay.policy.rebuilds_due_to_insufficient_history,
        1U);
    db_gl1_replay_shutdown();
}

static void replay_storage_is_fixed_and_bounded(db_test_state_t *state) {
    g_gl1_state = (renderer_state_t){0};
    g_gl1_state.diagnostics.gl1_replay_capacity = DB_REPLAY_CAPACITY_MAX;
    DB_TEST_EXPECT_TRUE(state, db_gl1_replay_init() != 0);
    DB_TEST_EXPECT_TRUE(state, g_gl1_state.replay.available != 0);
    DB_TEST_EXPECT_TRUE(state, g_gl1_state.replay.allocation_bytes <=
                                   (size_t)8U * 1024U * 1024U);
    DB_TEST_EXPECT_TRUE(state,
                        g_gl1_state.replay.entries[DB_REPLAY_CAPACITY_MAX - 1U]
                                .update.store.commands != NULL);
    db_gl1_replay_shutdown();
}

unsigned db_gl1_replay_test_run_all(void) {
    static const db_test_case_t cases[] = {
        {"replay_collects_chronological_deep_copies",
         replay_collects_chronological_deep_copies},
        {"incompatible_history_requires_published_rebuild",
         incompatible_history_requires_published_rebuild},
        {"replay_storage_is_fixed_and_bounded",
         replay_storage_is_fixed_and_bounded},
    };
    return db_test_run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}
