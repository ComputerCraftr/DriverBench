#include "core/db_numeric.h"
#include "core/db_render_ir.h"
#include "core/db_render_ir_optimizer_internal.h"
#include "core/db_render_ir_surface.h"
#include "core/db_render_types.h"
#include "support/test_harness.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    TEST_POLICY_COMMAND_BYTES = 1024,
    TEST_POLICY_CAPACITY = 16,
    TEST_POLICY_GRADIENT_END = 15,
    TEST_POLICY_STRESS_COMMANDS = 8192,
    TEST_POLICY_STRESS_SORT_LEVELS = 13,
    TEST_POLICY_HEADER_SIZE = 40,
    TEST_POLICY_CLEAR_SIZE = 72,
    TEST_POLICY_FILL_SIZE = 48,
#if UINTPTR_MAX == UINT64_MAX
    TEST_POLICY_GRADIENT_SIZE = 136,
    TEST_POLICY_UPLOAD_SIZE = 96,
#else
    TEST_POLICY_GRADIENT_SIZE = 132,
    TEST_POLICY_UPLOAD_SIZE = 92,
#endif
};

typedef struct {
    max_align_t commands[TEST_POLICY_COMMAND_BYTES / sizeof(max_align_t)];
    db_render_ir_fill_t fills[TEST_POLICY_CAPACITY];
    db_render_ir_resource_t resources[2];
    db_render_ir_region_t regions[TEST_POLICY_CAPACITY];
    db_render_ir_band_t bands[TEST_POLICY_CAPACITY];
    db_render_ir_span_t spans[TEST_POLICY_CAPACITY];
    db_render_ir_store_t store;
} test_policy_store_t;

static void init_policy_store(test_policy_store_t *fixture) {
    *fixture = (test_policy_store_t){0};
    fixture->store = (db_render_ir_store_t){
        .commands = fixture->commands,
        .command_capacity = sizeof(fixture->commands),
        .fills = fixture->fills,
        .fill_capacity = TEST_POLICY_CAPACITY,
        .resources = fixture->resources,
        .resource_capacity = 2U,
        .regions = fixture->regions,
        .region_capacity = TEST_POLICY_CAPACITY,
        .bands = fixture->bands,
        .band_capacity = TEST_POLICY_CAPACITY,
        .spans = fixture->spans,
        .span_capacity = TEST_POLICY_CAPACITY,
    };
}

static db_render_ir_resource_id_t
add_policy_target(db_test_state_t *state, test_policy_store_t *fixture) {
    db_render_ir_resource_id_t target = DB_RENDER_IR_INVALID_ID;
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_add_resource(
            &fixture->store,
            &(const db_render_ir_resource_t){
                .kind = DB_RENDER_IR_RESOURCE_CANONICAL_TARGET,
                .width = 16U,
                .height = 16U,
                .format = DB_PIXEL_FORMAT_RGBA8,
            },
            &target),
        DB_RENDER_IR_OK);
    return target;
}

static void command_padding_does_not_affect_hash(db_test_state_t *state) {
    test_policy_store_t fixture = {0};
    init_policy_store(&fixture);
    const db_render_ir_resource_id_t target =
        add_policy_target(state, &fixture);
    (void)db_render_ir_begin_target(&fixture.store, target);
    (void)db_render_ir_clear(
        &fixture.store, target,
        (db_render_ir_color_t){.rgba = {1.0, 0.0, 0.0, 1.0}},
        DB_RENDER_IR_INVALID_ID);
    (void)db_render_ir_end_target(&fixture.store, target);
    const db_render_ir_view_t before_view =
        db_render_ir_store_view(&fixture.store);
    const uint64_t before_hash = db_render_ir_hash(&before_view);
    const size_t clear_offset =
        DB_RENDER_IR_ALIGNED_RECORD_SIZE(db_render_ir_command_header_t);
    const size_t clear_record_size =
        DB_RENDER_IR_ALIGNED_RECORD_SIZE(db_render_ir_clear_command_t);
    unsigned char *const bytes = (unsigned char *)fixture.store.commands;
    for (size_t index = clear_offset + sizeof(db_render_ir_clear_command_t);
         index < clear_offset + clear_record_size; index++) {
        bytes[index] = UINT8_C(0xa5);
    }
    const db_render_ir_view_t after_view =
        db_render_ir_store_view(&fixture.store);
    DB_TEST_EXPECT_EQ_INT(state, db_render_ir_validate(&after_view),
                          DB_RENDER_IR_OK);
    DB_TEST_EXPECT_EQ_U64(state, db_render_ir_hash(&after_view), before_hash);
}

static void
adjacent_merge_rejects_unrepresentable_extent(db_test_state_t *state) {
    const db_render_ir_color_t color = {.rgba = {1.0, 0.0, 0.0, 1.0}};
    db_render_ir_fill_t fills[] = {
        {.rect = {.x = -INT32_MAX, .width = INT32_MAX, .height = 1},
         .color = color},
        {.rect = {.x = 0, .width = INT32_MAX, .height = 1}, .color = color},
    };
    db_render_ir_fill_t scratch[2] = {0};
    size_t count = sizeof(fills) / sizeof(fills[0]);
    DB_TEST_EXPECT_EQ_INT(
        state, db_render_ir_sort_and_merge_fills(fills, scratch, &count, NULL),
        DB_RENDER_IR_OK);
    DB_TEST_EXPECT_EQ_SIZE(state, count, 2U);

    fills[0] = (db_render_ir_fill_t){
        .rect = {.y = -INT32_MAX, .width = 1, .height = INT32_MAX},
        .color = color,
    };
    fills[1] = (db_render_ir_fill_t){
        .rect = {.y = 0, .width = 1, .height = INT32_MAX},
        .color = color,
    };
    count = sizeof(fills) / sizeof(fills[0]);
    DB_TEST_EXPECT_EQ_INT(
        state, db_render_ir_sort_and_merge_fills(fills, scratch, &count, NULL),
        DB_RENDER_IR_OK);
    DB_TEST_EXPECT_EQ_SIZE(state, count, 2U);
}

