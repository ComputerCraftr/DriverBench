#include "core/db_geometry.h"
#include "core/db_render_ir.h"
#include "core/db_render_ir_surface.h"
#include "core/db_render_types.h"
#include "support/test_harness.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    TEST_COMMAND_BYTES = 4096,
    TEST_CAPACITY = 64,
    TEST_WIDE_WIDTH = 100,
    TEST_GRADIENT_HEIGHT = 8,
    TEST_GRADIENT_START = 4,
    TEST_GRADIENT_END = 11,
    TEST_DAMAGE_SPLIT = 7,
    TEST_DAMAGE_GRADIENT_END = 38,
    TEST_UPLOAD_WIDTH = 10,
    TEST_UPLOAD_HEIGHT = 8,
    TEST_UPLOAD_BINDING_BYTES = 320,
    TEST_UPLOAD_BINDING_SHORT_BYTES = TEST_UPLOAD_BINDING_BYTES - 1,
    TEST_REGION_EXTENT = 10,
    TEST_OVERLAP_OFFSET = 5,
    TEST_RECT_EXTENT = 20,
    TEST_GRADIENT_ROWS = 32,
    TEST_GRADIENT_SAMPLE_ROW = 8,
    TEST_DAMAGE_AREA = 3900,
    TEST_MUTATED_RECT_X = 99,
    TEST_TARGET_HEIGHT = 80,
    TEST_TOUCHED_HEIGHT = 6,
};

static const double test_quarter = 0.25;
static const double test_half = 0.5;
static const double test_three_quarters = 0.75;
static const uint32_t test_ir_extent_overflow = (uint32_t)INT32_MAX + 1U;

typedef struct {
    max_align_t commands[TEST_COMMAND_BYTES / sizeof(max_align_t)];
    db_render_ir_fill_t fills[TEST_CAPACITY];
    db_render_ir_resource_t resources[4];
    db_render_ir_region_t regions[TEST_CAPACITY];
    db_render_ir_band_t bands[TEST_CAPACITY];
    db_render_ir_span_t spans[TEST_CAPACITY];
    db_render_ir_band_t optimizer_coverage_bands[TEST_CAPACITY];
    db_render_ir_band_t optimizer_coverage_band_scratch[TEST_CAPACITY];
    db_render_ir_span_t optimizer_coverage_spans[TEST_CAPACITY];
    db_render_ir_span_t optimizer_coverage_span_scratch[TEST_CAPACITY];
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

static db_render_ir_optimizer_workspace_t
optimizer_workspace(test_store_t *fixture, db_render_ir_fill_t *primary,
                    db_render_ir_fill_t *secondary) {
    return (db_render_ir_optimizer_workspace_t){
        .primary = primary,
        .secondary = secondary,
        .coverage_bands = fixture->optimizer_coverage_bands,
        .coverage_band_scratch = fixture->optimizer_coverage_band_scratch,
        .coverage_spans = fixture->optimizer_coverage_spans,
        .coverage_span_scratch = fixture->optimizer_coverage_span_scratch,
        .capacity = TEST_CAPACITY,
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
                .format = DB_PIXEL_FORMAT_RGBA8,
            },
            &target),
        DB_RENDER_IR_OK);
    return target;
}

static void extent_conversion_is_checked(db_test_state_t *state) {
    db_render_ir_rect_t rect = {0};
    DB_TEST_EXPECT_TRUE(state,
                        db_render_ir_rect_from_extent(100U, 80U, &rect) != 0);
    DB_TEST_EXPECT_EQ_INT(state, rect.width, 100);
    DB_TEST_EXPECT_EQ_INT(state, rect.height, 80);
    DB_TEST_EXPECT_TRUE(state, db_render_ir_rect_from_extent(
                                   test_ir_extent_overflow, 1U, &rect) == 0);
    DB_TEST_EXPECT_TRUE(state, db_render_ir_rect_from_extent(
                                   1U, test_ir_extent_overflow, &rect) == 0);
}

static void packed_iteration_is_aligned(db_test_state_t *state) {
    test_store_t fixture = {0};
    init_store(&fixture);
    const db_render_ir_resource_id_t target = add_target(state, &fixture);
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_begin_target(&fixture.store, target),
                          DB_RENDER_IR_OK);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_fill_rects(&fixture.store, target,
                                &(const db_render_ir_fill_t){
                                    .rect = {.width = 8, .height = 8},
                                    .color = {.rgba = {0.25, 0.5, 0.75, 1.0}}},
                                1U, DB_RENDER_IR_INVALID_ID),
        DB_RENDER_IR_OK);
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_end_target(&fixture.store, target),
                          DB_RENDER_IR_OK);
    db_render_ir_region_id_t damage = DB_RENDER_IR_INVALID_ID;
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_add_rect_region(
                              &fixture.store,
                              (db_render_ir_rect_t){.width = 8, .height = 8},
                              &damage),
                          DB_RENDER_IR_OK);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_set_last_command_regions(&fixture.store, damage, damage),
        DB_RENDER_IR_OK);
    const db_render_ir_view_t view = db_render_ir_store_view(&fixture.store);
    DB_TEST_EXPECT_EQ_INT(state, db_render_ir_validate(&view), DB_RENDER_IR_OK);
    db_render_ir_iterator_t iterator = {0};
    db_render_ir_iterator_begin(&iterator, &view);
    const db_render_ir_command_header_t *first =
        db_render_ir_iterator_next(&iterator);
    DB_TEST_EXPECT_TRUE(state, first != NULL);
    DB_TEST_EXPECT_EQ_U32(state, first->sequence, 0U);
    const db_render_ir_command_header_t *second =
        db_render_ir_iterator_next(&iterator);
    DB_TEST_EXPECT_TRUE(state, second != NULL);
    DB_TEST_EXPECT_EQ_U32(state, second->sequence, 1U);
    const db_render_ir_command_header_t *third =
        db_render_ir_iterator_next(&iterator);
    DB_TEST_EXPECT_TRUE(state, third != NULL);
    DB_TEST_EXPECT_EQ_U32(state, third->sequence, 2U);
    DB_TEST_EXPECT_EQ_U32(state, third->touched_region, damage);
    DB_TEST_EXPECT_TRUE(state, db_render_ir_iterator_next(&iterator) == NULL);
}

