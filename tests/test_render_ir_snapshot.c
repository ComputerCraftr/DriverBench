#include "core/db_numeric.h"
#include "core/db_render_ir.h"
#include "core/db_render_ir_snapshot.h"
#include "core/db_render_types.h"
#include "support/test_harness.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    TEST_COMMAND_BYTES = 4096,
    TEST_CAPACITY = 64,
    TEST_MUTATED_RECT_X = 99
};

typedef struct {
    max_align_t commands[TEST_COMMAND_BYTES / sizeof(max_align_t)];
    db_render_ir_fill_t fills[TEST_CAPACITY];
    db_render_ir_resource_t resources[4];
    db_render_ir_region_t regions[TEST_CAPACITY];
    db_render_ir_band_t bands[TEST_CAPACITY];
    db_render_ir_span_t spans[TEST_CAPACITY];
    db_render_ir_store_t store;
} test_store_t;

static void init_store(test_store_t *fixture) {
    *fixture = (test_store_t){0};
    fixture->store = (db_render_ir_store_t){
        .commands = fixture->commands,
        .command_capacity = sizeof(fixture->commands),
        .fills = fixture->fills,
        .fill_capacity = TEST_CAPACITY,
        .resources = fixture->resources,
        .resource_capacity = 4U,
        .regions = fixture->regions,
        .region_capacity = TEST_CAPACITY,
        .bands = fixture->bands,
        .band_capacity = TEST_CAPACITY,
        .spans = fixture->spans,
        .span_capacity = TEST_CAPACITY,
    };
}

static db_render_ir_resource_id_t add_target(db_test_state_t *state,
                                             test_store_t *fixture) {
    db_render_ir_resource_id_t target = DB_RENDER_IR_INVALID_ID;
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_add_resource(
            &fixture->store,
            &(const db_render_ir_resource_t){
                .kind = DB_RENDER_IR_RESOURCE_CANONICAL_TARGET,
                .width = 100U,
                .height = 80U,
                .format = DB_PIXEL_FORMAT_RGBA8},
            &target),
        DB_RENDER_IR_OK);
    return target;
}

static void color_construction_is_canonical(db_test_state_t *state) {
    test_store_t fixture = {0};
    init_store(&fixture);
    const db_render_ir_resource_id_t target = add_target(state, &fixture);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_clear(
            &fixture.store, target,
            (db_render_ir_color_t){.rgba = {-0.0, 2.0, -1.0, 1.0}},
            DB_RENDER_IR_INVALID_ID),
        DB_RENDER_IR_OK);
    const db_render_ir_view_t view = db_render_ir_store_view(&fixture.store);
    db_render_ir_iterator_t iterator = {0};
    db_render_ir_iterator_begin(&iterator, &view);
    const db_render_ir_clear_command_t *const clear = DB_RENDER_IR_COMMAND_AS(
        db_render_ir_clear_command_t, db_render_ir_iterator_next(&iterator));
    DB_TEST_EXPECT_TRUE(state, clear != NULL);
    if (clear == NULL) {
        return;
    }
    DB_TEST_EXPECT_EQ_SIZE(state, db_f64_to_bits_u64(clear->color.rgba[0]),
                           UINT64_C(0));
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_clear(&fixture.store, target,
                           (db_render_ir_color_t){.rgba = {0.0, 0.0, 0.0, 1.5}},
                           DB_RENDER_IR_INVALID_ID),
        DB_RENDER_IR_INVALID);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_clear(
            &fixture.store, target,
            (db_render_ir_color_t){.rgba = {0.0, 0.0, 0.0, nan("")}},
            DB_RENDER_IR_INVALID_ID),
        DB_RENDER_IR_INVALID);
}

