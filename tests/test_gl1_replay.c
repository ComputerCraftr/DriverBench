#include "renderers/opengl_gl1_5_gles1_1/gl1_internal.h"
#include "renderers/opengl_gl1_5_gles1_1/gl1_renderer.h"
#include "support/test_harness.h"

#include "core/db_core.h"
#include "core/db_frame_plan.h"
#include "core/db_numeric.h"
#include "core/db_render_ir.h"
#include "core/db_render_ir_snapshot.h"
#include "core/db_render_result.h"
#include "core/db_render_types.h"
#include "core/db_replay_policy.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    TEST_COMMAND_BYTES = 1024,
    TEST_CAPACITY = 16,
    TEST_WIDTH = 100,
    TEST_HEIGHT = 80,
    TEST_TARGET_GENERATION = 9,
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
            state, db_gl1_replay_prepare(&plan, TEST_WIDTH, TEST_HEIGHT,
                                         DB_PIXEL_FORMAT_RGBA8, 1U, 0) != 0);
        db_gl1_replay_publish_pending();
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
    DB_TEST_EXPECT_TRUE(
        state, g_gl1_state.replay.entries[g_gl1_state.replay.pending_entry]
                       .update.store.commands != NULL);
    db_gl1_replay_shutdown();
}

static int replay_bytes_are_identical(const void *lhs, const void *rhs,
                                      size_t count, size_t element_size) {
    if (count == 0U) {
        return 1;
    }
    const size_t byte_count = db_checked_mul_size(
        "test_gl1_replay", "replay_compare_bytes", count, element_size);
    return DB_BOOL(memcmp(lhs, rhs, byte_count) == 0);
}

static int replay_views_are_byte_identical(const db_render_ir_view_t *lhs,
                                           const db_render_ir_view_t *rhs) {
    if ((lhs->command_size != rhs->command_size) ||
        (lhs->command_count != rhs->command_count) ||
        (lhs->fill_count != rhs->fill_count) ||
        (lhs->resource_count != rhs->resource_count) ||
        (lhs->region_count != rhs->region_count) ||
        (lhs->band_count != rhs->band_count) ||
        (lhs->span_count != rhs->span_count)) {
        return 0;
    }
    return DB_BOOL(
        replay_bytes_are_identical(lhs->commands, rhs->commands,
                                   lhs->command_size, 1U) &&
        replay_bytes_are_identical(lhs->fills, rhs->fills, lhs->fill_count,
                                   sizeof(*lhs->fills)) &&
        replay_bytes_are_identical(lhs->resources, rhs->resources,
                                   lhs->resource_count,
                                   sizeof(*lhs->resources)) &&
        replay_bytes_are_identical(lhs->regions, rhs->regions,
                                   lhs->region_count, sizeof(*lhs->regions)) &&
        replay_bytes_are_identical(lhs->bands, rhs->bands, lhs->band_count,
                                   sizeof(*lhs->bands)) &&
        replay_bytes_are_identical(lhs->spans, rhs->spans, lhs->span_count,
                                   sizeof(*lhs->spans)));
}

static int replay_entry_metadata_is_identical(const gl1_replay_entry_t *lhs,
                                              const gl1_replay_entry_t *rhs) {
    return DB_BOOL(
        (lhs->update.layout_generation == rhs->update.layout_generation) &&
        (lhs->update.store.commands == rhs->update.store.commands) &&
        (lhs->update.store.command_capacity ==
         rhs->update.store.command_capacity) &&
        (lhs->update.store.fills == rhs->update.store.fills) &&
        (lhs->update.store.fill_capacity == rhs->update.store.fill_capacity) &&
        (lhs->update.store.resources == rhs->update.store.resources) &&
        (lhs->update.store.resource_capacity ==
         rhs->update.store.resource_capacity) &&
        (lhs->update.store.regions == rhs->update.store.regions) &&
        (lhs->update.store.region_capacity ==
         rhs->update.store.region_capacity) &&
        (lhs->update.store.bands == rhs->update.store.bands) &&
        (lhs->update.store.band_capacity == rhs->update.store.band_capacity) &&
        (lhs->update.store.spans == rhs->update.store.spans) &&
        (lhs->update.store.span_capacity == rhs->update.store.span_capacity) &&
        (lhs->update.store.next_sequence == rhs->update.store.next_sequence) &&
        (lhs->update.store.status == rhs->update.store.status) &&
        (lhs->frame_index == rhs->frame_index) &&
        (lhs->target_generation == rhs->target_generation) &&
        (lhs->width == rhs->width) && (lhs->height == rhs->height) &&
        (lhs->format == rhs->format) &&
        (lhs->replay_boundary == rhs->replay_boundary) &&
        (lhs->valid == rhs->valid));
}