static void optimizer_preserves_fragmented_clip_pixels(db_test_state_t *state) {
    test_store_t raw = {0};
    test_store_t optimized = {0};
    db_render_ir_fill_t primary[TEST_CAPACITY] = {0};
    db_render_ir_fill_t secondary[TEST_CAPACITY] = {0};
    static uint32_t raw_pixels[TEST_WIDE_WIDTH * TEST_TARGET_HEIGHT];
    static uint32_t optimized_pixels[TEST_WIDE_WIDTH * TEST_TARGET_HEIGHT];
    memset(raw_pixels, 0, sizeof(raw_pixels));
    memset(optimized_pixels, 0, sizeof(optimized_pixels));
    init_store(&raw);
    init_store(&optimized);
    const db_render_ir_resource_id_t target = add_target(state, &raw);
    db_render_ir_region_id_t unused = DB_RENDER_IR_INVALID_ID;
    (void)db_render_ir_add_rect_region(
        &raw.store, (db_render_ir_rect_t){.width = 1, .height = 1}, &unused);
    (void)db_render_ir_add_rect_region(
        &raw.store, (db_render_ir_rect_t){.x = 2, .width = 1, .height = 1},
        &unused);
    const db_render_ir_fill_t clip_fills[] = {
        {.rect = {.x = 3, .y = 4, .width = 5, .height = 2}},
        {.rect = {.x = 11, .y = 7, .width = 4, .height = 3}},
    };
    db_render_ir_region_id_t raw_clip = DB_RENDER_IR_INVALID_ID;
    (void)db_render_ir_add_fill_region(&raw.store, clip_fills, 2U, &raw_clip);
    const db_render_ir_fill_t full = {
        .rect = {.width = TEST_WIDE_WIDTH, .height = TEST_TARGET_HEIGHT},
        .color = {.rgba = {1.0, test_quarter, test_half, 1.0}},
    };
    (void)db_render_ir_begin_target(&raw.store, target);
    (void)db_render_ir_fill_rects(&raw.store, target, &full, 1U, raw_clip);
    (void)db_render_ir_end_target(&raw.store, target);
    const db_render_ir_view_t raw_view = db_render_ir_store_view(&raw.store);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_optimize(
            &raw_view, &optimized.store,
            optimizer_workspace(&optimized, primary, secondary)),
        DB_RENDER_IR_OK);
    const db_render_ir_view_t optimized_view =
        db_render_ir_store_view(&optimized.store);
    db_render_ir_iterator_t iterator = {0};
    db_render_ir_iterator_begin(&iterator, &optimized_view);
    (void)db_render_ir_iterator_next(&iterator);
    const db_render_ir_command_header_t *const optimized_fill =
        db_render_ir_iterator_next(&iterator);
    DB_TEST_EXPECT_TRUE(state, optimized_fill != NULL);
    if (optimized_fill == NULL) {
        return;
    }
    DB_TEST_EXPECT_TRUE(state,
                        optimized_fill->clip_region != DB_RENDER_IR_INVALID_ID);
    DB_TEST_EXPECT_TRUE(state, optimized_fill->clip_region != raw_clip);
    DB_TEST_EXPECT_EQ_U64(state,
                          db_render_ir_region_area(
                              &optimized_view, optimized_fill->touched_region),
                          22U);
    const db_pixel_surface_t raw_surface = {
        .pixels = raw_pixels,
        .pixel_width = TEST_WIDE_WIDTH,
        .pixel_height = TEST_TARGET_HEIGHT,
        .format = DB_PIXEL_FORMAT_RGBA8,
    };
    const db_pixel_surface_t optimized_surface = {
        .pixels = optimized_pixels,
        .pixel_width = TEST_WIDE_WIDTH,
        .pixel_height = TEST_TARGET_HEIGHT,
        .format = DB_PIXEL_FORMAT_RGBA8,
    };
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_rasterize_surface(&raw_view, TEST_WIDE_WIDTH,
                                       TEST_TARGET_HEIGHT, &raw_surface),
        DB_RENDER_IR_OK);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_rasterize_surface(&optimized_view, TEST_WIDE_WIDTH,
                                       TEST_TARGET_HEIGHT, &optimized_surface),
        DB_RENDER_IR_OK);
    DB_TEST_EXPECT_TRUE(
        state, memcmp(raw_pixels, optimized_pixels, sizeof(raw_pixels)) == 0);
}