static void snapshot_owns_complete_ir_storage(db_test_state_t *state) {
    test_store_t fixture = {0};
    init_store(&fixture);
    const db_render_ir_resource_id_t target = add_target(state, &fixture);
    const db_render_ir_fill_t fill = {
        .rect = {.x = 3, .y = 4, .width = 5, .height = 6},
        .color = {.rgba = {0.25, 0.5, 0.75, 1.0}},
    };
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_fill_rects(&fixture.store, target, &fill,
                                                  1U, DB_RENDER_IR_INVALID_ID),
                          DB_RENDER_IR_OK);
    db_render_ir_snapshot_t snapshot = {0};
    DB_TEST_EXPECT_TRUE(
        state, db_render_ir_snapshot_init(&snapshot, TEST_COMMAND_BYTES,
                                          TEST_CAPACITY, 4U, TEST_CAPACITY,
                                          TEST_CAPACITY, TEST_CAPACITY) != 0);
    const db_render_ir_view_t source = db_render_ir_store_view(&fixture.store);
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_snapshot_capture(&snapshot, &source),
                          DB_RENDER_IR_OK);
    fixture.fills[0].rect.x = TEST_MUTATED_RECT_X;
    memset(fixture.commands, 0, sizeof(fixture.commands));
    const db_render_ir_view_t captured = db_render_ir_snapshot_view(&snapshot);
    DB_TEST_EXPECT_EQ_INT(state, captured.fills[0].rect.x, 3);
    DB_TEST_EXPECT_EQ_U64(state, captured.command_count, source.command_count);
    DB_TEST_EXPECT_EQ_INT(state, db_render_ir_validate(&captured),
                          DB_RENDER_IR_OK);
    db_render_ir_snapshot_shutdown(&snapshot);
}

static void
snapshot_capacity_failure_preserves_prior_copy(db_test_state_t *state) {
    test_store_t fixture = {0};
    init_store(&fixture);
    const db_render_ir_resource_id_t target = add_target(state, &fixture);
    const db_render_ir_fill_t fill = {
        .rect = {.width = 1, .height = 1},
        .color = {.rgba = {1.0, 0.0, 0.0, 1.0}},
    };
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_fill_rects(&fixture.store, target, &fill,
                                                  1U, DB_RENDER_IR_INVALID_ID),
                          DB_RENDER_IR_OK);
    db_render_ir_snapshot_t snapshot = {0};
    DB_TEST_EXPECT_TRUE(
        state, db_render_ir_snapshot_init(&snapshot, TEST_COMMAND_BYTES, 1U, 4U,
                                          TEST_CAPACITY, TEST_CAPACITY,
                                          TEST_CAPACITY) != 0);
    db_render_ir_view_t source = db_render_ir_store_view(&fixture.store);
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_snapshot_capture(&snapshot, &source),
                          DB_RENDER_IR_OK);
    const db_render_ir_view_t captured = db_render_ir_snapshot_view(&snapshot);
    const uint64_t prior_hash = db_render_ir_hash(&captured);
    source.fill_count = 2U;
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_snapshot_capture(&snapshot, &source),
                          DB_RENDER_IR_CAPACITY);
    const db_render_ir_view_t retained = db_render_ir_snapshot_view(&snapshot);
    DB_TEST_EXPECT_EQ_U64(state, db_render_ir_hash(&retained), prior_hash);
    db_render_ir_snapshot_shutdown(&snapshot);
}

static void snapshot_capture_accepts_its_own_view(db_test_state_t *state) {
    test_store_t fixture = {0};
    init_store(&fixture);
    const db_render_ir_resource_id_t target = add_target(state, &fixture);
    const db_render_ir_fill_t fill = {
        .rect = {.x = 2, .y = 3, .width = 4, .height = 5},
        .color = {.rgba = {0.25, 0.5, 0.75, 1.0}},
    };
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_fill_rects(&fixture.store, target, &fill,
                                                  1U, DB_RENDER_IR_INVALID_ID),
                          DB_RENDER_IR_OK);
    db_render_ir_snapshot_t snapshot = {0};
    DB_TEST_EXPECT_TRUE(
        state, db_render_ir_snapshot_init(&snapshot, TEST_COMMAND_BYTES,
                                          TEST_CAPACITY, 4U, TEST_CAPACITY,
                                          TEST_CAPACITY, TEST_CAPACITY) != 0);
    const db_render_ir_view_t source = db_render_ir_store_view(&fixture.store);
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_snapshot_capture(&snapshot, &source),
                          DB_RENDER_IR_OK);
    const db_render_ir_view_t retained = db_render_ir_snapshot_view(&snapshot);
    const uint64_t retained_hash = db_render_ir_hash(&retained);
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_snapshot_capture(&snapshot, &retained),
                          DB_RENDER_IR_OK);
    const db_render_ir_view_t recaptured =
        db_render_ir_snapshot_view(&snapshot);
    DB_TEST_EXPECT_EQ_U64(state, db_render_ir_hash(&recaptured), retained_hash);
    DB_TEST_EXPECT_EQ_INT(state, db_render_ir_validate(&recaptured),
                          DB_RENDER_IR_OK);
    db_render_ir_snapshot_shutdown(&snapshot);
}

