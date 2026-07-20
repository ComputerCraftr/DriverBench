#include "core/db_render_ir.h"
#include "core/db_render_types.h"
#include "support/test_harness.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    TEST_MALFORMED_COMMAND_BYTES = 1024,
    TEST_MALFORMED_CAPACITY = 16,
    TEST_MALFORMED_MISSING_COMMAND_CAPACITY = 128,
};

typedef struct {
    max_align_t commands[TEST_MALFORMED_COMMAND_BYTES / sizeof(max_align_t)];
    db_render_ir_fill_t fills[TEST_MALFORMED_CAPACITY];
    db_render_ir_resource_t resources[2];
    db_render_ir_region_t regions[TEST_MALFORMED_CAPACITY];
    db_render_ir_band_t bands[TEST_MALFORMED_CAPACITY];
    db_render_ir_span_t spans[TEST_MALFORMED_CAPACITY];
    db_render_ir_store_t store;
} malformed_store_t;

static void init_malformed_store(malformed_store_t *fixture) {
    *fixture = (malformed_store_t){0};
    fixture->store = (db_render_ir_store_t){
        .commands = fixture->commands,
        .command_capacity = sizeof(fixture->commands),
        .fills = fixture->fills,
        .fill_capacity = TEST_MALFORMED_CAPACITY,
        .resources = fixture->resources,
        .resource_capacity = 2U,
        .regions = fixture->regions,
        .region_capacity = TEST_MALFORMED_CAPACITY,
        .bands = fixture->bands,
        .band_capacity = TEST_MALFORMED_CAPACITY,
        .spans = fixture->spans,
        .span_capacity = TEST_MALFORMED_CAPACITY,
    };
}

static db_render_ir_resource_id_t
add_malformed_target(db_test_state_t *state, malformed_store_t *fixture) {
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
static void ir_identity_counters_do_not_wrap(db_test_state_t *state) {
    malformed_store_t fixture = {0};
    init_malformed_store(&fixture);
    const db_render_ir_resource_id_t target =
        add_malformed_target(state, &fixture);
    fixture.store.next_sequence = UINT32_MAX;
    const size_t initial_command_size = fixture.store.command_size;
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_clear(&fixture.store, target,
                           (db_render_ir_color_t){.rgba = {0.0, 0.0, 0.0, 1.0}},
                           DB_RENDER_IR_INVALID_ID),
        DB_RENDER_IR_ARITHMETIC_OVERFLOW);
    DB_TEST_EXPECT_EQ_SIZE(state, fixture.store.command_size,
                           initial_command_size);
    DB_TEST_EXPECT_EQ_U32(state, fixture.store.next_sequence, UINT32_MAX);

    init_malformed_store(&fixture);
    fixture.store.resource_count = UINT32_MAX;
    fixture.store.resource_capacity = SIZE_MAX;
    db_render_ir_resource_id_t resource_id = DB_RENDER_IR_INVALID_ID;
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_add_resource(
            &fixture.store,
            &(const db_render_ir_resource_t){
                .kind = DB_RENDER_IR_RESOURCE_CANONICAL_TARGET,
                .width = 1U,
                .height = 1U,
                .format = DB_PIXEL_FORMAT_RGBA8,
            },
            &resource_id),
        DB_RENDER_IR_CAPACITY);
    DB_TEST_EXPECT_EQ_U32(state, resource_id, DB_RENDER_IR_INVALID_ID);
}

static void truncated_last_command_update_is_safe(db_test_state_t *state) {
    malformed_store_t fixture = {0};
    init_malformed_store(&fixture);
    fixture.store.command_size = 1U;
    fixture.store.command_count = 1U;
    fixture.store.region_count = 1U;
    DB_TEST_EXPECT_EQ_INT(
        state, db_render_ir_set_last_command_regions(&fixture.store, 0U, 0U),
        DB_RENDER_IR_INVALID);
}