static void optimizer_canonicalizes_full_target_clip(db_test_state_t *state) {
    test_store_t raw = {0};
    test_store_t optimized = {0};
    db_render_ir_fill_t primary[TEST_CAPACITY] = {0};
    db_render_ir_fill_t secondary[TEST_CAPACITY] = {0};
    init_store(&raw);
    init_store(&optimized);
    const db_render_ir_resource_id_t target = add_target(state, &raw);
    db_render_ir_region_id_t clip = DB_RENDER_IR_INVALID_ID;
    (void)db_render_ir_add_rect_region(
        &raw.store,
        (db_render_ir_rect_t){.width = TEST_WIDE_WIDTH,
                              .height = TEST_TARGET_HEIGHT},
        &clip);
    (void)db_render_ir_begin_target(&raw.store, target);
    (void)db_render_ir_fill_rects(
        &raw.store, target,
        &(const db_render_ir_fill_t){
            .rect = {.width = TEST_WIDE_WIDTH, .height = TEST_TARGET_HEIGHT},
            .color = {.rgba = {test_quarter, test_half, test_three_quarters,
                               1.0}}},
        1U, clip);
    (void)db_render_ir_end_target(&raw.store, target);
    const db_render_ir_view_t raw_view = db_render_ir_store_view(&raw.store);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_optimize(
            &raw_view, &optimized.store,
            optimizer_workspace(&optimized, primary, secondary)),
        DB_RENDER_IR_OK);
    const db_render_ir_view_t optimized_view =
        db_render_ir_store_view(&optimized.store);
    db_render_ir_iterator_t iterator = {0};
    db_render_ir_iterator_begin(&iterator, &optimized_view);
    (void)db_render_ir_iterator_next(&iterator);
    const db_render_ir_command_header_t *const fill =
        db_render_ir_iterator_next(&iterator);
    DB_TEST_EXPECT_TRUE(state, fill != NULL);
    DB_TEST_EXPECT_EQ_U32(state, fill->clip_region, DB_RENDER_IR_INVALID_ID);
    DB_TEST_EXPECT_TRUE(state, fill->touched_region != DB_RENDER_IR_INVALID_ID);
    DB_TEST_EXPECT_EQ_U64(
        state, db_render_ir_region_area(&optimized_view, fill->touched_region),
        (uint64_t)TEST_WIDE_WIDTH * 80U);
}

static void overwrite_eliminates_hidden_fill(db_test_state_t *state) {
    test_store_t raw = {0};
    test_store_t optimized = {0};
    db_render_ir_fill_t primary[TEST_CAPACITY] = {0};
    db_render_ir_fill_t secondary[TEST_CAPACITY] = {0};
    init_store(&raw);
    init_store(&optimized);
    const db_render_ir_resource_id_t target = add_target(state, &raw);
    const db_render_ir_fill_t fills[2] = {
        {.rect = {.width = 40, .height = 30},
         .color = {.rgba = {0.2, 0.2, 0.2, 1.0}}},
        {.rect = {.width = 40, .height = 30},
         .color = {.rgba = {0.0, 1.0, 0.0, 1.0}}},
    };
    (void)db_render_ir_begin_target(&raw.store, target);
    (void)db_render_ir_fill_rects(&raw.store, target, fills, 2U,
                                  DB_RENDER_IR_INVALID_ID);
    (void)db_render_ir_end_target(&raw.store, target);
    const db_render_ir_view_t raw_view = db_render_ir_store_view(&raw.store);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_optimize(
            &raw_view, &optimized.store,
            optimizer_workspace(&optimized, primary, secondary)),
        DB_RENDER_IR_OK);
    DB_TEST_EXPECT_EQ_SIZE(state, optimized.store.fill_count, 1U);
    DB_TEST_EXPECT_DOUBLE_EQUAL(state, optimized.store.fills[0].color.rgba[1],
                                1.0);
}

static void
partial_overwrite_preserves_visible_remainder(db_test_state_t *state) {
    test_store_t raw = {0};
    test_store_t optimized = {0};
    db_render_ir_fill_t primary[TEST_CAPACITY] = {0};
    db_render_ir_fill_t secondary[TEST_CAPACITY] = {0};
    init_store(&raw);
    init_store(&optimized);
    const db_render_ir_resource_id_t target = add_target(state, &raw);
    const db_render_ir_fill_t fills[2] = {
        {.rect = {.width = TEST_RECT_EXTENT, .height = TEST_REGION_EXTENT},
         .color = {.rgba = {1.0, 0.0, 0.0, 1.0}}},
        {.rect = {.x = TEST_REGION_EXTENT,
                  .width = TEST_RECT_EXTENT,
                  .height = TEST_REGION_EXTENT},
         .color = {.rgba = {0.0, 0.0, 1.0, 1.0}}},
    };
    (void)db_render_ir_begin_target(&raw.store, target);
    (void)db_render_ir_fill_rects(&raw.store, target, fills, 2U,
                                  DB_RENDER_IR_INVALID_ID);
    (void)db_render_ir_end_target(&raw.store, target);
    const db_render_ir_view_t raw_view = db_render_ir_store_view(&raw.store);
    (void)db_render_ir_optimize(
        &raw_view, &optimized.store,
        optimizer_workspace(&optimized, primary, secondary));
    DB_TEST_EXPECT_EQ_SIZE(state, optimized.store.fill_count, 2U);
    uint64_t area = 0U;
    for (size_t index = 0U; index < optimized.store.fill_count; index++) {
        area += db_render_ir_rect_area(optimized.store.fills[index].rect);
    }
    DB_TEST_EXPECT_EQ_U64(state, area, 300U);
}

