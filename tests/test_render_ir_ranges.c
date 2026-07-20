#include "core/db_geometry.h"
#include "core/db_render_ir.h"
#include "core/db_render_types.h"
#include "support/test_harness.h"

#include <stddef.h>
#include <stdint.h>

enum {
    TEST_COMMAND_BYTES = 2048,
    TEST_CAPACITY = 16,
    TEST_TARGET_WIDTH = 100,
    TEST_TARGET_HEIGHT = 80,
    TEST_TOUCHED_HEIGHT = 6,
};

typedef struct {
    max_align_t commands[TEST_COMMAND_BYTES / sizeof(max_align_t)];
    db_render_ir_fill_t fills[TEST_CAPACITY];
    db_render_ir_resource_t resources[1];
    db_render_ir_region_t regions[TEST_CAPACITY];
    db_render_ir_band_t bands[TEST_CAPACITY];
    db_render_ir_span_t spans[TEST_CAPACITY];
    db_render_ir_store_t store;
} range_store_t;

static void init_store(range_store_t *fixture) {
    *fixture = (range_store_t){0};
    fixture->store = (db_render_ir_store_t){
        .commands = fixture->commands,
        .command_capacity = sizeof(fixture->commands),
        .fills = fixture->fills,
        .fill_capacity = TEST_CAPACITY,
        .resources = fixture->resources,
        .resource_capacity = 1U,
        .regions = fixture->regions,
        .region_capacity = TEST_CAPACITY,
        .bands = fixture->bands,
        .band_capacity = TEST_CAPACITY,
        .spans = fixture->spans,
        .span_capacity = TEST_CAPACITY,
    };
}

static db_render_ir_resource_id_t add_target(db_test_state_t *state,
                                             range_store_t *fixture) {
    db_render_ir_resource_id_t target = DB_RENDER_IR_INVALID_ID;
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_add_resource(
            &fixture->store,
            &(const db_render_ir_resource_t){
                .kind = DB_RENDER_IR_RESOURCE_CANONICAL_TARGET,
                .width = TEST_TARGET_WIDTH,
                .height = TEST_TARGET_HEIGHT,
                .format = DB_PIXEL_FORMAT_RGBA8,
            },
            &target),
        DB_RENDER_IR_OK);
    return target;
}

static void compatibility_respects_ordering_domains(db_test_state_t *state) {
    range_store_t fixture = {0};
    init_store(&fixture);
    const db_render_ir_resource_id_t target = add_target(state, &fixture);
    const db_render_ir_fill_t fills[] = {
        {.rect = {.width = 2, .height = 2},
         .color = {.rgba = {1.0, 0.0, 0.0, 1.0}}},
        {.rect = {.x = 2, .width = 2, .height = 2},
         .color = {.rgba = {1.0, 0.0, 0.0, 1.0}}},
    };
    (void)db_render_ir_begin_target(&fixture.store, target);
    (void)db_render_ir_fill_rects(&fixture.store, target, &fills[0], 1U,
                                  DB_RENDER_IR_INVALID_ID);
    (void)db_render_ir_set_last_command_ordering(&fixture.store, 1U, 0U);
    (void)db_render_ir_fill_rects(&fixture.store, target, &fills[1], 1U,
                                  DB_RENDER_IR_INVALID_ID);
    (void)db_render_ir_set_last_command_ordering(&fixture.store, 2U, 0U);
    (void)db_render_ir_end_target(&fixture.store, target);
    const db_render_ir_view_t view = db_render_ir_store_view(&fixture.store);
    db_render_ir_command_range_t ranges[2] = {0};
    int overflow = 0;
    DB_TEST_EXPECT_EQ_SIZE(state,
                           db_render_ir_collect_command_ranges(
                               &view, DB_RENDER_IR_STREAM_UPDATE, ranges,
                               sizeof(ranges) / sizeof(ranges[0]), &overflow),
                           2U);
    DB_TEST_EXPECT_EQ_INT(state, overflow, 0);
    const db_render_ir_metadata_t metadata = db_render_ir_metadata(
        &view, DB_RENDER_IR_OK, TEST_TARGET_WIDTH, TEST_TARGET_HEIGHT);
    DB_TEST_EXPECT_EQ_U32(state, metadata.compatible_batch_count, 2U);
}