static void
snapshot_rejects_cross_arena_alias_transactionally(db_test_state_t *state) {
    test_store_t fixture = {0};
    init_store(&fixture);
    const db_render_ir_resource_id_t target = add_target(state, &fixture);
    const db_render_ir_fill_t fill = {
        .rect = {.x = 2, .y = 3, .width = 4, .height = 5},
        .color = {.rgba = {0.25, 0.5, 0.75, 1.0}},
    };
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_fill_rects(&fixture.store, target, &fill,
                                                  1U, DB_RENDER_IR_INVALID_ID),
                          DB_RENDER_IR_OK);
    db_render_ir_snapshot_t snapshot = {0};
    DB_TEST_EXPECT_TRUE(
        state, db_render_ir_snapshot_init(&snapshot, TEST_COMMAND_BYTES,
                                          TEST_CAPACITY, 4U, TEST_CAPACITY,
                                          TEST_CAPACITY, TEST_CAPACITY) != 0);
    memcpy(snapshot.store.commands, &fill, sizeof(fill));
    unsigned char before[sizeof(fill)] = {0};
    memcpy(before, snapshot.store.commands, sizeof(before));
    db_render_ir_view_t source = db_render_ir_store_view(&fixture.store);
    source.fills = (const db_render_ir_fill_t *)snapshot.store.commands;
    DB_TEST_EXPECT_EQ_INT(state, db_render_ir_validate(&source),
                          DB_RENDER_IR_OK);
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_snapshot_capture(&snapshot, &source),
                          DB_RENDER_IR_INVALID);
    unsigned char after[sizeof(before)] = {0};
    memcpy(after, snapshot.store.commands, sizeof(after));
    DB_TEST_EXPECT_TRUE(state, memcmp(before, after, sizeof(before)) == 0);
    db_render_ir_snapshot_shutdown(&snapshot);
}

static void snapshot_rejects_stale_layout_generation(db_test_state_t *state) {
    test_store_t fixture = {0};
    init_store(&fixture);
    const db_render_ir_resource_id_t target = add_target(state, &fixture);
    const db_render_ir_fill_t fill = {
        .rect = {.width = 1, .height = 1},
        .color = {.rgba = {1.0, 0.0, 0.0, 1.0}},
    };
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_fill_rects(&fixture.store, target, &fill,
                                                  1U, DB_RENDER_IR_INVALID_ID),
                          DB_RENDER_IR_OK);
    db_render_ir_snapshot_t snapshot = {0};
    DB_TEST_EXPECT_TRUE(
        state, db_render_ir_snapshot_init(&snapshot, TEST_COMMAND_BYTES,
                                          TEST_CAPACITY, 4U, TEST_CAPACITY,
                                          TEST_CAPACITY, TEST_CAPACITY) != 0);
    snapshot.layout_generation = DB_RENDER_IR_LAYOUT_GENERATION - 1U;
    const db_render_ir_view_t source = db_render_ir_store_view(&fixture.store);
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_snapshot_capture(&snapshot, &source),
                          DB_RENDER_IR_INVALID);
    DB_TEST_EXPECT_EQ_SIZE(
        state, db_render_ir_snapshot_view(&snapshot).command_size, 0U);
    db_render_ir_snapshot_shutdown(&snapshot);
}