static void capacity_failure_is_typed(db_test_state_t *state) {
    test_store_t fixture = {0};
    init_store(&fixture);
    fixture.store.fill_capacity = 1U;
    const db_render_ir_resource_id_t target = add_target(state, &fixture);
    const db_render_ir_fill_t fills[2] = {
        {.rect = {.width = 1, .height = 1}},
        {.rect = {.x = 2, .width = 1, .height = 1}},
    };
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_fill_rects(&fixture.store, target, fills,
                                                  2U, DB_RENDER_IR_INVALID_ID),
                          DB_RENDER_IR_CAPACITY);
}

static void
rectangle_conversion_rejects_invalid_bounds(db_test_state_t *state) {
    db_grid_block_t block = {0};
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_rect_to_grid_block(
            (db_render_ir_rect_t){.x = 2, .y = 3, .width = 4, .height = 5}, 10U,
            10U, &block),
        1);
    DB_TEST_EXPECT_EQ_U32(state, block.col_start, 2U);
    DB_TEST_EXPECT_EQ_U32(state, block.row_start, 3U);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_rect_to_grid_block(
            (db_render_ir_rect_t){.x = -1, .width = 1, .height = 1}, 10U, 10U,
            &block),
        0);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_rect_to_grid_block(
            (db_render_ir_rect_t){.x = 9, .width = 2, .height = 1}, 10U, 10U,
            &block),
        0);
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_rect_to_grid_block(
                              (db_render_ir_rect_t){.x = INT32_MAX,
                                                    .width = INT32_MAX,
                                                    .height = 1},
                              UINT32_MAX - 2U, 10U, &block),
                          0);
}

static void
validation_accepts_clippable_out_of_target_fills(db_test_state_t *state) {
    test_store_t fixture = {0};
    init_store(&fixture);
    const db_render_ir_resource_id_t target = add_target(state, &fixture);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_fill_rects(&fixture.store, target,
                                &(const db_render_ir_fill_t){
                                    .rect = {.x = 99, .width = 2, .height = 1},
                                    .color = {.rgba = {0.0, 0.0, 0.0, 1.0}}},
                                1U, DB_RENDER_IR_INVALID_ID),
        DB_RENDER_IR_OK);
    const db_render_ir_view_t view = db_render_ir_store_view(&fixture.store);
    DB_TEST_EXPECT_EQ_INT(state, db_render_ir_validate(&view), DB_RENDER_IR_OK);
}

static void rectangle_endpoint_overflow_is_rejected_transactionally(
    db_test_state_t *state) {
    enum {
        REJECTED_X_ENDPOINT = 17,
        REJECTED_Y_ENDPOINT = 23,
    };
    test_store_t fixture = {0};
    init_store(&fixture);
    const db_render_ir_resource_id_t target = add_target(state, &fixture);
    const size_t command_size = fixture.store.command_size;
    const size_t command_count = fixture.store.command_count;
    const size_t fill_count = fixture.store.fill_count;
    const db_render_ir_status_t status = fixture.store.status;
    const db_render_ir_fill_t invalid = {
        .rect = {.x = INT32_MAX, .width = 1, .height = 1},
        .color = {.rgba = {0.0, 0.0, 0.0, 1.0}},
    };
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_fill_rects(&fixture.store, target,
                                                  &invalid, 1U,
                                                  DB_RENDER_IR_INVALID_ID),
                          DB_RENDER_IR_INVALID);
    DB_TEST_EXPECT_EQ_SIZE(state, fixture.store.command_size, command_size);
    DB_TEST_EXPECT_EQ_SIZE(state, fixture.store.command_count, command_count);
    DB_TEST_EXPECT_EQ_SIZE(state, fixture.store.fill_count, fill_count);
    DB_TEST_EXPECT_EQ_INT(state, fixture.store.status, status);

    const db_render_ir_fill_t valid = {
        .rect = {.width = 1, .height = 1},
        .color = {.rgba = {0.0, 0.0, 0.0, 1.0}},
    };
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_fill_rects(&fixture.store, target,
                                                  &valid, 1U,
                                                  DB_RENDER_IR_INVALID_ID),
                          DB_RENDER_IR_OK);
    fixture.fills[0].rect = invalid.rect;
    const db_render_ir_view_t malformed =
        db_render_ir_store_view(&fixture.store);
    DB_TEST_EXPECT_EQ_INT(state, db_render_ir_validate(&malformed),
                          DB_RENDER_IR_INVALID);
    db_render_ir_rect_iterator_t iterator = {0};
    db_render_ir_fill_t decoded = {0};
    db_render_ir_rect_iterator_begin(&iterator, &malformed);
    DB_TEST_EXPECT_TRUE(
        state, db_render_ir_rect_iterator_next(&iterator, &decoded) == 0);

    int32_t x_end = REJECTED_X_ENDPOINT;
    int32_t y_end = REJECTED_Y_ENDPOINT;
    DB_TEST_EXPECT_TRUE(
        state, db_render_ir_rect_endpoints(invalid.rect, &x_end, &y_end) == 0);
    DB_TEST_EXPECT_EQ_INT(state, x_end, REJECTED_X_ENDPOINT);
    DB_TEST_EXPECT_EQ_INT(state, y_end, REJECTED_Y_ENDPOINT);
}