static void
malformed_commands_are_never_batch_compatible(db_test_state_t *state) {
    range_store_t fixture = {0};
    init_store(&fixture);
    const db_render_ir_resource_id_t target = add_target(state, &fixture);
    const db_render_ir_view_t view = db_render_ir_store_view(&fixture.store);
    db_render_ir_command_header_t left = {
        .byte_size =
            DB_RENDER_IR_ALIGNED_RECORD_SIZE(db_render_ir_command_header_t),
        .opcode = DB_RENDER_IR_OP_BEGIN_TARGET,
        .destination = target,
        .clip_region = DB_RENDER_IR_INVALID_ID,
        .touched_region = DB_RENDER_IR_INVALID_ID,
        .full_coverage_region = DB_RENDER_IR_INVALID_ID,
    };
    db_render_ir_command_header_t right = left;
    DB_TEST_EXPECT_TRUE(state, db_render_ir_commands_batch_compatible(
                                   &view, &left, &right) != 0);

    right.opcode = UINT8_MAX;
    DB_TEST_EXPECT_EQ_INT(
        state, db_render_ir_commands_batch_compatible(&view, &left, &right), 0);
    right = left;
    right.byte_size--;
    DB_TEST_EXPECT_EQ_INT(
        state, db_render_ir_commands_batch_compatible(&view, &left, &right), 0);

    left = (db_render_ir_command_header_t){
        .byte_size =
            DB_RENDER_IR_ALIGNED_RECORD_SIZE(db_render_ir_upload_command_t),
        .opcode = DB_RENDER_IR_OP_UPLOAD_IMAGE,
        .destination = target,
        .clip_region = DB_RENDER_IR_INVALID_ID,
        .touched_region = DB_RENDER_IR_INVALID_ID,
        .full_coverage_region = DB_RENDER_IR_INVALID_ID,
    };
    right = left;
    DB_TEST_EXPECT_EQ_INT(
        state, db_render_ir_commands_batch_compatible(&view, &left, &right), 0);

    db_render_ir_region_t malformed_region = {
        .first_band = 0U,
        .band_count = 1U,
    };
    const db_render_ir_view_t malformed_view = {
        .resources = fixture.resources,
        .resource_count = 1U,
        .regions = &malformed_region,
        .region_count = 1U,
    };
    left = (db_render_ir_command_header_t){
        .byte_size =
            DB_RENDER_IR_ALIGNED_RECORD_SIZE(db_render_ir_command_header_t),
        .opcode = DB_RENDER_IR_OP_BEGIN_TARGET,
        .destination = target,
        .clip_region = 0U,
        .touched_region = DB_RENDER_IR_INVALID_ID,
        .full_coverage_region = DB_RENDER_IR_INVALID_ID,
    };
    right = left;
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_commands_batch_compatible(&malformed_view, &left, &right),
        0);
}