static void missing_ir_arenas_are_rejected(db_test_state_t *state) {
    db_render_ir_store_t store = {.command_capacity =
                                      TEST_MALFORMED_MISSING_COMMAND_CAPACITY};
    DB_TEST_EXPECT_EQ_INT(state, db_render_ir_begin_target(&store, 0U),
                          DB_RENDER_IR_INVALID);

    store = (db_render_ir_store_t){.resource_capacity = 1U};
    db_render_ir_resource_id_t resource = DB_RENDER_IR_INVALID_ID;
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_add_resource(
            &store,
            &(const db_render_ir_resource_t){
                .kind = DB_RENDER_IR_RESOURCE_CANONICAL_TARGET,
                .width = 1U,
                .height = 1U,
                .format = DB_PIXEL_FORMAT_RGBA8,
            },
            &resource),
        DB_RENDER_IR_INVALID);

    store = (db_render_ir_store_t){
        .region_capacity = 1U, .band_capacity = 1U, .span_capacity = 1U};
    db_render_ir_region_id_t region = DB_RENDER_IR_INVALID_ID;
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_add_rect_region(
            &store, (db_render_ir_rect_t){.width = 1, .height = 1}, &region),
        DB_RENDER_IR_INVALID);

    const db_render_ir_region_t malformed_region = {.band_count = 1U};
    const db_render_ir_view_t malformed_source = {
        .regions = &malformed_region, .region_count = 1U, .band_count = 1U};
    malformed_store_t destination = {0};
    init_malformed_store(&destination);
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_region_import(&malformed_source, 0U,
                                                     &destination.store,
                                                     &region),
                          DB_RENDER_IR_INVALID);
}

static void stale_last_command_offset_does_not_mutate(db_test_state_t *state) {
    malformed_store_t fixture = {0};
    init_malformed_store(&fixture);
    const db_render_ir_resource_id_t target =
        add_malformed_target(state, &fixture);
    db_render_ir_region_id_t region = DB_RENDER_IR_INVALID_ID;
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_add_rect_region(
                              &fixture.store,
                              (db_render_ir_rect_t){.width = 1, .height = 1},
                              &region),
                          DB_RENDER_IR_OK);
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_begin_target(&fixture.store, target),
                          DB_RENDER_IR_OK);
    unsigned char before[DB_RENDER_IR_ALIGNED_RECORD_SIZE(
        db_render_ir_command_header_t)] = {0};
    memcpy(before, fixture.store.commands, sizeof(before));
    fixture.store.last_command_offset = fixture.store.command_size;
    DB_TEST_EXPECT_EQ_INT(
        state, db_render_ir_set_last_command_ordering(&fixture.store, 1U, 2U),
        DB_RENDER_IR_INVALID);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_set_last_command_regions(&fixture.store, region, region),
        DB_RENDER_IR_INVALID);
    const unsigned char *const after =
        (const unsigned char *)fixture.store.commands;
    int unchanged = 1;
    for (size_t index = 0U; index < sizeof(before); index++) {
        unchanged &= before[index] == after[index];
    }
    DB_TEST_EXPECT_TRUE(state, unchanged != 0);
}

static void validation_rejects_wrapped_region_ranges(db_test_state_t *state) {
    const db_render_ir_region_t region = {.first_band = UINT32_MAX,
                                          .band_count = 2U};
    const db_render_ir_band_t band = {0};
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_validate(&(const db_render_ir_view_t){
                              .regions = &region,
                              .region_count = 1U,
                              .bands = &band,
                              .band_count = 1U,
                          }),
                          DB_RENDER_IR_INVALID);
}

static void view_validation_rejects_overlapping_arenas(db_test_state_t *state) {
    union {
        db_render_ir_region_t region;
        db_render_ir_band_t band;
    } shared = {.band = {
                    .y_start = 0,
                    .y_end = 1,
                    .first_span = 0U,
                    .span_count = 1U,
                }};
    const db_render_ir_span_t span = {.x_start = 0, .x_end = 1};
    const db_render_ir_view_t overlapping = {
        .regions = &shared.region,
        .region_count = 1U,
        .bands = &shared.band,
        .band_count = 1U,
        .spans = &span,
        .span_count = 1U,
    };
    DB_TEST_EXPECT_EQ_INT(state, db_render_ir_validate(&overlapping),
                          DB_RENDER_IR_INVALID);
}