static void validation_rejects_invalid_upload_bounds(db_test_state_t *state) {
    test_store_t fixture = {0};
    init_store(&fixture);
    const db_render_ir_resource_id_t target = add_target(state, &fixture);
    db_render_ir_resource_id_t source = DB_RENDER_IR_INVALID_ID;
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_add_resource(
                              &fixture.store,
                              &(const db_render_ir_resource_t){
                                  .kind = DB_RENDER_IR_RESOURCE_RASTER_SOURCE,
                                  .width = TEST_UPLOAD_WIDTH,
                                  .height = TEST_UPLOAD_HEIGHT,
                                  .format = DB_PIXEL_FORMAT_RGBA8,
                              },
                              &source),
                          DB_RENDER_IR_OK);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_upload_image(
            &fixture.store, target, source,
            (db_render_ir_rect_t){.width = TEST_UPLOAD_WIDTH,
                                  .height = TEST_UPLOAD_HEIGHT},
            95, 0,
            (db_render_ir_upload_semantics_t){
                .replacement = DB_RENDER_IR_UPLOAD_REPLACE_EXACT,
                .filter = DB_RENDER_IR_FILTER_NEAREST,
                .conversion = DB_RENDER_IR_CONVERSION_EXACT,
                .prior_content = DB_RENDER_IR_PRIOR_CONTENT_INDEPENDENT,
                .opacity = 1.0}),
        DB_RENDER_IR_OK);
    const db_render_ir_view_t view = db_render_ir_store_view(&fixture.store);
    DB_TEST_EXPECT_EQ_INT(state, db_render_ir_validate(&view),
                          DB_RENDER_IR_INVALID);
}

static void validation_rejects_missing_counted_storage(db_test_state_t *state) {
    const db_render_ir_view_t missing_resources = {
        .resource_count = 1U,
    };
    DB_TEST_EXPECT_EQ_INT(state, db_render_ir_validate(&missing_resources),
                          DB_RENDER_IR_INVALID);
    const db_render_ir_view_t missing_spans = {
        .spans = NULL,
        .span_count = 1U,
    };
    DB_TEST_EXPECT_EQ_INT(state, db_render_ir_validate(&missing_spans),
                          DB_RENDER_IR_INVALID);
    const db_render_ir_view_t missing_commands = {
        .command_size = sizeof(db_render_ir_command_header_t),
        .command_count = 1U,
    };
    db_render_ir_iterator_t iterator = {0};
    db_render_ir_iterator_begin(&iterator, &missing_commands);
    DB_TEST_EXPECT_TRUE(state, db_render_ir_iterator_next(&iterator) == NULL);
    DB_TEST_EXPECT_EQ_U64(state, db_render_ir_hash(&missing_commands), 0U);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_metadata(&missing_commands, DB_RENDER_IR_OK, 1U, 1U)
            .status,
        DB_RENDER_IR_INVALID);
}

static void
truncated_command_header_is_rejected_safely(db_test_state_t *state) {
    const max_align_t truncated = {0};
    const db_render_ir_view_t view = {
        .commands = &truncated,
        .command_size = 1U,
        .command_count = 1U,
    };
    db_render_ir_iterator_t iterator = {0};
    db_render_ir_iterator_begin(&iterator, &view);
    DB_TEST_EXPECT_TRUE(state, db_render_ir_iterator_next(&iterator) == NULL);
    DB_TEST_EXPECT_EQ_INT(state, db_render_ir_validate(&view),
                          DB_RENDER_IR_INVALID);
}

static void semantic_hash_includes_command_payload(db_test_state_t *state) {
    test_store_t lhs = {0};
    test_store_t rhs = {0};
    init_store(&lhs);
    init_store(&rhs);
    const db_render_ir_resource_id_t lhs_target = add_target(state, &lhs);
    const db_render_ir_resource_id_t rhs_target = add_target(state, &rhs);
    (void)db_render_ir_clear(
        &lhs.store, lhs_target,
        (db_render_ir_color_t){
            .rgba = {test_quarter, test_half, test_three_quarters, 1.0}},
        DB_RENDER_IR_INVALID_ID);
    (void)db_render_ir_clear(
        &rhs.store, rhs_target,
        (db_render_ir_color_t){
            .rgba = {test_quarter, test_half, test_half, 1.0}},
        DB_RENDER_IR_INVALID_ID);
    const uint64_t lhs_hash = db_render_ir_hash(&(const db_render_ir_view_t){
        .commands = lhs.store.commands,
        .command_size = lhs.store.command_size,
        .command_count = lhs.store.command_count,
        .resources = lhs.store.resources,
        .resource_count = lhs.store.resource_count,
    });
    const uint64_t rhs_hash = db_render_ir_hash(&(const db_render_ir_view_t){
        .commands = rhs.store.commands,
        .command_size = rhs.store.command_size,
        .command_count = rhs.store.command_count,
        .resources = rhs.store.resources,
        .resource_count = rhs.store.resource_count,
    });
    DB_TEST_EXPECT_TRUE(state, lhs_hash != rhs_hash);
}