static void command_layout_baseline_is_stable(db_test_state_t *state) {
    DB_TEST_EXPECT_EQ_U32(state, DB_RENDER_IR_LAYOUT_GENERATION, 2U);
    DB_TEST_EXPECT_EQ_SIZE(state, sizeof(db_render_ir_command_header_t),
                           TEST_POLICY_HEADER_SIZE);
    DB_TEST_EXPECT_EQ_SIZE(state, sizeof(db_render_ir_clear_command_t),
                           TEST_POLICY_CLEAR_SIZE);
    DB_TEST_EXPECT_EQ_SIZE(state, sizeof(db_render_ir_fill_command_t),
                           TEST_POLICY_FILL_SIZE);
    DB_TEST_EXPECT_EQ_SIZE(state,
                           sizeof(db_render_ir_linear_gradient_command_t),
                           TEST_POLICY_GRADIENT_SIZE);
    DB_TEST_EXPECT_EQ_SIZE(state, sizeof(db_render_ir_upload_command_t),
                           TEST_POLICY_UPLOAD_SIZE);
}

static void semantic_hash_includes_region_contents(db_test_state_t *state) {
    test_policy_store_t first = {0};
    test_policy_store_t second = {0};
    init_policy_store(&first);
    init_policy_store(&second);
    const db_render_ir_resource_id_t first_target =
        add_policy_target(state, &first);
    const db_render_ir_resource_id_t second_target =
        add_policy_target(state, &second);
    db_render_ir_region_id_t first_clip = DB_RENDER_IR_INVALID_ID;
    db_render_ir_region_id_t second_clip = DB_RENDER_IR_INVALID_ID;
    (void)db_render_ir_add_rect_region(
        &first.store, (db_render_ir_rect_t){.width = 2, .height = 2},
        &first_clip);
    (void)db_render_ir_add_rect_region(
        &second.store, (db_render_ir_rect_t){.x = 1, .width = 2, .height = 2},
        &second_clip);
    const db_render_ir_fill_t fill = {
        .rect = {.width = 4, .height = 4},
        .color = {.rgba = {0.0, 1.0, 0.0, 1.0}},
    };
    (void)db_render_ir_begin_target(&first.store, first_target);
    (void)db_render_ir_fill_rects(&first.store, first_target, &fill, 1U,
                                  first_clip);
    (void)db_render_ir_end_target(&first.store, first_target);
    (void)db_render_ir_begin_target(&second.store, second_target);
    (void)db_render_ir_fill_rects(&second.store, second_target, &fill, 1U,
                                  second_clip);
    (void)db_render_ir_end_target(&second.store, second_target);
    DB_TEST_EXPECT_EQ_U32(state, first_clip, second_clip);
    const db_render_ir_view_t first_view =
        db_render_ir_store_view(&first.store);
    const db_render_ir_view_t second_view =
        db_render_ir_store_view(&second.store);
    DB_TEST_EXPECT_TRUE(state, db_render_ir_hash(&first_view) !=
                                   db_render_ir_hash(&second_view));
}

static void region_import_is_transactional(db_test_state_t *state) {
    test_policy_store_t source = {0};
    test_policy_store_t destination = {0};
    init_policy_store(&source);
    init_policy_store(&destination);
    db_render_ir_region_id_t empty_region = DB_RENDER_IR_INVALID_ID;
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_add_fill_region(&source.store, NULL, 0U, &empty_region),
        DB_RENDER_IR_OK);
    db_render_ir_region_id_t empty_import = 0U;
    const db_render_ir_view_t empty_source_view =
        db_render_ir_store_view(&source.store);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_region_import(&empty_source_view, empty_region,
                                   &destination.store, &empty_import),
        DB_RENDER_IR_OK);
    DB_TEST_EXPECT_EQ_U32(state, empty_import, DB_RENDER_IR_INVALID_ID);
    DB_TEST_EXPECT_EQ_SIZE(state, destination.store.region_count, 0U);
    const db_render_ir_fill_t fragments[] = {
        {.rect = {.x = 1, .y = 2, .width = 3, .height = 2}},
        {.rect = {.x = 8, .y = 2, .width = 2, .height = 2}},
    };
    db_render_ir_region_id_t source_region = DB_RENDER_IR_INVALID_ID;
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_add_fill_region(&source.store, fragments,
                                                       2U, &source_region),
                          DB_RENDER_IR_OK);
    destination.store.span_capacity = 1U;
    const size_t region_count = destination.store.region_count;
    const size_t band_count = destination.store.band_count;
    const size_t span_count = destination.store.span_count;
    const db_render_ir_status_t prior_status = destination.store.status;
    db_render_ir_region_id_t imported = DB_RENDER_IR_INVALID_ID;
    const db_render_ir_view_t source_view =
        db_render_ir_store_view(&source.store);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_region_import(&source_view, source_region,
                                   &destination.store, &imported),
        DB_RENDER_IR_CAPACITY);
    DB_TEST_EXPECT_EQ_SIZE(state, destination.store.region_count, region_count);
    DB_TEST_EXPECT_EQ_SIZE(state, destination.store.band_count, band_count);
    DB_TEST_EXPECT_EQ_SIZE(state, destination.store.span_count, span_count);
    DB_TEST_EXPECT_EQ_INT(state, destination.store.status, prior_status);
}