static void malformed_region_queries_are_bounded(db_test_state_t *state) {
    const db_render_ir_region_t regions[] = {
        {.first_band = UINT32_MAX, .band_count = 2U},
        {.first_band = 0U, .band_count = 1U},
    };
    const db_render_ir_band_t band = {
        .y_start = 0,
        .y_end = 1,
        .first_span = UINT32_MAX,
        .span_count = 2U,
    };
    const db_render_ir_span_t span = {.x_start = 0, .x_end = 1};
    const db_render_ir_view_t malformed = {
        .regions = regions,
        .region_count = 2U,
        .bands = &band,
        .band_count = 1U,
        .spans = &span,
        .span_count = 1U,
    };
    size_t span_count = SIZE_MAX;
    uint32_t row_span_count = UINT32_MAX;
    DB_TEST_EXPECT_TRUE(
        state, db_render_ir_region_validate(&malformed, 0U, &span_count) == 0);
    DB_TEST_EXPECT_EQ_SIZE(state, span_count, SIZE_MAX);
    DB_TEST_EXPECT_EQ_U64(state, db_render_ir_region_area(&malformed, 0U), 0U);
    DB_TEST_EXPECT_TRUE(state,
                        db_render_ir_regions_equal(&malformed, 0U, 1U) == 0);
    DB_TEST_EXPECT_TRUE(state, db_render_ir_region_row_span_count(
                                   &malformed, 1U, &row_span_count) == 0);
    DB_TEST_EXPECT_EQ_U32(state, row_span_count, UINT32_MAX);
}

static void malformed_region_algebra_does_not_publish(db_test_state_t *state) {
    malformed_store_t fixture = {0};
    init_malformed_store(&fixture);
    fixture.store.region_count = 2U;
    fixture.store.regions[0] =
        (db_render_ir_region_t){.first_band = UINT32_MAX, .band_count = 1U};
    fixture.store.regions[1] = (db_render_ir_region_t){0};
    const size_t region_count = fixture.store.region_count;
    const size_t band_count = fixture.store.band_count;
    const size_t span_count = fixture.store.span_count;
    db_render_ir_region_id_t output = UINT32_C(0xA5A5A5A5);
    DB_TEST_EXPECT_EQ_INT(
        state, db_render_ir_region_union(&fixture.store, 0U, 1U, &output),
        DB_RENDER_IR_INVALID);
    DB_TEST_EXPECT_EQ_SIZE(state, fixture.store.region_count, region_count);
    DB_TEST_EXPECT_EQ_SIZE(state, fixture.store.band_count, band_count);
    DB_TEST_EXPECT_EQ_SIZE(state, fixture.store.span_count, span_count);
    DB_TEST_EXPECT_EQ_U32(state, output, UINT32_C(0xA5A5A5A5));
}

static void
cross_aliased_region_import_does_not_publish(db_test_state_t *state) {
    const db_render_ir_region_t source_region = {
        .first_band = 0U,
        .band_count = 1U,
    };
    union {
        db_render_ir_band_t band;
        db_render_ir_span_t span;
    } source_storage = {.band = {
                            .y_start = 0,
                            .y_end = 1,
                            .first_span = 0U,
                            .span_count = 1U,
                        }};
    const db_render_ir_band_t source_band_before = source_storage.band;
    const db_render_ir_span_t source_span = {.x_start = 0, .x_end = 1};
    db_render_ir_region_t destination_regions[1] = {0};
    db_render_ir_band_t destination_bands[1] = {0};
    db_render_ir_store_t destination = {
        .regions = destination_regions,
        .region_capacity = 1U,
        .bands = destination_bands,
        .band_capacity = 1U,
        .spans = &source_storage.span,
        .span_capacity = 1U,
    };
    const db_render_ir_view_t source = {
        .regions = &source_region,
        .region_count = 1U,
        .bands = &source_storage.band,
        .band_count = 1U,
        .spans = &source_span,
        .span_count = 1U,
    };
    db_render_ir_region_id_t output = UINT32_C(0xa5a5a5a5);
    DB_TEST_EXPECT_EQ_INT(
        state, db_render_ir_region_import(&source, 0U, &destination, &output),
        DB_RENDER_IR_INVALID);
    DB_TEST_EXPECT_EQ_SIZE(state, destination.region_count, 0U);
    DB_TEST_EXPECT_EQ_SIZE(state, destination.band_count, 0U);
    DB_TEST_EXPECT_EQ_SIZE(state, destination.span_count, 0U);
    DB_TEST_EXPECT_EQ_U32(state, output, UINT32_C(0xa5a5a5a5));
    DB_TEST_EXPECT_EQ_INT(state, source_storage.band.y_start,
                          source_band_before.y_start);
    DB_TEST_EXPECT_EQ_INT(state, source_storage.band.y_end,
                          source_band_before.y_end);
    DB_TEST_EXPECT_EQ_U32(state, source_storage.band.first_span,
                          source_band_before.first_span);
    DB_TEST_EXPECT_EQ_U32(state, source_storage.band.span_count,
                          source_band_before.span_count);
}