typedef struct {
    unsigned uploads;
    unsigned gradients;
} test_lowering_context_t;

static int
count_gradient(void *opaque_context, db_render_ir_resource_id_t target,
               const db_render_ir_linear_gradient_command_t *gradient,
               db_render_ir_rect_t coverage) {
    test_lowering_context_t *const context =
        (test_lowering_context_t *)opaque_context;
    if ((context == NULL) || (target != 0U) || (gradient == NULL) ||
        (gradient->bounds.width != TEST_WIDE_WIDTH) ||
        (gradient->bounds.height != TEST_GRADIENT_HEIGHT) ||
        (coverage.width != TEST_WIDE_WIDTH) ||
        (coverage.height != TEST_GRADIENT_HEIGHT) ||
        (gradient->axis_start != TEST_GRADIENT_START) ||
        (gradient->axis_end != TEST_GRADIENT_END)) {
        return 0;
    }
    context->gradients++;
    return 1;
}

static void
gradient_survives_optimization_and_lowering(db_test_state_t *state) {
    test_store_t raw = {0};
    test_store_t optimized = {0};
    db_render_ir_fill_t primary[TEST_CAPACITY] = {0};
    db_render_ir_fill_t secondary[TEST_CAPACITY] = {0};
    init_store(&raw);
    init_store(&optimized);
    const db_render_ir_resource_id_t target = add_target(state, &raw);
    (void)db_render_ir_begin_target(&raw.store, target);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_fill_linear_gradient(
            &raw.store, target,
            (db_render_ir_rect_t){.y = TEST_GRADIENT_START,
                                  .width = TEST_WIDE_WIDTH,
                                  .height = TEST_GRADIENT_HEIGHT},
            TEST_GRADIENT_START, TEST_GRADIENT_END, 0,
            (db_render_ir_color_t){
                .rgba = {test_half, test_half, test_half, 1.0}},
            (db_render_ir_color_t){.rgba = {0.0, 1.0, 0.0, 1.0}},
            DB_RENDER_IR_INVALID_ID),
        DB_RENDER_IR_OK);
    (void)db_render_ir_end_target(&raw.store, target);
    const db_render_ir_view_t raw_view = db_render_ir_store_view(&raw.store);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_optimize(
            &raw_view, &optimized.store,
            optimizer_workspace(&optimized, primary, secondary)),
        DB_RENDER_IR_OK);
    const db_render_ir_view_t optimized_view =
        db_render_ir_store_view(&optimized.store);
    db_render_ir_iterator_t iterator = {0};
    db_render_ir_iterator_begin(&iterator, &optimized_view);
    (void)db_render_ir_iterator_next(&iterator);
    const db_render_ir_command_header_t *const command =
        db_render_ir_iterator_next(&iterator);
    DB_TEST_EXPECT_TRUE(state, command != NULL);
    DB_TEST_EXPECT_EQ_INT(state, command->opcode,
                          DB_RENDER_IR_OP_FILL_LINEAR_GRADIENT);
    const db_render_ir_linear_gradient_command_t *const gradient =
        DB_RENDER_IR_COMMAND_AS(db_render_ir_linear_gradient_command_t,
                                command);
    const db_render_ir_color_t midpoint = db_render_ir_linear_gradient_color_at(
        gradient, TEST_GRADIENT_SAMPLE_ROW);
    DB_TEST_EXPECT_TRUE(state, midpoint.rgba[0] < test_half);
    DB_TEST_EXPECT_TRUE(state, midpoint.rgba[1] > test_half);
    test_lowering_context_t context = {0};
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_lower(&optimized_view,
                           (db_render_ir_external_binding_view_t){0},
                           &(const db_render_ir_lowering_ops_t){
                               .fill_linear_gradient = count_gradient},
                           &context),
        DB_RENDER_IR_OK);
    DB_TEST_EXPECT_EQ_U32(state, context.gradients, 1U);
}

static void ordered_stream_damage_unions_all_writes(db_test_state_t *state) {
    test_store_t raw = {0};
    test_store_t optimized = {0};
    db_render_ir_fill_t primary[TEST_CAPACITY] = {0};
    db_render_ir_fill_t secondary[TEST_CAPACITY] = {0};
    init_store(&raw);
    init_store(&optimized);
    const db_render_ir_resource_id_t target = add_target(state, &raw);
    (void)db_render_ir_begin_target(&raw.store, target);
    (void)db_render_ir_fill_rects(
        &raw.store, target,
        &(const db_render_ir_fill_t){
            .rect = {.width = TEST_WIDE_WIDTH, .height = TEST_DAMAGE_SPLIT},
            .color = {.rgba = {test_half, test_half, test_half, 1.0}}},
        1U, DB_RENDER_IR_INVALID_ID);
    (void)db_render_ir_fill_linear_gradient(
        &raw.store, target,
        (db_render_ir_rect_t){.y = TEST_DAMAGE_SPLIT,
                              .width = TEST_WIDE_WIDTH,
                              .height = TEST_GRADIENT_ROWS},
        TEST_DAMAGE_SPLIT, TEST_DAMAGE_GRADIENT_END, 1,
        (db_render_ir_color_t){.rgba = {test_half, test_half, test_half, 1.0}},
        (db_render_ir_color_t){.rgba = {0.0, 1.0, 0.0, 1.0}},
        DB_RENDER_IR_INVALID_ID);
    (void)db_render_ir_end_target(&raw.store, target);
    const db_render_ir_view_t raw_view = db_render_ir_store_view(&raw.store);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_optimize(
            &raw_view, &optimized.store,
            optimizer_workspace(&optimized, primary, secondary)),
        DB_RENDER_IR_OK);
    const db_render_ir_view_t optimized_view =
        db_render_ir_store_view(&optimized.store);
    const db_render_ir_region_id_t damage =
        db_render_ir_final_damage_region(&optimized_view);
    DB_TEST_EXPECT_TRUE(state, damage != DB_RENDER_IR_INVALID_ID);
    DB_TEST_EXPECT_EQ_U64(state,
                          db_render_ir_region_area(&optimized_view, damage),
                          TEST_DAMAGE_AREA);
}