static void
failed_presentation_preserves_committed_history(db_test_state_t *state) {
    g_gl1_state = (renderer_state_t){0};
    g_gl1_state.diagnostics.gl1_replay_capacity = 3U;
    g_gl1_state.backing.format.surface_pixel_format = DB_PIXEL_FORMAT_RGBA8;
    DB_TEST_EXPECT_TRUE(state, db_gl1_replay_init() != 0);
    db_render_ir_snapshot_t committed_copies[3] = {0};
    gl1_replay_entry_t committed_entries[3] = {0};
    for (uint32_t frame = 0U; frame < 3U; frame++) {
        replay_ir_fixture_t fixture = {0};
        init_ir(&fixture, frame);
        const db_frame_plan_t plan = plan_for(&fixture, frame, 0U);
        DB_TEST_EXPECT_TRUE(
            state, db_gl1_replay_prepare(&plan, TEST_WIDTH, TEST_HEIGHT,
                                         DB_PIXEL_FORMAT_RGBA8,
                                         TEST_TARGET_GENERATION, 0) != 0);
        db_gl1_replay_publish_pending();
        const db_render_ir_view_t committed = db_render_ir_snapshot_view(
            &g_gl1_state.replay.entries[frame].update);
        DB_TEST_EXPECT_TRUE(
            state, db_render_ir_snapshot_init(&committed_copies[frame],
                                              TEST_COMMAND_BYTES, TEST_CAPACITY,
                                              2U, TEST_CAPACITY, TEST_CAPACITY,
                                              TEST_CAPACITY) != 0);
        DB_TEST_EXPECT_EQ_INT(
            state,
            db_render_ir_snapshot_capture(&committed_copies[frame], &committed),
            DB_RENDER_IR_OK);
        committed_entries[frame] = g_gl1_state.replay.entries[frame];
    }

    replay_ir_fixture_t pending_fixture = {0};
    init_ir(&pending_fixture, 3U);
    const db_frame_plan_t pending_plan = plan_for(&pending_fixture, 3U, 0U);
    DB_TEST_EXPECT_TRUE(
        state, db_gl1_replay_prepare(&pending_plan, TEST_WIDTH, TEST_HEIGHT,
                                     DB_PIXEL_FORMAT_RGBA8,
                                     TEST_TARGET_GENERATION, 0) != 0);
    db_gl1_finalize_frame(0, DB_RENDER_TARGET_GL1_DIRECT_WINDOW,
                          TEST_TARGET_GENERATION);

    for (uint32_t index = 0U; index < 3U; index++) {
        const db_render_ir_view_t committed = db_render_ir_snapshot_view(
            &g_gl1_state.replay.entries[index].update);
        const db_render_ir_view_t prior =
            db_render_ir_snapshot_view(&committed_copies[index]);
        DB_TEST_EXPECT_TRUE(
            state, replay_views_are_byte_identical(&committed, &prior) != 0);
        DB_TEST_EXPECT_TRUE(state, replay_entry_metadata_is_identical(
                                       &g_gl1_state.replay.entries[index],
                                       &committed_entries[index]) != 0);
        db_render_ir_snapshot_shutdown(&committed_copies[index]);
    }
    DB_TEST_EXPECT_EQ_U32(state, g_gl1_state.replay.policy.retained_count, 3U);
    db_gl1_replay_shutdown();
}

static void failed_prepare_preserves_committed_lineage(db_test_state_t *state) {
    g_gl1_state = (renderer_state_t){0};
    g_gl1_state.diagnostics.gl1_replay_capacity = 1U;
    g_gl1_state.backing.format.surface_pixel_format = DB_PIXEL_FORMAT_RGBA8;
    DB_TEST_EXPECT_TRUE(state, db_gl1_replay_init() != 0);
    g_gl1_state.replay.target_generation = TEST_TARGET_GENERATION;

    replay_ir_fixture_t fixture = {0};
    init_ir(&fixture, 0U);
    db_frame_plan_t malformed = plan_for(&fixture, 0U, 0U);
    malformed.update_ir.command_count++;
    DB_TEST_EXPECT_EQ_INT(state,
                          db_gl1_replay_prepare(&malformed, TEST_WIDTH,
                                                TEST_HEIGHT,
                                                DB_PIXEL_FORMAT_RGBA8,
                                                TEST_TARGET_GENERATION + 1U, 0),
                          0);
    DB_TEST_EXPECT_EQ_U64(state, g_gl1_state.replay.target_generation,
                          TEST_TARGET_GENERATION);
    DB_TEST_EXPECT_EQ_U32(state, g_gl1_state.replay.policy.retained_count, 0U);
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
        {"failed_presentation_preserves_committed_history",
         failed_presentation_preserves_committed_history},
        {"failed_prepare_preserves_committed_lineage",
         failed_prepare_preserves_committed_lineage},
    };
    return db_test_run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}