static void fill_append_failure_restores_fill_arena(db_test_state_t *state) {
    test_policy_store_t fixture = {0};
    init_policy_store(&fixture);
    const db_render_ir_resource_id_t target =
        add_policy_target(state, &fixture);
    (void)db_render_ir_begin_target(&fixture.store, target);
    fixture.store.command_capacity = fixture.store.command_size;
    const db_render_ir_fill_t fill = {
        .rect = {.width = 2, .height = 2},
        .color = {.rgba = {1.0, 0.0, 0.0, 1.0}},
    };
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_fill_rects(&fixture.store, target, &fill,
                                                  1U, DB_RENDER_IR_INVALID_ID),
                          DB_RENDER_IR_CAPACITY);
    DB_TEST_EXPECT_EQ_SIZE(state, fixture.store.fill_count, 0U);
    DB_TEST_EXPECT_EQ_U32(state, fixture.store.command_count, 1U);

    init_policy_store(&fixture);
    const db_render_ir_fill_t sentinel = {
        .rect = {.x = 7, .y = 7, .width = 1, .height = 1},
        .color = {.rgba = {0.25, 0.5, 0.75, 1.0}},
    };
    fixture.fills[0] = sentinel;
    const db_render_ir_fill_t invalid_batch[] = {
        fill,
        {.rect = {.width = 0, .height = 1},
         .color = {.rgba = {0.0, 1.0, 0.0, 1.0}}},
    };
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_fill_rects(&fixture.store, 0U,
                                                  invalid_batch, 2U,
                                                  DB_RENDER_IR_INVALID_ID),
                          DB_RENDER_IR_INVALID);
    DB_TEST_EXPECT_EQ_SIZE(state, fixture.store.fill_count, 0U);
    DB_TEST_EXPECT_EQ_INT(state, fixture.fills[0].rect.x, sentinel.rect.x);
    DB_TEST_EXPECT_DOUBLE_EQUAL(state, fixture.fills[0].color.rgba[0],
                                sentinel.color.rgba[0]);
}

static void region_builders_restore_arenas_on_failure(db_test_state_t *state) {
    test_policy_store_t fixture = {0};
    init_policy_store(&fixture);
    db_render_ir_region_id_t existing = DB_RENDER_IR_INVALID_ID;
    (void)db_render_ir_add_rect_region(
        &fixture.store, (db_render_ir_rect_t){.width = 1, .height = 1},
        &existing);
    const size_t initial_regions = fixture.store.region_count;
    const size_t initial_bands = fixture.store.band_count;
    const size_t initial_spans = fixture.store.span_count;
    fixture.store.span_capacity = initial_spans + 1U;
    const db_render_ir_fill_t fragments[] = {
        {.rect = {.y = 2, .width = 1, .height = 1}},
        {.rect = {.x = 3, .y = 2, .width = 1, .height = 1}},
    };
    db_render_ir_region_id_t failed = DB_RENDER_IR_INVALID_ID;
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_add_fill_region(&fixture.store, fragments, 2U, &failed),
        DB_RENDER_IR_CAPACITY);
    DB_TEST_EXPECT_EQ_SIZE(state, fixture.store.region_count, initial_regions);
    DB_TEST_EXPECT_EQ_SIZE(state, fixture.store.band_count, initial_bands);
    DB_TEST_EXPECT_EQ_SIZE(state, fixture.store.span_count, initial_spans);

    fixture.store.span_capacity = TEST_POLICY_CAPACITY;
    db_render_ir_region_id_t rhs = DB_RENDER_IR_INVALID_ID;
    (void)db_render_ir_add_rect_region(
        &fixture.store, (db_render_ir_rect_t){.x = 3, .width = 1, .height = 1},
        &rhs);
    const size_t before_union_regions = fixture.store.region_count;
    const size_t before_union_bands = fixture.store.band_count;
    const size_t before_union_spans = fixture.store.span_count;
    fixture.store.span_capacity = before_union_spans + 1U;
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_region_union(&fixture.store, existing, rhs, &failed),
        DB_RENDER_IR_CAPACITY);
    DB_TEST_EXPECT_EQ_SIZE(state, fixture.store.region_count,
                           before_union_regions);
    DB_TEST_EXPECT_EQ_SIZE(state, fixture.store.band_count, before_union_bands);
    DB_TEST_EXPECT_EQ_SIZE(state, fixture.store.span_count, before_union_spans);
}

static void
region_builder_needs_only_normalized_capacity(db_test_state_t *state) {
    test_policy_store_t fixture = {0};
    init_policy_store(&fixture);
    fixture.store.span_capacity = 1U;
    const db_render_ir_fill_t overlapping[] = {
        {.rect = {.width = 4, .height = 2}},
        {.rect = {.x = 2, .width = 4, .height = 2}},
    };
    db_render_ir_region_id_t region = DB_RENDER_IR_INVALID_ID;
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_add_fill_region(&fixture.store, overlapping, 2U, &region),
        DB_RENDER_IR_OK);
    DB_TEST_EXPECT_EQ_SIZE(state, fixture.store.span_count, 1U);
    DB_TEST_EXPECT_EQ_INT(state, fixture.store.spans[0].x_start, 0);
    DB_TEST_EXPECT_EQ_INT(state, fixture.store.spans[0].x_end, 6);
}