static int count_upload(void *opaque_context, db_render_ir_resource_id_t target,
                        const db_render_ir_upload_command_t *upload,
                        db_render_ir_external_binding_view_t bindings) {
    test_lowering_context_t *const context =
        (test_lowering_context_t *)opaque_context;
    db_render_ir_external_binding_t binding = {0};
    if ((context == NULL) || (upload == NULL) || (target != 0U) ||
        (upload->source != 1U) ||
        (upload->source_rect.width != TEST_UPLOAD_WIDTH) ||
        (upload->source_rect.height != TEST_UPLOAD_HEIGHT) ||
        (upload->destination_x != 2) || (upload->destination_y != 3) ||
        (bindings.count != 1U) ||
        (db_render_ir_find_binding(bindings, upload->source, &binding) == 0)) {
        return 0;
    }
    context->uploads++;
    return 1;
}

static void upload_survives_optimization_and_lowering(db_test_state_t *state) {
    test_store_t raw = {0};
    test_store_t optimized = {0};
    db_render_ir_fill_t primary[TEST_CAPACITY] = {0};
    db_render_ir_fill_t secondary[TEST_CAPACITY] = {0};
    init_store(&raw);
    init_store(&optimized);
    const db_render_ir_resource_id_t target = add_target(state, &raw);
    db_render_ir_resource_id_t source = DB_RENDER_IR_INVALID_ID;
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_add_resource(
                              &raw.store,
                              &(const db_render_ir_resource_t){
                                  .kind = DB_RENDER_IR_RESOURCE_RASTER_SOURCE,
                                  .width = TEST_UPLOAD_WIDTH,
                                  .height = TEST_UPLOAD_HEIGHT,
                                  .format = DB_PIXEL_FORMAT_RGBA8,
                              },
                              &source),
                          DB_RENDER_IR_OK);
    (void)db_render_ir_begin_target(&raw.store, target);
    (void)db_render_ir_upload_image(
        &raw.store, target, source,
        (db_render_ir_rect_t){.width = TEST_UPLOAD_WIDTH,
                              .height = TEST_UPLOAD_HEIGHT},
        2, 3,
        (db_render_ir_upload_semantics_t){
            .replacement = DB_RENDER_IR_UPLOAD_REPLACE_EXACT,
            .filter = DB_RENDER_IR_FILTER_NEAREST,
            .conversion = DB_RENDER_IR_CONVERSION_EXACT,
            .prior_content = DB_RENDER_IR_PRIOR_CONTENT_INDEPENDENT,
            .opacity = 1.0});
    (void)db_render_ir_end_target(&raw.store, target);
    const db_render_ir_view_t raw_view = db_render_ir_store_view(&raw.store);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_optimize(
            &raw_view, &optimized.store,
            optimizer_workspace(&optimized, primary, secondary)),
        DB_RENDER_IR_OK);
    const db_render_ir_view_t optimized_view =
        db_render_ir_store_view(&optimized.store);
    test_lowering_context_t context = {0};
    const db_render_ir_external_binding_t valid_binding = {
        .resource = source,
        .width = TEST_UPLOAD_WIDTH,
        .height = TEST_UPLOAD_HEIGHT,
        .format = DB_PIXEL_FORMAT_RGBA8,
        .row_stride_bytes = 40U,
        .size_bytes = TEST_UPLOAD_BINDING_BYTES,
        .pixels = &context,
    };
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_validate_bindings(
                              &optimized_view,
                              (db_render_ir_external_binding_view_t){
                                  .bindings = &valid_binding, .count = 1U}),
                          DB_RENDER_IR_OK);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_validate_bindings(
            &optimized_view, (db_render_ir_external_binding_view_t){0}),
        DB_RENDER_IR_INVALID);
    const db_render_ir_external_binding_t short_stride_binding = {
        .resource = source,
        .width = TEST_UPLOAD_WIDTH,
        .height = TEST_UPLOAD_HEIGHT,
        .format = DB_PIXEL_FORMAT_RGBA8,
        .row_stride_bytes = 4U,
        .size_bytes = 32U,
        .pixels = &context,
    };
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_validate_bindings(
            &optimized_view,
            (db_render_ir_external_binding_view_t){
                .bindings = &short_stride_binding, .count = 1U}),
        DB_RENDER_IR_INVALID);
    db_render_ir_external_binding_t undersized_binding = valid_binding;
    undersized_binding.size_bytes = TEST_UPLOAD_BINDING_SHORT_BYTES;
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_validate_bindings(
            &optimized_view,
            (db_render_ir_external_binding_view_t){
                .bindings = &undersized_binding, .count = 1U}),
        DB_RENDER_IR_INVALID);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_lower(
            &optimized_view,
            (db_render_ir_external_binding_view_t){
                .bindings = &undersized_binding, .count = 1U},
            &(const db_render_ir_lowering_ops_t){.upload_image = count_upload},
            &context),
        DB_RENDER_IR_INVALID);
    DB_TEST_EXPECT_EQ_U32(state, context.uploads, 0U);

    db_render_ir_view_t malformed_view = optimized_view;
    malformed_view.resources = NULL;
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_lower(
            &malformed_view,
            (db_render_ir_external_binding_view_t){.bindings = &valid_binding,
                                                   .count = 1U},
            &(const db_render_ir_lowering_ops_t){.upload_image = count_upload},
            &context),
        DB_RENDER_IR_INVALID);
    DB_TEST_EXPECT_EQ_U32(state, context.uploads, 0U);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_lower(
            &optimized_view,
            (db_render_ir_external_binding_view_t){.bindings = &valid_binding,
                                                   .count = 1U},
            &(const db_render_ir_lowering_ops_t){.upload_image = count_upload},
            &context),
        DB_RENDER_IR_OK);
    DB_TEST_EXPECT_EQ_U32(state, context.uploads, 1U);
}