static void same_arena_region_import_does_not_publish(db_test_state_t *state) {
    malformed_store_t fixture = {0};
    init_malformed_store(&fixture);
    db_render_ir_region_id_t source_region = DB_RENDER_IR_INVALID_ID;
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_add_rect_region(
            &fixture.store,
            (db_render_ir_rect_t){.x = 2, .y = 3, .width = 4, .height = 5},
            &source_region),
        DB_RENDER_IR_OK);
    const size_t region_count = fixture.store.region_count;
    const size_t band_count = fixture.store.band_count;
    const size_t span_count = fixture.store.span_count;
    const db_render_ir_region_t region_before =
        fixture.store.regions[source_region];
    const db_render_ir_band_t band_before = fixture.store.bands[0];
    const db_render_ir_span_t span_before = fixture.store.spans[0];
    db_render_ir_region_id_t output = UINT32_C(0xa5a5a5a5);
    const db_render_ir_view_t source = db_render_ir_store_view(&fixture.store);
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_region_import(&source, source_region,
                                                     &fixture.store, &output),
                          DB_RENDER_IR_INVALID);
    DB_TEST_EXPECT_EQ_SIZE(state, fixture.store.region_count, region_count);
    DB_TEST_EXPECT_EQ_SIZE(state, fixture.store.band_count, band_count);
    DB_TEST_EXPECT_EQ_SIZE(state, fixture.store.span_count, span_count);
    DB_TEST_EXPECT_EQ_U32(state, output, UINT32_C(0xa5a5a5a5));
    DB_TEST_EXPECT_EQ_U32(state,
                          fixture.store.regions[source_region].first_band,
                          region_before.first_band);
    DB_TEST_EXPECT_EQ_U32(state,
                          fixture.store.regions[source_region].band_count,
                          region_before.band_count);
    DB_TEST_EXPECT_EQ_INT(state, fixture.store.bands[0].y_start,
                          band_before.y_start);
    DB_TEST_EXPECT_EQ_INT(state, fixture.store.bands[0].y_end,
                          band_before.y_end);
    DB_TEST_EXPECT_EQ_U32(state, fixture.store.bands[0].first_span,
                          band_before.first_span);
    DB_TEST_EXPECT_EQ_U32(state, fixture.store.bands[0].span_count,
                          band_before.span_count);
    DB_TEST_EXPECT_EQ_INT(state, fixture.store.spans[0].x_start,
                          span_before.x_start);
    DB_TEST_EXPECT_EQ_INT(state, fixture.store.spans[0].x_end,
                          span_before.x_end);
}

unsigned db_render_ir_malformed_test_run_all(void) {
    static const db_test_case_t cases[] = {
        {"ir_identity_counters_do_not_wrap", ir_identity_counters_do_not_wrap},
        {"truncated_last_command_update_is_safe",
         truncated_last_command_update_is_safe},
        {"missing_ir_arenas_are_rejected", missing_ir_arenas_are_rejected},
        {"stale_last_command_offset_does_not_mutate",
         stale_last_command_offset_does_not_mutate},
        {"validation_rejects_wrapped_region_ranges",
         validation_rejects_wrapped_region_ranges},
        {"view_validation_rejects_overlapping_arenas",
         view_validation_rejects_overlapping_arenas},
        {"malformed_region_queries_are_bounded",
         malformed_region_queries_are_bounded},
        {"malformed_region_algebra_does_not_publish",
         malformed_region_algebra_does_not_publish},
        {"cross_aliased_region_import_does_not_publish",
         cross_aliased_region_import_does_not_publish},
        {"same_arena_region_import_does_not_publish",
         same_arena_region_import_does_not_publish},
    };
    return db_test_run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}