static void empty_intersection_needs_no_band_capacity(db_test_state_t *state) {
    test_policy_store_t fixture = {0};
    init_policy_store(&fixture);
    fixture.store.band_capacity = 2U;
    db_render_ir_region_id_t lhs = DB_RENDER_IR_INVALID_ID;
    db_render_ir_region_id_t rhs = DB_RENDER_IR_INVALID_ID;
    (void)db_render_ir_add_rect_region(
        &fixture.store, (db_render_ir_rect_t){.width = 1, .height = 1}, &lhs);
    (void)db_render_ir_add_rect_region(
        &fixture.store,
        (db_render_ir_rect_t){.x = 2, .y = 2, .width = 1, .height = 1}, &rhs);
    DB_TEST_EXPECT_EQ_SIZE(state, fixture.store.band_count,
                           fixture.store.band_capacity);
    db_render_ir_region_id_t intersection = DB_RENDER_IR_INVALID_ID;
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_region_intersection(&fixture.store, lhs,
                                                           rhs, &intersection),
                          DB_RENDER_IR_OK);
    DB_TEST_EXPECT_TRUE(state, intersection != DB_RENDER_IR_INVALID_ID);
    DB_TEST_EXPECT_EQ_U32(state, fixture.store.regions[intersection].band_count,
                          0U);
    DB_TEST_EXPECT_EQ_SIZE(state, fixture.store.band_count, 2U);
}

static void validation_rejects_unnormalized_regions(db_test_state_t *state) {
    const db_render_ir_region_t region = {.band_count = 1U};
    const db_render_ir_band_t adjacent_span_band = {.y_end = 1,
                                                    .span_count = 2U};
    const db_render_ir_span_t adjacent_spans[] = {
        {.x_start = 0, .x_end = 2},
        {.x_start = 2, .x_end = 4},
    };
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_validate(&(const db_render_ir_view_t){
                              .regions = &region,
                              .region_count = 1U,
                              .bands = &adjacent_span_band,
                              .band_count = 1U,
                              .spans = adjacent_spans,
                              .span_count = 2U,
                          }),
                          DB_RENDER_IR_INVALID);

    const db_render_ir_region_t two_band_region = {.band_count = 2U};
    const db_render_ir_band_t adjacent_bands[] = {
        {.y_start = 0, .y_end = 2, .first_span = 0U, .span_count = 1U},
        {.y_start = 2, .y_end = 4, .first_span = 1U, .span_count = 1U},
    };
    const db_render_ir_span_t identical_spans[] = {
        {.x_start = 0, .x_end = 2},
        {.x_start = 0, .x_end = 2},
    };
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_validate(&(const db_render_ir_view_t){
                              .regions = &two_band_region,
                              .region_count = 1U,
                              .bands = adjacent_bands,
                              .band_count = 2U,
                              .spans = identical_spans,
                              .span_count = 2U,
                          }),
                          DB_RENDER_IR_INVALID);
}

static void metadata_rejects_counter_overflow(db_test_state_t *state) {
    test_policy_store_t fixture = {0};
    init_policy_store(&fixture);
    const db_render_ir_resource_id_t target =
        add_policy_target(state, &fixture);
    fixture.resources[target].height = INT32_MAX;
    const db_render_ir_rect_t bounds = {.width = 1, .height = INT32_MAX};
    const db_render_ir_color_t black = {.rgba = {0.0, 0.0, 0.0, 1.0}};
    const db_render_ir_color_t white = {.rgba = {1.0, 1.0, 1.0, 1.0}};
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_fill_linear_gradient(
                              &fixture.store, target, bounds, 0, INT32_MAX, 0,
                              black, white, DB_RENDER_IR_INVALID_ID),
                          DB_RENDER_IR_OK);
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_fill_linear_gradient(
                              &fixture.store, target, bounds, 0, INT32_MAX, 0,
                              black, white, DB_RENDER_IR_INVALID_ID),
                          DB_RENDER_IR_OK);
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_fill_linear_gradient(
                              &fixture.store, target, bounds, 0, INT32_MAX, 0,
                              black, white, DB_RENDER_IR_INVALID_ID),
                          DB_RENDER_IR_OK);
    const db_render_ir_view_t view = db_render_ir_store_view(&fixture.store);
    const db_render_ir_metadata_t metadata =
        db_render_ir_metadata(&view, DB_RENDER_IR_OK, 16U, 16U);
    DB_TEST_EXPECT_EQ_INT(state, metadata.status,
                          DB_RENDER_IR_ARITHMETIC_OVERFLOW);
    DB_TEST_EXPECT_EQ_INT(state, metadata.worker_partitionable, 0);
}

typedef struct {
    db_render_ir_rect_t coverage[2];
    size_t count;
} gradient_coverage_capture_t;

static int capture_gradient_coverage(
    void *opaque, db_render_ir_resource_id_t target,
    const db_render_ir_linear_gradient_command_t *gradient,
    db_render_ir_rect_t coverage) {
    gradient_coverage_capture_t *const capture =
        (gradient_coverage_capture_t *)opaque;
    if ((capture == NULL) || (target != 0U) || (gradient == NULL) ||
        (gradient->bounds.x != 0) || (gradient->bounds.y != 0) ||
        (gradient->bounds.width != 16) || (gradient->bounds.height != 16) ||
        (gradient->axis_start != 0) ||
        (gradient->axis_end != TEST_POLICY_GRADIENT_END) ||
        (capture->count >= 2U)) {
        return 0;
    }
    capture->coverage[capture->count++] = coverage;
    return 1;
}