static void y_banded_region_boolean_operations(db_test_state_t *state) {
    test_store_t fixture = {0};
    init_store(&fixture);
    db_render_ir_region_id_t lhs = DB_RENDER_IR_INVALID_ID;
    db_render_ir_region_id_t rhs = DB_RENDER_IR_INVALID_ID;
    db_render_ir_region_id_t joined = DB_RENDER_IR_INVALID_ID;
    db_render_ir_region_id_t overlap = DB_RENDER_IR_INVALID_ID;
    db_render_ir_region_id_t remainder = DB_RENDER_IR_INVALID_ID;
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_add_rect_region(
            &fixture.store,
            (db_render_ir_rect_t){.width = TEST_REGION_EXTENT,
                                  .height = TEST_REGION_EXTENT},
            &lhs),
        DB_RENDER_IR_OK);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_add_rect_region(
            &fixture.store,
            (db_render_ir_rect_t){.x = TEST_OVERLAP_OFFSET,
                                  .width = TEST_REGION_EXTENT,
                                  .height = TEST_REGION_EXTENT},
            &rhs),
        DB_RENDER_IR_OK);
    DB_TEST_EXPECT_EQ_INT(
        state, db_render_ir_region_union(&fixture.store, lhs, rhs, &joined),
        DB_RENDER_IR_OK);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_region_intersection(&fixture.store, lhs, rhs, &overlap),
        DB_RENDER_IR_OK);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_region_subtract(&fixture.store, lhs, rhs, &remainder),
        DB_RENDER_IR_OK);
    const db_render_ir_view_t view = db_render_ir_store_view(&fixture.store);
    DB_TEST_EXPECT_EQ_U64(state, db_render_ir_region_area(&view, joined), 150U);
    DB_TEST_EXPECT_EQ_U64(state, db_render_ir_region_area(&view, overlap), 50U);
    DB_TEST_EXPECT_EQ_U64(state, db_render_ir_region_area(&view, remainder),
                          50U);
}

unsigned db_render_ir_test_run_all(void) {
    static const db_test_case_t cases[] = {
        {"packed_iteration_is_aligned", packed_iteration_is_aligned},
        {"optimizer_preserves_fragmented_clip_pixels",
         optimizer_preserves_fragmented_clip_pixels},
        {"optimizer_canonicalizes_full_target_clip",
         optimizer_canonicalizes_full_target_clip},
        {"extent_conversion_is_checked", extent_conversion_is_checked},
        {"overwrite_eliminates_hidden_fill", overwrite_eliminates_hidden_fill},
        {"partial_overwrite_preserves_visible_remainder",
         partial_overwrite_preserves_visible_remainder},
        {"capacity_failure_is_typed", capacity_failure_is_typed},
        {"rectangle_conversion_rejects_invalid_bounds",
         rectangle_conversion_rejects_invalid_bounds},
        {"validation_accepts_clippable_out_of_target_fills",
         validation_accepts_clippable_out_of_target_fills},
        {"rectangle_endpoint_overflow_is_rejected_transactionally",
         rectangle_endpoint_overflow_is_rejected_transactionally},
        {"validation_rejects_invalid_upload_bounds",
         validation_rejects_invalid_upload_bounds},
        {"validation_rejects_missing_counted_storage",
         validation_rejects_missing_counted_storage},
        {"truncated_command_header_is_rejected_safely",
         truncated_command_header_is_rejected_safely},
        {"semantic_hash_includes_command_payload",
         semantic_hash_includes_command_payload},
        {"y_banded_region_boolean_operations",
         y_banded_region_boolean_operations},
        {"upload_survives_optimization_and_lowering",
         upload_survives_optimization_and_lowering},
        {"gradient_survives_optimization_and_lowering",
         gradient_survives_optimization_and_lowering},
        {"ordered_stream_damage_unions_all_writes",
         ordered_stream_damage_unions_all_writes},
    };
    return db_test_run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}