static void command_ranges_summarize_geometry_once(db_test_state_t *state) {
    range_store_t fixture = {0};
    init_store(&fixture);
    const db_render_ir_resource_id_t target = add_target(state, &fixture);
    const db_render_ir_fill_t fills[] = {
        {.rect = {.x = 1, .y = 2, .width = 3, .height = 4},
         .color = {.rgba = {1.0, 0.0, 0.0, 1.0}}},
        {.rect = {.x = 5, .y = 6, .width = 7, .height = 8},
         .color = {.rgba = {0.0, 1.0, 0.0, 1.0}}},
        {.rect = {.x = 9, .y = 10, .width = 11, .height = 12},
         .color = {.rgba = {0.0, 0.0, 1.0, 1.0}}},
    };
    (void)db_render_ir_begin_target(&fixture.store, target);
    (void)db_render_ir_fill_rects(&fixture.store, target, fills,
                                  sizeof(fills) / sizeof(fills[0]),
                                  DB_RENDER_IR_INVALID_ID);
    (void)db_render_ir_end_target(&fixture.store, target);
    const db_render_ir_view_t view = db_render_ir_store_view(&fixture.store);
    db_render_ir_command_range_t range = {0};
    int overflow = 0;
    DB_TEST_EXPECT_EQ_SIZE(
        state,
        db_render_ir_collect_command_ranges(&view, DB_RENDER_IR_STREAM_UPDATE,
                                            &range, 1U, &overflow),
        1U);
    DB_TEST_EXPECT_EQ_INT(state, overflow, 0);
    DB_TEST_EXPECT_EQ_U32(state, range.instance_count, 3U);
    DB_TEST_EXPECT_EQ_U32(state, range.fallback_instance_count, 3U);
    DB_TEST_EXPECT_EQ_INT(state, range.has_bounds, 1);
    DB_TEST_EXPECT_EQ_INT(state, range.bounds.x, 1);
    DB_TEST_EXPECT_EQ_INT(state, range.bounds.y, 2);
    DB_TEST_EXPECT_EQ_INT(state, range.bounds.width, 19);
    DB_TEST_EXPECT_EQ_INT(state, range.bounds.height, 20);
}

static void command_range_bounds_use_effective_region(db_test_state_t *state) {
    range_store_t fixture = {0};
    init_store(&fixture);
    const db_render_ir_resource_id_t target = add_target(state, &fixture);
    const db_render_ir_fill_t fill = {
        .rect = {.width = 20, .height = 20},
        .color = {.rgba = {1.0, 0.0, 0.0, 1.0}},
    };
    db_render_ir_region_id_t touched = DB_RENDER_IR_INVALID_ID;
    (void)db_render_ir_add_rect_region(
        &fixture.store,
        (db_render_ir_rect_t){
            .x = 3, .y = 4, .width = 5, .height = TEST_TOUCHED_HEIGHT},
        &touched);
    (void)db_render_ir_fill_rects(&fixture.store, target, &fill, 1U, touched);
    (void)db_render_ir_set_last_command_regions(&fixture.store, touched,
                                                touched);
    const db_render_ir_view_t view = db_render_ir_store_view(&fixture.store);
    db_render_ir_command_range_t range = {0};
    int overflow = 0;
    DB_TEST_EXPECT_EQ_SIZE(
        state,
        db_render_ir_collect_command_ranges(&view, DB_RENDER_IR_STREAM_UPDATE,
                                            &range, 1U, &overflow),
        1U);
    DB_TEST_EXPECT_EQ_INT(state, overflow, 0);
    DB_TEST_EXPECT_EQ_INT(state, range.bounds.x, 3);
    DB_TEST_EXPECT_EQ_INT(state, range.bounds.y, 4);
    DB_TEST_EXPECT_EQ_INT(state, range.bounds.width, 5);
    DB_TEST_EXPECT_EQ_INT(state, range.bounds.height, 6);
}