static void clipped_lowering_preserves_gradient_axis(db_test_state_t *state) {
    test_policy_store_t fixture = {0};
    init_policy_store(&fixture);
    const db_render_ir_resource_id_t target =
        add_policy_target(state, &fixture);
    const db_render_ir_fill_t fragments[] = {
        {.rect = {.x = 1, .y = 2, .width = 3, .height = 2}},
        {.rect = {.x = 8, .y = 6, .width = 2, .height = 3}},
    };
    db_render_ir_region_id_t clip = DB_RENDER_IR_INVALID_ID;
    (void)db_render_ir_add_fill_region(&fixture.store, fragments, 2U, &clip);
    (void)db_render_ir_fill_linear_gradient(
        &fixture.store, target,
        (db_render_ir_rect_t){.width = 16, .height = 16}, 0,
        TEST_POLICY_GRADIENT_END, 0,
        (db_render_ir_color_t){.rgba = {1.0, 0.0, 0.0, 1.0}},
        (db_render_ir_color_t){.rgba = {0.0, 0.0, 1.0, 1.0}}, clip);
    const db_render_ir_view_t view = db_render_ir_store_view(&fixture.store);
    gradient_coverage_capture_t capture = {0};
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_lower(
            &view, (db_render_ir_external_binding_view_t){0},
            &(const db_render_ir_lowering_ops_t){.fill_linear_gradient =
                                                     capture_gradient_coverage},
            &capture),
        DB_RENDER_IR_OK);
    DB_TEST_EXPECT_EQ_SIZE(state, capture.count, 2U);
    DB_TEST_EXPECT_EQ_INT(state, capture.coverage[0].x, fragments[0].rect.x);
    DB_TEST_EXPECT_EQ_INT(state, capture.coverage[0].y, fragments[0].rect.y);
    DB_TEST_EXPECT_EQ_INT(state, capture.coverage[0].width,
                          fragments[0].rect.width);
    DB_TEST_EXPECT_EQ_INT(state, capture.coverage[0].height,
                          fragments[0].rect.height);
    DB_TEST_EXPECT_EQ_INT(state, capture.coverage[1].x, fragments[1].rect.x);
    DB_TEST_EXPECT_EQ_INT(state, capture.coverage[1].y, fragments[1].rect.y);
    DB_TEST_EXPECT_EQ_INT(state, capture.coverage[1].width,
                          fragments[1].rect.width);
    DB_TEST_EXPECT_EQ_INT(state, capture.coverage[1].height,
                          fragments[1].rect.height);

    db_render_ir_rect_iterator_t iterator = {0};
    db_render_ir_rect_iterator_begin(&iterator, &view);
    db_render_ir_fill_t fill = {0};
    size_t row_count = 0U;
    while (db_render_ir_rect_iterator_next(&iterator, &fill) != 0) {
        const int in_first =
            DB_BOOL((fill.rect.x == 1) && (fill.rect.y >= 2) &&
                    (fill.rect.y < 4) && (fill.rect.width == 3));
        const int in_second =
            DB_BOOL((fill.rect.x == 8) && (fill.rect.y >= 6) &&
                    (fill.rect.y < 9) && (fill.rect.width == 2));
        DB_TEST_EXPECT_TRUE(state, (in_first != 0) || (in_second != 0));
        const db_render_ir_color_t expected =
            db_render_ir_linear_gradient_color_at(
                DB_RENDER_IR_COMMAND_AS(db_render_ir_linear_gradient_command_t,
                                        iterator.active_command),
                fill.rect.y);
        DB_TEST_EXPECT_TRUE(
            state,
            (db_equal_f64(fill.color.rgba[0], expected.rgba[0]) != 0) &&
                (db_equal_f64(fill.color.rgba[1], expected.rgba[1]) != 0) &&
                (db_equal_f64(fill.color.rgba[2], expected.rgba[2]) != 0) &&
                (db_equal_f64(fill.color.rgba[3], expected.rgba[3]) != 0));
        row_count++;
    }
    DB_TEST_EXPECT_EQ_SIZE(state, row_count, 5U);
}

static void normalized_coverage_preserves_last_writer(db_test_state_t *state) {
    test_policy_store_t raw = {0};
    test_policy_store_t optimized = {0};
    db_render_ir_fill_t primary[TEST_POLICY_CAPACITY] = {0};
    db_render_ir_fill_t secondary[TEST_POLICY_CAPACITY] = {0};
    db_render_ir_band_t coverage_bands[TEST_POLICY_CAPACITY] = {0};
    db_render_ir_band_t coverage_band_scratch[TEST_POLICY_CAPACITY] = {0};
    db_render_ir_span_t coverage_spans[TEST_POLICY_CAPACITY] = {0};
    db_render_ir_span_t coverage_span_scratch[TEST_POLICY_CAPACITY] = {0};
    uint32_t raw_pixels[16U * 16U] = {0};
    uint32_t optimized_pixels[16U * 16U] = {0};
    init_policy_store(&raw);
    init_policy_store(&optimized);
    const db_render_ir_resource_id_t target = add_policy_target(state, &raw);
    const db_render_ir_fill_t fills[] = {
        {.rect = {.width = 12, .height = 8},
         .color = {.rgba = {1.0, 0.0, 0.0, 1.0}}},
        {.rect = {.x = 2, .y = 1, .width = 8, .height = 6},
         .color = {.rgba = {0.0, 1.0, 0.0, 1.0}}},
        {.rect = {.x = 4, .width = 2, .height = 8},
         .color = {.rgba = {0.0, 0.0, 1.0, 1.0}}},
        {.rect = {.y = 3, .width = 12, .height = 2},
         .color = {.rgba = {1.0, 1.0, 0.0, 1.0}}},
    };
    (void)db_render_ir_begin_target(&raw.store, target);
    (void)db_render_ir_fill_rects(&raw.store, target, fills,
                                  sizeof(fills) / sizeof(fills[0]),
                                  DB_RENDER_IR_INVALID_ID);
    (void)db_render_ir_end_target(&raw.store, target);
    const db_render_ir_view_t raw_view = db_render_ir_store_view(&raw.store);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_optimize(
            &raw_view, &optimized.store,
            (db_render_ir_optimizer_workspace_t){
                .primary = primary,
                .secondary = secondary,
                .coverage_bands = coverage_bands,
                .coverage_band_scratch = coverage_band_scratch,
                .coverage_spans = coverage_spans,
                .coverage_span_scratch = coverage_span_scratch,
                .capacity = TEST_POLICY_CAPACITY,
            }),
        DB_RENDER_IR_OK);
    const db_render_ir_view_t optimized_view =
        db_render_ir_store_view(&optimized.store);
    const db_pixel_surface_t raw_surface = {
        .pixels = raw_pixels,
        .pixel_width = 16U,
        .pixel_height = 16U,
        .format = DB_PIXEL_FORMAT_RGBA8,
    };
    const db_pixel_surface_t optimized_surface = {
        .pixels = optimized_pixels,
        .pixel_width = 16U,
        .pixel_height = 16U,
        .format = DB_PIXEL_FORMAT_RGBA8,
    };
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_rasterize_surface(&raw_view, 16U, 16U, &raw_surface),
        DB_RENDER_IR_OK);
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_rasterize_surface(
                              &optimized_view, 16U, 16U, &optimized_surface),
                          DB_RENDER_IR_OK);
    DB_TEST_EXPECT_TRUE(
        state, memcmp(raw_pixels, optimized_pixels, sizeof(raw_pixels)) == 0);
}