static void
snapshot_rejects_invalid_source_transactionally(db_test_state_t *state) {
    test_store_t fixture = {0};
    init_store(&fixture);
    const db_render_ir_resource_id_t target = add_target(state, &fixture);
    const db_render_ir_fill_t fill = {
        .rect = {.width = 1, .height = 1},
        .color = {.rgba = {1.0, 0.0, 0.0, 1.0}},
    };
    (void)db_render_ir_fill_rects(&fixture.store, target, &fill, 1U,
                                  DB_RENDER_IR_INVALID_ID);
    db_render_ir_snapshot_t snapshot = {0};
    DB_TEST_EXPECT_TRUE(
        state, db_render_ir_snapshot_init(&snapshot, TEST_COMMAND_BYTES,
                                          TEST_CAPACITY, 4U, TEST_CAPACITY,
                                          TEST_CAPACITY, TEST_CAPACITY) != 0);
    db_render_ir_view_t source = db_render_ir_store_view(&fixture.store);
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_snapshot_capture(&snapshot, &source),
                          DB_RENDER_IR_OK);
    const db_render_ir_view_t initial = db_render_ir_snapshot_view(&snapshot);
    const uint64_t retained_hash = db_render_ir_hash(&initial);
    db_render_ir_command_header_t *const command =
        (db_render_ir_command_header_t *)fixture.store.commands;
    command->sequence = 1U;
    source = db_render_ir_store_view(&fixture.store);
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_snapshot_capture(&snapshot, &source),
                          DB_RENDER_IR_INVALID);
    const db_render_ir_view_t retained = db_render_ir_snapshot_view(&snapshot);
    DB_TEST_EXPECT_EQ_U64(state, db_render_ir_hash(&retained), retained_hash);
    db_render_ir_snapshot_shutdown(&snapshot);
}

static void
replay_clone_rejects_nonreplayable_transactionally(db_test_state_t *state) {
    test_store_t fixture = {0};
    init_store(&fixture);
    const db_render_ir_resource_id_t target = add_target(state, &fixture);
    const db_render_ir_fill_t fill = {
        .rect = {.width = 1, .height = 1},
        .color = {.rgba = {1.0, 0.0, 0.0, 1.0}},
    };
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_fill_rects(&fixture.store, target, &fill,
                                                  1U, DB_RENDER_IR_INVALID_ID),
                          DB_RENDER_IR_OK);
    db_render_ir_owned_store_t owned = {0};
    DB_TEST_EXPECT_TRUE(
        state, db_render_ir_snapshot_init(&owned, TEST_COMMAND_BYTES,
                                          TEST_CAPACITY, 4U, TEST_CAPACITY,
                                          TEST_CAPACITY, TEST_CAPACITY) != 0);
    db_render_ir_view_t source = db_render_ir_store_view(&fixture.store);
    db_render_ir_metadata_t metadata =
        db_render_ir_metadata(&source, DB_RENDER_IR_OK, 1U, 1U);
    DB_TEST_EXPECT_EQ_INT(
        state, db_render_ir_clone_replayable(&source, &metadata, &owned),
        DB_RENDER_IR_CLONE_OK);
    const db_render_ir_view_t initial_clone =
        db_render_ir_snapshot_view(&owned);
    const uint64_t retained_hash = db_render_ir_hash(&initial_clone);

    fixture.store.command_size = 0U;
    fixture.store.command_count = 0U;
    fixture.store.next_sequence = 0U;
    DB_TEST_EXPECT_EQ_INT(
        state, db_render_ir_invalidate_resource(&fixture.store, target),
        DB_RENDER_IR_OK);
    source = db_render_ir_store_view(&fixture.store);
    metadata = db_render_ir_metadata(&source, DB_RENDER_IR_OK, 1U, 1U);
    DB_TEST_EXPECT_EQ_INT(
        state, db_render_ir_clone_replayable(&source, &metadata, &owned),
        DB_RENDER_IR_CLONE_NOT_REPLAYABLE);
    const db_render_ir_view_t retained = db_render_ir_snapshot_view(&owned);
    DB_TEST_EXPECT_EQ_U64(state, db_render_ir_hash(&retained), retained_hash);
    db_render_ir_snapshot_shutdown(&owned);
}