static void
range_and_region_capacity_failures_publish_nothing(db_test_state_t *state) {
    range_store_t fixture = {0};
    init_store(&fixture);
    const db_render_ir_resource_id_t target = add_target(state, &fixture);
    const db_render_ir_fill_t clip_fills[] = {
        {.rect = {.width = 2, .height = 2}},
        {.rect = {.x = 4, .y = 4, .width = 2, .height = 2}},
    };
    db_render_ir_region_id_t clip = DB_RENDER_IR_INVALID_ID;
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_add_fill_region(&fixture.store, clip_fills, 2U, &clip),
        DB_RENDER_IR_OK);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_clear(&fixture.store, target,
                           (db_render_ir_color_t){.rgba = {0.0, 0.0, 0.0, 1.0}},
                           clip),
        DB_RENDER_IR_OK);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_fill_rects(&fixture.store, target,
                                &(const db_render_ir_fill_t){
                                    .rect = {.width = 4, .height = 4},
                                    .color = {.rgba = {1.0, 0.0, 0.0, 1.0}}},
                                1U, clip),
        DB_RENDER_IR_OK);
    const db_render_ir_view_t view = db_render_ir_store_view(&fixture.store);
    db_render_ir_command_range_t range = {
        .first_sequence = UINT32_C(0xA5A5A5A5),
        .command_count = UINT32_C(0x5A5A5A5A),
    };
    int overflow = 0;
    DB_TEST_EXPECT_EQ_SIZE(
        state,
        db_render_ir_collect_command_ranges(&view, DB_RENDER_IR_STREAM_UPDATE,
                                            &range, 1U, &overflow),
        0U);
    DB_TEST_EXPECT_EQ_INT(state, overflow, 1);
    DB_TEST_EXPECT_EQ_U32(state, range.first_sequence, UINT32_C(0xA5A5A5A5));
    DB_TEST_EXPECT_EQ_U32(state, range.command_count, UINT32_C(0x5A5A5A5A));

    db_grid_block_t block = {
        .row_start = UINT32_C(0x12345678),
        .row_count = UINT32_C(0x87654321),
    };
    overflow = 0;
    DB_TEST_EXPECT_EQ_SIZE(state,
                           db_render_ir_region_copy_grid_blocks(
                               &view, clip, &block, 1U, &overflow),
                           0U);
    DB_TEST_EXPECT_EQ_INT(state, overflow, 1);
    DB_TEST_EXPECT_EQ_U32(state, block.row_start, UINT32_C(0x12345678));
    DB_TEST_EXPECT_EQ_U32(state, block.row_count, UINT32_C(0x87654321));
}

static void
malformed_rect_iterator_references_are_bounded(db_test_state_t *state) {
    range_store_t fixture = {0};
    init_store(&fixture);
    const db_render_ir_resource_id_t target = add_target(state, &fixture);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_fill_rects(&fixture.store, target,
                                &(const db_render_ir_fill_t){
                                    .rect = {.width = 2, .height = 2},
                                    .color = {.rgba = {1.0, 0.0, 0.0, 1.0}}},
                                1U, DB_RENDER_IR_INVALID_ID),
        DB_RENDER_IR_OK);
    db_render_ir_view_t malformed = db_render_ir_store_view(&fixture.store);
    malformed.fill_count = 0U;
    db_render_ir_rect_iterator_t iterator = {0};
    db_render_ir_rect_iterator_begin(&iterator, &malformed);
    db_render_ir_fill_t output = {0};
    DB_TEST_EXPECT_EQ_INT(
        state, db_render_ir_rect_iterator_next(&iterator, &output), 0);

    init_store(&fixture);
    const db_render_ir_resource_id_t clipped_target =
        add_target(state, &fixture);
    db_render_ir_region_id_t clip = DB_RENDER_IR_INVALID_ID;
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_add_rect_region(
                              &fixture.store,
                              (db_render_ir_rect_t){.width = 2, .height = 2},
                              &clip),
                          DB_RENDER_IR_OK);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_clear(&fixture.store, clipped_target,
                           (db_render_ir_color_t){.rgba = {0.0, 0.0, 0.0, 1.0}},
                           clip),
        DB_RENDER_IR_OK);
    malformed = db_render_ir_store_view(&fixture.store);
    malformed.band_count = 0U;
    db_render_ir_rect_iterator_begin(&iterator, &malformed);
    DB_TEST_EXPECT_EQ_INT(
        state, db_render_ir_rect_iterator_next(&iterator, &output), 0);
}

unsigned db_render_ir_ranges_test_run_all(void) {
    static const db_test_case_t cases[] = {
        {"compatibility_respects_ordering_domains",
         compatibility_respects_ordering_domains},
        {"malformed_commands_are_never_batch_compatible",
         malformed_commands_are_never_batch_compatible},
        {"command_ranges_summarize_geometry_once",
         command_ranges_summarize_geometry_once},
        {"command_range_bounds_use_effective_region",
         command_range_bounds_use_effective_region},
        {"range_and_region_capacity_failures_publish_nothing",
         range_and_region_capacity_failures_publish_nothing},
        {"malformed_rect_iterator_references_are_bounded",
         malformed_rect_iterator_references_are_bounded},
    };
    return db_test_run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}