static void
optimizer_rejects_in_place_storage_transactionally(db_test_state_t *state) {
    test_policy_store_t raw = {0};
    db_render_ir_fill_t primary[TEST_POLICY_CAPACITY] = {0};
    db_render_ir_fill_t secondary[TEST_POLICY_CAPACITY] = {0};
    db_render_ir_band_t coverage_bands[TEST_POLICY_CAPACITY] = {0};
    db_render_ir_band_t coverage_band_scratch[TEST_POLICY_CAPACITY] = {0};
    db_render_ir_span_t coverage_spans[TEST_POLICY_CAPACITY] = {0};
    db_render_ir_span_t coverage_span_scratch[TEST_POLICY_CAPACITY] = {0};
    init_policy_store(&raw);
    const db_render_ir_resource_id_t target = add_policy_target(state, &raw);
    const db_render_ir_fill_t fill = {
        .rect = {.x = 1, .y = 2, .width = 3, .height = 4},
        .color = {.rgba = {0.25, 0.5, 0.75, 1.0}},
    };
    (void)db_render_ir_begin_target(&raw.store, target);
    (void)db_render_ir_fill_rects(&raw.store, target, &fill, 1U,
                                  DB_RENDER_IR_INVALID_ID);
    (void)db_render_ir_end_target(&raw.store, target);
    const db_render_ir_view_t raw_view = db_render_ir_store_view(&raw.store);
    const uint64_t hash = db_render_ir_hash(&raw_view);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_optimize(
            &raw_view, &raw.store,
            (db_render_ir_optimizer_workspace_t){
                .primary = primary,
                .secondary = secondary,
                .coverage_bands = coverage_bands,
                .coverage_band_scratch = coverage_band_scratch,
                .coverage_spans = coverage_spans,
                .coverage_span_scratch = coverage_span_scratch,
                .capacity = TEST_POLICY_CAPACITY,
            }),
        DB_RENDER_IR_INVALID);
    const db_render_ir_view_t retained = db_render_ir_store_view(&raw.store);
    DB_TEST_EXPECT_EQ_U64(state, db_render_ir_hash(&retained), hash);
    DB_TEST_EXPECT_EQ_INT(state, db_render_ir_validate(&retained),
                          DB_RENDER_IR_OK);
}

static void
optimizer_rejects_workspace_alias_transactionally(db_test_state_t *state) {
    test_policy_store_t raw = {0};
    test_policy_store_t optimized = {0};
    db_render_ir_fill_t secondary[TEST_POLICY_CAPACITY] = {0};
    db_render_ir_band_t coverage_bands[TEST_POLICY_CAPACITY] = {0};
    db_render_ir_band_t coverage_band_scratch[TEST_POLICY_CAPACITY] = {0};
    db_render_ir_span_t coverage_spans[TEST_POLICY_CAPACITY] = {0};
    db_render_ir_span_t coverage_span_scratch[TEST_POLICY_CAPACITY] = {0};
    init_policy_store(&raw);
    init_policy_store(&optimized);
    const db_render_ir_resource_id_t target = add_policy_target(state, &raw);
    const db_render_ir_fill_t fill = {
        .rect = {.x = 1, .y = 2, .width = 3, .height = 4},
        .color = {.rgba = {0.25, 0.5, 0.75, 1.0}},
    };
    (void)db_render_ir_begin_target(&raw.store, target);
    (void)db_render_ir_fill_rects(&raw.store, target, &fill, 1U,
                                  DB_RENDER_IR_INVALID_ID);
    (void)db_render_ir_end_target(&raw.store, target);
    const db_render_ir_view_t raw_view = db_render_ir_store_view(&raw.store);
    const uint64_t raw_hash = db_render_ir_hash(&raw_view);
    const db_render_ir_view_t optimized_before =
        db_render_ir_store_view(&optimized.store);
    const uint64_t optimized_hash = db_render_ir_hash(&optimized_before);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_optimize(
            &raw_view, &optimized.store,
            (db_render_ir_optimizer_workspace_t){
                .primary = raw.fills,
                .secondary = secondary,
                .coverage_bands = coverage_bands,
                .coverage_band_scratch = coverage_band_scratch,
                .coverage_spans = coverage_spans,
                .coverage_span_scratch = coverage_span_scratch,
                .capacity = TEST_POLICY_CAPACITY,
            }),
        DB_RENDER_IR_INVALID);
    const db_render_ir_view_t retained_raw =
        db_render_ir_store_view(&raw.store);
    const db_render_ir_view_t retained_optimized =
        db_render_ir_store_view(&optimized.store);
    DB_TEST_EXPECT_EQ_U64(state, db_render_ir_hash(&retained_raw), raw_hash);
    DB_TEST_EXPECT_EQ_U64(state, db_render_ir_hash(&retained_optimized),
                          optimized_hash);
}