static void
command_ranges_do_not_alias_iterator_storage(db_test_state_t *state) {
    test_store_t fixture = {0};
    init_store(&fixture);
    const db_render_ir_resource_id_t target = add_target(state, &fixture);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_clear(&fixture.store, target,
                           (db_render_ir_color_t){.rgba = {0.0, 0.0, 0.0, 1.0}},
                           DB_RENDER_IR_INVALID_ID),
        DB_RENDER_IR_OK);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_fill_linear_gradient(
            &fixture.store, target,
            (db_render_ir_rect_t){.y = 4, .width = 100, .height = 8}, 4, 11, 0,
            (db_render_ir_color_t){.rgba = {0.0, 0.0, 0.0, 1.0}},
            (db_render_ir_color_t){.rgba = {1.0, 1.0, 1.0, 1.0}},
            DB_RENDER_IR_INVALID_ID),
        DB_RENDER_IR_OK);
    const db_render_ir_view_t view = db_render_ir_store_view(&fixture.store);
    db_render_ir_command_range_t ranges[2] = {0};
    int overflow = 0;
    const size_t count = db_render_ir_collect_command_ranges(
        &view, DB_RENDER_IR_STREAM_UPDATE, ranges, 2U, &overflow);
    DB_TEST_EXPECT_EQ_SIZE(state, count, 2U);
    DB_TEST_EXPECT_EQ_INT(state, overflow, 0);
    DB_TEST_EXPECT_EQ_INT(state, ranges[0].opcode, DB_RENDER_IR_OP_CLEAR);
    DB_TEST_EXPECT_EQ_INT(state, ranges[1].opcode,
                          DB_RENDER_IR_OP_FILL_LINEAR_GRADIENT);
    DB_TEST_EXPECT_EQ_U32(state, ranges[0].instance_count, 1U);
    DB_TEST_EXPECT_EQ_U32(state, ranges[1].instance_count, 1U);
}

static void
snapshot_reinitialization_preserves_owned_storage(db_test_state_t *state) {
    db_render_ir_snapshot_t snapshot = {0};
    DB_TEST_EXPECT_TRUE(
        state, db_render_ir_snapshot_init(&snapshot, TEST_COMMAND_BYTES,
                                          TEST_CAPACITY, 4U, TEST_CAPACITY,
                                          TEST_CAPACITY, TEST_CAPACITY) != 0);
    void *const commands = snapshot.store.commands;
    db_render_ir_fill_t *const fills = snapshot.store.fills;
    DB_TEST_EXPECT_TRUE(
        state, db_render_ir_snapshot_init(&snapshot, TEST_COMMAND_BYTES,
                                          TEST_CAPACITY, 4U, TEST_CAPACITY,
                                          TEST_CAPACITY, TEST_CAPACITY) == 0);
    DB_TEST_EXPECT_TRUE(state, snapshot.store.commands == commands);
    DB_TEST_EXPECT_TRUE(state, snapshot.store.fills == fills);
    DB_TEST_EXPECT_EQ_U32(state, snapshot.layout_generation,
                          DB_RENDER_IR_LAYOUT_GENERATION);
    db_render_ir_snapshot_shutdown(&snapshot);
}

unsigned db_render_ir_snapshot_test_run_all(void) {
    static const db_test_case_t cases[] = {
        {"color_construction_is_canonical", color_construction_is_canonical},
        {"snapshot_owns_complete_ir_storage",
         snapshot_owns_complete_ir_storage},
        {"snapshot_capacity_failure_preserves_prior_copy",
         snapshot_capacity_failure_preserves_prior_copy},
        {"snapshot_capture_accepts_its_own_view",
         snapshot_capture_accepts_its_own_view},
        {"snapshot_rejects_cross_arena_alias_transactionally",
         snapshot_rejects_cross_arena_alias_transactionally},
        {"snapshot_rejects_stale_layout_generation",
         snapshot_rejects_stale_layout_generation},
        {"snapshot_rejects_invalid_source_transactionally",
         snapshot_rejects_invalid_source_transactionally},
        {"replay_clone_rejects_nonreplayable_transactionally",
         replay_clone_rejects_nonreplayable_transactionally},
        {"command_ranges_do_not_alias_iterator_storage",
         command_ranges_do_not_alias_iterator_storage},
        {"snapshot_reinitialization_preserves_owned_storage",
         snapshot_reinitialization_preserves_owned_storage},
    };
    return db_test_run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}