static void adversarial_optimizer_work_is_bounded(db_test_state_t *state) {
    const size_t count = TEST_POLICY_STRESS_COMMANDS;
    const size_t record_size =
        DB_RENDER_IR_ALIGNED_RECORD_SIZE(db_render_ir_fill_command_t);
    const size_t command_bytes = (count + 2U) * record_size;
    const size_t command_units =
        (command_bytes + sizeof(max_align_t) - 1U) / sizeof(max_align_t);
    max_align_t *const raw_commands =
        (max_align_t *)calloc(command_units, sizeof(max_align_t));
    max_align_t *const optimized_commands =
        (max_align_t *)calloc(command_units, sizeof(max_align_t));
    db_render_ir_fill_t *const raw_fills =
        (db_render_ir_fill_t *)calloc(count, sizeof(db_render_ir_fill_t));
    db_render_ir_fill_t *const optimized_fills =
        (db_render_ir_fill_t *)calloc(count, sizeof(db_render_ir_fill_t));
    db_render_ir_fill_t *const primary =
        (db_render_ir_fill_t *)calloc(count, sizeof(db_render_ir_fill_t));
    db_render_ir_fill_t *const secondary =
        (db_render_ir_fill_t *)calloc(count, sizeof(db_render_ir_fill_t));
    db_render_ir_band_t *const coverage_bands =
        (db_render_ir_band_t *)calloc(count, sizeof(db_render_ir_band_t));
    db_render_ir_band_t *const coverage_band_scratch =
        (db_render_ir_band_t *)calloc(count, sizeof(db_render_ir_band_t));
    db_render_ir_span_t *const coverage_spans =
        (db_render_ir_span_t *)calloc(count, sizeof(db_render_ir_span_t));
    db_render_ir_span_t *const coverage_span_scratch =
        (db_render_ir_span_t *)calloc(count, sizeof(db_render_ir_span_t));
    db_render_ir_region_t *const optimized_regions =
        (db_render_ir_region_t *)calloc(count, sizeof(db_render_ir_region_t));
    db_render_ir_band_t *const optimized_bands =
        (db_render_ir_band_t *)calloc(count, sizeof(db_render_ir_band_t));
    db_render_ir_span_t *const optimized_spans =
        (db_render_ir_span_t *)calloc(count, sizeof(db_render_ir_span_t));
    if ((raw_commands == NULL) || (optimized_commands == NULL) ||
        (raw_fills == NULL) || (optimized_fills == NULL) || (primary == NULL) ||
        (secondary == NULL) || (coverage_bands == NULL) ||
        (coverage_band_scratch == NULL) || (coverage_spans == NULL) ||
        (coverage_span_scratch == NULL) || (optimized_regions == NULL) ||
        (optimized_bands == NULL) || (optimized_spans == NULL)) {
        DB_TEST_EXPECT_TRUE(state, 0);
        free(optimized_spans);
        free(optimized_bands);
        free(optimized_regions);
        free(coverage_span_scratch);
        free(coverage_spans);
        free(coverage_band_scratch);
        free(coverage_bands);
        free(secondary);
        free(primary);
        free(optimized_fills);
        free(raw_fills);
        free(optimized_commands);
        free(raw_commands);
        return;
    }
    db_render_ir_resource_t raw_resource[1] = {0};
    db_render_ir_resource_t optimized_resource[1] = {0};
    db_render_ir_store_t raw = {
        .commands = raw_commands,
        .command_capacity = command_units * sizeof(max_align_t),
        .fills = raw_fills,
        .fill_capacity = count,
        .resources = raw_resource,
        .resource_capacity = 1U,
    };
    db_render_ir_store_t optimized = {
        .commands = optimized_commands,
        .command_capacity = command_units * sizeof(max_align_t),
        .fills = optimized_fills,
        .fill_capacity = count,
        .resources = optimized_resource,
        .resource_capacity = 1U,
        .regions = optimized_regions,
        .region_capacity = count,
        .bands = optimized_bands,
        .band_capacity = count,
        .spans = optimized_spans,
        .span_capacity = count,
    };
    db_render_ir_resource_id_t target = DB_RENDER_IR_INVALID_ID;
    (void)db_render_ir_add_resource(
        &raw,
        &(const db_render_ir_resource_t){
            .kind = DB_RENDER_IR_RESOURCE_CANONICAL_TARGET,
            .width = TEST_POLICY_STRESS_COMMANDS,
            .height = 1U,
            .format = DB_PIXEL_FORMAT_RGBA8,
        },
        &target);
    (void)db_render_ir_begin_target(&raw, target);
    for (size_t index = 0U; index < count; index++) {
        const db_render_ir_fill_t fill = {
            .rect = {.x = (int32_t)index, .width = 1, .height = 1},
            .color = {.rgba = {1.0, 1.0, 1.0, 1.0}},
        };
        (void)db_render_ir_fill_rects(&raw, target, &fill, 1U,
                                      DB_RENDER_IR_INVALID_ID);
    }
    (void)db_render_ir_end_target(&raw, target);
    db_render_ir_optimizer_stats_t stats = {0};
    const db_render_ir_view_t raw_view = db_render_ir_store_view(&raw);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_optimize(
            &raw_view, &optimized,
            (db_render_ir_optimizer_workspace_t){
                .primary = primary,
                .secondary = secondary,
                .coverage_bands = coverage_bands,
                .coverage_band_scratch = coverage_band_scratch,
                .coverage_spans = coverage_spans,
                .coverage_span_scratch = coverage_span_scratch,
                .capacity = count,
                .stats = &stats,
            }),
        DB_RENDER_IR_OK);
    const uint64_t comparison_budget =
        UINT64_C(8) * count * optimized.span_capacity;
    DB_TEST_EXPECT_TRUE(state,
                        stats.band_comparisons + stats.span_comparisons <=
                            comparison_budget);
    DB_TEST_EXPECT_TRUE(state, stats.region_splits <= optimized.span_capacity);
    DB_TEST_EXPECT_TRUE(state, stats.emitted_spans <= optimized.span_capacity);
    DB_TEST_EXPECT_TRUE(state, stats.region_imports <= raw.command_count);
    const uint64_t sort_comparison_budget =
        (UINT64_C(2) * count * TEST_POLICY_STRESS_SORT_LEVELS) +
        (UINT64_C(2) * count);
    DB_TEST_EXPECT_TRUE(state,
                        stats.sort_merge_comparisons <= sort_comparison_budget);
    DB_TEST_EXPECT_EQ_SIZE(state, optimized.fill_count, 1U);
    DB_TEST_EXPECT_EQ_INT(state, optimized.status, DB_RENDER_IR_OK);

    db_render_ir_store_reset(&raw);
    db_render_ir_store_reset(&optimized);
    target = DB_RENDER_IR_INVALID_ID;
    (void)db_render_ir_add_resource(
        &raw,
        &(const db_render_ir_resource_t){
            .kind = DB_RENDER_IR_RESOURCE_CANONICAL_TARGET,
            .width = TEST_POLICY_STRESS_COMMANDS,
            .height = 1U,
            .format = DB_PIXEL_FORMAT_RGBA8,
        },
        &target);
    for (size_t index = 0U; index < count; index++) {
        raw_fills[index] = (db_render_ir_fill_t){
            .rect = {.x = (int32_t)index, .width = 1, .height = 1},
            .color = {.rgba = {1.0, 1.0, 1.0, 1.0}},
        };
    }
    (void)db_render_ir_begin_target(&raw, target);
    (void)db_render_ir_fill_rects(&raw, target, raw_fills, count,
                                  DB_RENDER_IR_INVALID_ID);
    (void)db_render_ir_invalidate_resource(&raw, target);
    (void)db_render_ir_end_target(&raw, target);
    stats = (db_render_ir_optimizer_stats_t){0};
    const db_render_ir_view_t batched_view = db_render_ir_store_view(&raw);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_optimize(
            &batched_view, &optimized,
            (db_render_ir_optimizer_workspace_t){
                .primary = primary,
                .secondary = secondary,
                .coverage_bands = coverage_bands,
                .coverage_band_scratch = coverage_band_scratch,
                .coverage_spans = coverage_spans,
                .coverage_span_scratch = coverage_span_scratch,
                .capacity = count,
                .stats = &stats,
            }),
        DB_RENDER_IR_OK);
    const db_render_ir_view_t optimized_batched_view =
        db_render_ir_store_view(&optimized);
    DB_TEST_EXPECT_EQ_INT(state, db_render_ir_validate(&optimized_batched_view),
                          DB_RENDER_IR_OK);
    DB_TEST_EXPECT_TRUE(state,
                        stats.band_comparisons + stats.span_comparisons <=
                            (UINT64_C(4) * (uint64_t)count));
    DB_TEST_EXPECT_EQ_SIZE(state, optimized.fill_count, count);
    DB_TEST_EXPECT_EQ_SIZE(state, optimized.region_count, 1U);
    free(optimized_spans);
    free(optimized_bands);
    free(optimized_regions);
    free(coverage_span_scratch);
    free(coverage_spans);
    free(coverage_band_scratch);
    free(coverage_bands);
    free(secondary);
    free(primary);
    free(optimized_fills);
    free(raw_fills);
    free(optimized_commands);
    free(raw_commands);
}

unsigned db_render_ir_policy_test_run_all(void) {
    static const db_test_case_t cases[] = {
        {"command_padding_does_not_affect_hash",
         command_padding_does_not_affect_hash},
        {"adjacent_merge_rejects_unrepresentable_extent",
         adjacent_merge_rejects_unrepresentable_extent},
        {"command_layout_baseline_is_stable",
         command_layout_baseline_is_stable},
        {"semantic_hash_includes_region_contents",
         semantic_hash_includes_region_contents},
        {"region_import_is_transactional", region_import_is_transactional},
        {"fill_append_failure_restores_fill_arena",
         fill_append_failure_restores_fill_arena},
        {"region_builders_restore_arenas_on_failure",
         region_builders_restore_arenas_on_failure},
        {"region_builder_needs_only_normalized_capacity",
         region_builder_needs_only_normalized_capacity},
        {"empty_intersection_needs_no_band_capacity",
         empty_intersection_needs_no_band_capacity},
        {"validation_rejects_unnormalized_regions",
         validation_rejects_unnormalized_regions},
        {"metadata_rejects_counter_overflow",
         metadata_rejects_counter_overflow},
        {"clipped_lowering_preserves_gradient_axis",
         clipped_lowering_preserves_gradient_axis},
        {"normalized_coverage_preserves_last_writer",
         normalized_coverage_preserves_last_writer},
        {"optimizer_rejects_in_place_storage_transactionally",
         optimizer_rejects_in_place_storage_transactionally},
        {"optimizer_rejects_workspace_alias_transactionally",
         optimizer_rejects_workspace_alias_transactionally},
        {"adversarial_optimizer_work_is_bounded",
         adversarial_optimizer_work_is_bounded},
    };
    return db_test_run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}
