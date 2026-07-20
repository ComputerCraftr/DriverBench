#include "core/db_hash.h"
#include "core/db_numeric.h"
#include "core/db_render_ir.h"
#include "core/db_render_ir_surface.h"
#include "core/db_render_types.h"
#include "support/test_harness.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    TEST_CLIP_COMMAND_BYTES = 1024,
    TEST_CLIP_CAPACITY = 16,
    TEST_CLIP_GRADIENT_END = 15,
};

typedef struct {
    max_align_t commands[TEST_CLIP_COMMAND_BYTES / sizeof(max_align_t)];
    db_render_ir_fill_t fills[TEST_CLIP_CAPACITY];
    db_render_ir_resource_t resources[2];
    db_render_ir_region_t regions[TEST_CLIP_CAPACITY];
    db_render_ir_band_t bands[TEST_CLIP_CAPACITY];
    db_render_ir_span_t spans[TEST_CLIP_CAPACITY];
    db_render_ir_store_t store;
} test_clip_store_t;

static void init_clip_store(test_clip_store_t *fixture) {
    *fixture = (test_clip_store_t){0};
    fixture->store = (db_render_ir_store_t){
        .commands = fixture->commands,
        .command_capacity = sizeof(fixture->commands),
        .fills = fixture->fills,
        .fill_capacity = TEST_CLIP_CAPACITY,
        .resources = fixture->resources,
        .resource_capacity = 2U,
        .regions = fixture->regions,
        .region_capacity = TEST_CLIP_CAPACITY,
        .bands = fixture->bands,
        .band_capacity = TEST_CLIP_CAPACITY,
        .spans = fixture->spans,
        .span_capacity = TEST_CLIP_CAPACITY,
    };
}

static db_render_ir_resource_id_t add_clip_target(db_test_state_t *state,
                                                  test_clip_store_t *fixture) {
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

static db_render_ir_status_t
optimize_clip_store(const db_render_ir_view_t *raw,
                    test_clip_store_t *optimized,
                    db_render_ir_optimizer_stats_t *stats) {
    db_render_ir_fill_t primary[TEST_CLIP_CAPACITY] = {0};
    db_render_ir_fill_t secondary[TEST_CLIP_CAPACITY] = {0};
    db_render_ir_band_t coverage_bands[TEST_CLIP_CAPACITY] = {0};
    db_render_ir_band_t coverage_band_scratch[TEST_CLIP_CAPACITY] = {0};
    db_render_ir_span_t coverage_spans[TEST_CLIP_CAPACITY] = {0};
    db_render_ir_span_t coverage_span_scratch[TEST_CLIP_CAPACITY] = {0};
    return db_render_ir_optimize(
        raw, &optimized->store,
        (db_render_ir_optimizer_workspace_t){
            .primary = primary,
            .secondary = secondary,
            .coverage_bands = coverage_bands,
            .coverage_band_scratch = coverage_band_scratch,
            .coverage_spans = coverage_spans,
            .coverage_span_scratch = coverage_span_scratch,
            .capacity = TEST_CLIP_CAPACITY,
            .stats = stats,
        });
}

static int oracle_point_in_rect(db_render_ir_rect_t rect, uint32_t column,
                                uint32_t row) {
    const int64_t x_end = (int64_t)rect.x + rect.width;
    const int64_t y_end = (int64_t)rect.y + rect.height;
    return DB_BOOL(((int64_t)column >= rect.x) && ((int64_t)column < x_end) &&
                   ((int64_t)row >= rect.y) && ((int64_t)row < y_end));
}

static int oracle_point_in_region(const db_render_ir_view_t *view,
                                  db_render_ir_region_id_t region_id,
                                  uint32_t column, uint32_t row) {
    if (region_id == DB_RENDER_IR_INVALID_ID) {
        return 1;
    }
    if ((view == NULL) || (region_id >= view->region_count)) {
        return 0;
    }
    const db_render_ir_region_t region = view->regions[region_id];
    for (uint32_t band_offset = 0U; band_offset < region.band_count;
         band_offset++) {
        const db_render_ir_band_t band =
            view->bands[region.first_band + band_offset];
        if (((int64_t)row < band.y_start) || ((int64_t)row >= band.y_end)) {
            continue;
        }
        for (uint32_t span_offset = 0U; span_offset < band.span_count;
             span_offset++) {
            const db_render_ir_span_t span =
                view->spans[band.first_span + span_offset];
            if (((int64_t)column >= span.x_start) &&
                ((int64_t)column < span.x_end)) {
                return 1;
            }
        }
    }
    return 0;
}

static uint32_t oracle_pack_color(db_render_ir_color_t color) {
    return db_pack_rgba8888_from_rgb01(color.rgba[0], color.rgba[1],
                                       color.rgba[2], UINT8_MAX);
}

static db_render_ir_color_t
oracle_gradient_color(const db_render_ir_linear_gradient_command_t *gradient,
                      uint32_t row) {
    int64_t clamped = row;
    if (clamped < gradient->axis_start) {
        clamped = gradient->axis_start;
    }
    if (clamped > gradient->axis_end) {
        clamped = gradient->axis_end;
    }
    double amount = 0.0;
    if (gradient->axis_end > gradient->axis_start) {
        amount = DB_TO_F64(clamped - gradient->axis_start) /
                 DB_TO_F64((int64_t)gradient->axis_end - gradient->axis_start);
    }
    if (gradient->reverse_stops != 0U) {
        amount = 1.0 - amount;
    }
    db_render_ir_color_t result = {0};
    for (size_t channel = 0U; channel < 4U; channel++) {
        result.rgba[channel] = gradient->start_color.rgba[channel] +
                               ((gradient->end_color.rgba[channel] -
                                 gradient->start_color.rgba[channel]) *
                                amount);
    }
    return result;
}

static int oracle_rasterize(const db_render_ir_view_t *view,
                            uint32_t pixels[16U * 16U]) {
    if ((view == NULL) || (pixels == NULL)) {
        return 0;
    }
    db_render_ir_iterator_t iterator = {0};
    db_render_ir_iterator_begin(&iterator, view);
    const db_render_ir_command_header_t *command = NULL;
    while ((command = db_render_ir_iterator_next(&iterator)) != NULL) {
        if ((command->opcode == DB_RENDER_IR_OP_BEGIN_TARGET) ||
            (command->opcode == DB_RENDER_IR_OP_END_TARGET)) {
            continue;
        }
        for (uint32_t y = 0U; y < 16U; y++) {
            for (uint32_t x = 0U; x < 16U; x++) {
                if (oracle_point_in_region(view, command->clip_region, x, y) ==
                    0) {
                    continue;
                }
                db_render_ir_color_t color = {0};
                int covered = 0;
                if (command->opcode == DB_RENDER_IR_OP_CLEAR) {
                    color = DB_RENDER_IR_COMMAND_AS(
                                db_render_ir_clear_command_t, command)
                                ->color;
                    covered = 1;
                } else if (command->opcode == DB_RENDER_IR_OP_FILL_RECTS) {
                    const db_render_ir_fill_command_t *const fills =
                        DB_RENDER_IR_COMMAND_AS(db_render_ir_fill_command_t,
                                                command);
                    for (uint32_t index = 0U; index < fills->fill_count;
                         index++) {
                        const db_render_ir_fill_t fill =
                            view->fills[fills->first_fill + index];
                        if (oracle_point_in_rect(fill.rect, x, y) != 0) {
                            color = fill.color;
                            covered = 1;
                        }
                    }
                } else if (command->opcode ==
                           DB_RENDER_IR_OP_FILL_LINEAR_GRADIENT) {
                    const db_render_ir_linear_gradient_command_t *const
                        gradient = DB_RENDER_IR_COMMAND_AS(
                            db_render_ir_linear_gradient_command_t, command);
                    if (oracle_point_in_rect(gradient->bounds, x, y) != 0) {
                        color = oracle_gradient_color(gradient, y);
                        covered = 1;
                    }
                } else {
                    return 0;
                }
                if (covered != 0) {
                    pixels[(y * 16U) + x] = oracle_pack_color(color);
                }
            }
        }
    }
    return DB_BOOL(iterator.offset == view->command_size);
}

static void expect_policy_surfaces_equal(db_test_state_t *state,
                                         const db_render_ir_view_t *raw,
                                         const db_render_ir_view_t *optimized) {
    uint32_t raw_pixels[16U * 16U] = {0};
    uint32_t optimized_pixels[16U * 16U] = {0};
    const db_pixel_surface_t optimized_surface = {
        .pixels = optimized_pixels,
        .pixel_width = 16U,
        .pixel_height = 16U,
        .format = DB_PIXEL_FORMAT_RGBA8,
    };
    DB_TEST_EXPECT_TRUE(state, oracle_rasterize(raw, raw_pixels) != 0);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_rasterize_surface(optimized, 16U, 16U, &optimized_surface),
        DB_RENDER_IR_OK);
    DB_TEST_EXPECT_TRUE(
        state, memcmp(raw_pixels, optimized_pixels, sizeof(raw_pixels)) == 0);
    DB_TEST_EXPECT_EQ_U64(
        state,
        db_hash_rgba8_pixels_canonical(raw_pixels, 16U, 16U,
                                       16U * sizeof(uint32_t), 0),
        db_hash_rgba8_pixels_canonical(optimized_pixels, 16U, 16U,
                                       16U * sizeof(uint32_t), 0));
}

static const db_render_ir_command_header_t *
first_policy_draw(const db_render_ir_view_t *view,
                  db_render_ir_command_t *storage) {
    db_render_ir_iterator_t iterator = {0};
    db_render_ir_iterator_begin(&iterator, view);
    const db_render_ir_command_header_t *command = NULL;
    while ((command = db_render_ir_iterator_next(&iterator)) != NULL) {
        if ((command->opcode != DB_RENDER_IR_OP_BEGIN_TARGET) &&
            (command->opcode != DB_RENDER_IR_OP_END_TARGET)) {
            *storage = iterator.current;
            return &storage->header;
        }
    }
    return NULL;
}

static int policy_regions_equal_across_views(const db_render_ir_view_t *lhs,
                                             db_render_ir_region_id_t lhs_id,
                                             const db_render_ir_view_t *rhs,
                                             db_render_ir_region_id_t rhs_id) {
    if ((lhs_id == DB_RENDER_IR_INVALID_ID) ||
        (rhs_id == DB_RENDER_IR_INVALID_ID)) {
        return DB_BOOL(lhs_id == rhs_id);
    }
    if ((lhs_id >= lhs->region_count) || (rhs_id >= rhs->region_count)) {
        return 0;
    }
    const db_render_ir_region_t lhs_region = lhs->regions[lhs_id];
    const db_render_ir_region_t rhs_region = rhs->regions[rhs_id];
    if (lhs_region.band_count != rhs_region.band_count) {
        return 0;
    }
    for (uint32_t band_offset = 0U; band_offset < lhs_region.band_count;
         band_offset++) {
        const db_render_ir_band_t lhs_band =
            lhs->bands[lhs_region.first_band + band_offset];
        const db_render_ir_band_t rhs_band =
            rhs->bands[rhs_region.first_band + band_offset];
        if ((lhs_band.y_start != rhs_band.y_start) ||
            (lhs_band.y_end != rhs_band.y_end) ||
            (lhs_band.span_count != rhs_band.span_count)) {
            return 0;
        }
        for (uint32_t span_offset = 0U; span_offset < lhs_band.span_count;
             span_offset++) {
            const db_render_ir_span_t lhs_span =
                lhs->spans[lhs_band.first_span + span_offset];
            const db_render_ir_span_t rhs_span =
                rhs->spans[rhs_band.first_span + span_offset];
            if ((lhs_span.x_start != rhs_span.x_start) ||
                (lhs_span.x_end != rhs_span.x_end)) {
                return 0;
            }
        }
    }
    return 1;
}

static void expect_policy_regions_equal(
    db_test_state_t *state, const db_render_ir_view_t *raw,
    const db_render_ir_command_header_t *raw_command,
    const db_render_ir_view_t *optimized,
    const db_render_ir_command_header_t *optimized_command) {
    DB_TEST_EXPECT_TRUE(state, policy_regions_equal_across_views(
                                   raw, raw_command->clip_region, optimized,
                                   optimized_command->clip_region) != 0);
    DB_TEST_EXPECT_TRUE(state, policy_regions_equal_across_views(
                                   raw, raw_command->touched_region, optimized,
                                   optimized_command->touched_region) != 0);
    DB_TEST_EXPECT_TRUE(state,
                        policy_regions_equal_across_views(
                            raw, raw_command->full_coverage_region, optimized,
                            optimized_command->full_coverage_region) != 0);
    DB_TEST_EXPECT_TRUE(
        state, policy_regions_equal_across_views(
                   raw, db_render_ir_final_damage_region(raw), optimized,
                   db_render_ir_final_damage_region(optimized)) != 0);
}

static void clipped_clear_preserves_effective_regions(db_test_state_t *state) {
    test_clip_store_t raw = {0};
    test_clip_store_t optimized = {0};
    init_clip_store(&raw);
    init_clip_store(&optimized);
    const db_render_ir_resource_id_t target = add_clip_target(state, &raw);
    db_render_ir_region_id_t clip = DB_RENDER_IR_INVALID_ID;
    (void)db_render_ir_add_rect_region(
        &raw.store,
        (db_render_ir_rect_t){.x = 2, .y = 3, .width = 5, .height = 4}, &clip);
    (void)db_render_ir_begin_target(&raw.store, target);
    (void)db_render_ir_clear(
        &raw.store, target,
        (db_render_ir_color_t){.rgba = {1.0, 0.0, 0.0, 1.0}}, clip);
    (void)db_render_ir_set_last_command_regions(&raw.store, clip, clip);
    (void)db_render_ir_end_target(&raw.store, target);
    (void)db_render_ir_set_last_command_regions(&raw.store, clip, clip);
    const db_render_ir_view_t raw_view = db_render_ir_store_view(&raw.store);
    DB_TEST_EXPECT_EQ_INT(state,
                          optimize_clip_store(&raw_view, &optimized, NULL),
                          DB_RENDER_IR_OK);
    const db_render_ir_view_t optimized_view =
        db_render_ir_store_view(&optimized.store);
    expect_policy_surfaces_equal(state, &raw_view, &optimized_view);
    db_render_ir_command_t optimized_storage = {0};
    const db_render_ir_command_header_t *const command =
        first_policy_draw(&optimized_view, &optimized_storage);
    DB_TEST_EXPECT_TRUE(state, command != NULL);
    if (command == NULL) {
        return;
    }
    db_render_ir_command_t raw_storage = {0};
    const db_render_ir_command_header_t *const raw_command =
        first_policy_draw(&raw_view, &raw_storage);
    DB_TEST_EXPECT_TRUE(state, raw_command != NULL);
    if (raw_command == NULL) {
        return;
    }
    expect_policy_regions_equal(state, &raw_view, raw_command, &optimized_view,
                                command);
    DB_TEST_EXPECT_TRUE(state, command->clip_region != DB_RENDER_IR_INVALID_ID);
    DB_TEST_EXPECT_TRUE(
        state, db_render_ir_regions_equal(&optimized_view, command->clip_region,
                                          command->touched_region) != 0);
    DB_TEST_EXPECT_TRUE(state, db_render_ir_regions_equal(
                                   &optimized_view, command->touched_region,
                                   command->full_coverage_region) != 0);
    DB_TEST_EXPECT_TRUE(
        state, db_render_ir_regions_equal(
                   &optimized_view, command->touched_region,
                   db_render_ir_final_damage_region(&optimized_view)) != 0);
    const db_render_ir_metadata_t metadata =
        db_render_ir_metadata(&optimized_view, DB_RENDER_IR_OK, 16U, 16U);
    DB_TEST_EXPECT_EQ_U64(state, metadata.damage_area, 20U);
    DB_TEST_EXPECT_EQ_INT(state, metadata.full_coverage, 0);
}

static void all_edge_fill_canonicalizes_to_target(db_test_state_t *state) {
    test_clip_store_t raw = {0};
    test_clip_store_t optimized = {0};
    init_clip_store(&raw);
    init_clip_store(&optimized);
    const db_render_ir_resource_id_t target = add_clip_target(state, &raw);
    const db_render_ir_fill_t fill = {
        .rect = {.x = -3, .y = -4, .width = 22, .height = 24},
        .color = {.rgba = {0.0, 1.0, 0.0, 1.0}},
    };
    db_render_ir_region_id_t target_region = DB_RENDER_IR_INVALID_ID;
    (void)db_render_ir_add_rect_region(
        &raw.store, (db_render_ir_rect_t){.width = 16, .height = 16},
        &target_region);
    (void)db_render_ir_begin_target(&raw.store, target);
    (void)db_render_ir_fill_rects(&raw.store, target, &fill, 1U,
                                  DB_RENDER_IR_INVALID_ID);
    (void)db_render_ir_set_last_command_regions(&raw.store, target_region,
                                                target_region);
    (void)db_render_ir_end_target(&raw.store, target);
    (void)db_render_ir_set_last_command_regions(&raw.store, target_region,
                                                target_region);
    const db_render_ir_view_t raw_view = db_render_ir_store_view(&raw.store);
    DB_TEST_EXPECT_EQ_INT(state,
                          optimize_clip_store(&raw_view, &optimized, NULL),
                          DB_RENDER_IR_OK);
    const db_render_ir_view_t optimized_view =
        db_render_ir_store_view(&optimized.store);
    expect_policy_surfaces_equal(state, &raw_view, &optimized_view);
    db_render_ir_command_t optimized_storage = {0};
    const db_render_ir_command_header_t *const command =
        first_policy_draw(&optimized_view, &optimized_storage);
    DB_TEST_EXPECT_TRUE(state, command != NULL);
    if (command == NULL) {
        return;
    }
    db_render_ir_command_t raw_storage = {0};
    const db_render_ir_command_header_t *const raw_command =
        first_policy_draw(&raw_view, &raw_storage);
    DB_TEST_EXPECT_TRUE(state, raw_command != NULL);
    if (raw_command == NULL) {
        return;
    }
    expect_policy_regions_equal(state, &raw_view, raw_command, &optimized_view,
                                command);
    DB_TEST_EXPECT_EQ_U32(state, command->clip_region, DB_RENDER_IR_INVALID_ID);
    DB_TEST_EXPECT_EQ_U64(
        state,
        db_render_ir_region_area(&optimized_view, command->touched_region),
        16U * 16U);
    const db_render_ir_metadata_t metadata =
        db_render_ir_metadata(&optimized_view, DB_RENDER_IR_OK, 16U, 16U);
    DB_TEST_EXPECT_EQ_INT(state, metadata.full_coverage, 1);
}

static void empty_effective_clip_eliminates_command(db_test_state_t *state) {
    test_clip_store_t raw = {0};
    test_clip_store_t optimized = {0};
    init_clip_store(&raw);
    init_clip_store(&optimized);
    const db_render_ir_resource_id_t target = add_clip_target(state, &raw);
    db_render_ir_region_id_t clip = DB_RENDER_IR_INVALID_ID;
    (void)db_render_ir_add_rect_region(
        &raw.store, (db_render_ir_rect_t){.width = 2, .height = 2}, &clip);
    const db_render_ir_fill_t fill = {
        .rect = {.x = 8, .y = 8, .width = 2, .height = 2},
        .color = {.rgba = {0.0, 0.0, 1.0, 1.0}},
    };
    (void)db_render_ir_begin_target(&raw.store, target);
    (void)db_render_ir_fill_rects(&raw.store, target, &fill, 1U, clip);
    (void)db_render_ir_end_target(&raw.store, target);
    const db_render_ir_view_t raw_view = db_render_ir_store_view(&raw.store);
    DB_TEST_EXPECT_EQ_INT(state,
                          optimize_clip_store(&raw_view, &optimized, NULL),
                          DB_RENDER_IR_OK);
    const db_render_ir_view_t optimized_view =
        db_render_ir_store_view(&optimized.store);
    expect_policy_surfaces_equal(state, &raw_view, &optimized_view);
    db_render_ir_command_t optimized_storage = {0};
    DB_TEST_EXPECT_TRUE(
        state, first_policy_draw(&optimized_view, &optimized_storage) == NULL);
    const db_render_ir_metadata_t metadata =
        db_render_ir_metadata(&optimized_view, DB_RENDER_IR_OK, 16U, 16U);
    DB_TEST_EXPECT_EQ_U64(state, metadata.damage_area, 0U);
    DB_TEST_EXPECT_EQ_U32(state, metadata.damage_region,
                          DB_RENDER_IR_INVALID_ID);
    db_render_ir_command_t raw_storage = {0};
    const db_render_ir_command_header_t *const raw_command =
        first_policy_draw(&raw_view, &raw_storage);
    DB_TEST_EXPECT_TRUE(state, raw_command != NULL);
    if (raw_command != NULL) {
        DB_TEST_EXPECT_EQ_U32(state, raw_command->touched_region,
                              DB_RENDER_IR_INVALID_ID);
        DB_TEST_EXPECT_EQ_U32(state, raw_command->full_coverage_region,
                              DB_RENDER_IR_INVALID_ID);
    }
    DB_TEST_EXPECT_EQ_U32(state, db_render_ir_final_damage_region(&raw_view),
                          DB_RENDER_IR_INVALID_ID);
}

static void clipped_overwrite_is_eliminated(db_test_state_t *state) {
    test_clip_store_t raw = {0};
    test_clip_store_t optimized = {0};
    init_clip_store(&raw);
    init_clip_store(&optimized);
    const db_render_ir_resource_id_t target = add_clip_target(state, &raw);
    db_render_ir_region_id_t clip = DB_RENDER_IR_INVALID_ID;
    (void)db_render_ir_add_rect_region(
        &raw.store,
        (db_render_ir_rect_t){.x = 2, .y = 2, .width = 8, .height = 8}, &clip);
    const db_render_ir_fill_t fills[] = {
        {.rect = {.width = 16, .height = 16},
         .color = {.rgba = {1.0, 0.0, 0.0, 1.0}}},
        {.rect = {.width = 16, .height = 16},
         .color = {.rgba = {0.0, 1.0, 0.0, 1.0}}},
    };
    (void)db_render_ir_begin_target(&raw.store, target);
    (void)db_render_ir_fill_rects(&raw.store, target, &fills[0], 1U, clip);
    (void)db_render_ir_set_last_command_regions(&raw.store, clip, clip);
    (void)db_render_ir_fill_rects(&raw.store, target, &fills[1], 1U, clip);
    (void)db_render_ir_set_last_command_regions(&raw.store, clip, clip);
    (void)db_render_ir_end_target(&raw.store, target);
    (void)db_render_ir_set_last_command_regions(&raw.store, clip, clip);
    const db_render_ir_view_t raw_view = db_render_ir_store_view(&raw.store);
    db_render_ir_optimizer_stats_t stats = {0};
    DB_TEST_EXPECT_EQ_INT(state,
                          optimize_clip_store(&raw_view, &optimized, &stats),
                          DB_RENDER_IR_OK);
    const db_render_ir_view_t optimized_view =
        db_render_ir_store_view(&optimized.store);
    expect_policy_surfaces_equal(state, &raw_view, &optimized_view);
    DB_TEST_EXPECT_EQ_SIZE(state, optimized.store.fill_count, 1U);
    DB_TEST_EXPECT_EQ_U64(state, stats.emitted_spans, 1U);
    db_render_ir_command_t optimized_storage = {0};
    const db_render_ir_command_header_t *const optimized_command =
        first_policy_draw(&optimized_view, &optimized_storage);
    DB_TEST_EXPECT_TRUE(state, optimized_command != NULL);
    if (optimized_command != NULL) {
        db_render_ir_iterator_t raw_iterator = {0};
        db_render_ir_iterator_begin(&raw_iterator, &raw_view);
        (void)db_render_ir_iterator_next(&raw_iterator);
        (void)db_render_ir_iterator_next(&raw_iterator);
        const db_render_ir_command_header_t *const raw_command =
            db_render_ir_iterator_next(&raw_iterator);
        DB_TEST_EXPECT_TRUE(state, raw_command != NULL);
        if (raw_command != NULL) {
            expect_policy_regions_equal(state, &raw_view, raw_command,
                                        &optimized_view, optimized_command);
        }
        DB_TEST_EXPECT_EQ_U64(
            state,
            db_render_ir_region_area(&optimized_view,
                                     optimized_command->touched_region),
            64U);
    }
}

static void
clipped_gradient_remains_one_semantic_command(db_test_state_t *state) {
    test_clip_store_t raw = {0};
    test_clip_store_t optimized = {0};
    uint32_t raw_pixels[16U * 16U] = {0};
    uint32_t optimized_pixels[16U * 16U] = {0};
    init_clip_store(&raw);
    init_clip_store(&optimized);
    const db_render_ir_resource_id_t target = add_clip_target(state, &raw);
    const db_render_ir_fill_t fragments[] = {
        {.rect = {.x = 1, .y = 2, .width = 3, .height = 2}},
        {.rect = {.x = 8, .y = 6, .width = 2, .height = 3}},
    };
    db_render_ir_region_id_t clip = DB_RENDER_IR_INVALID_ID;
    (void)db_render_ir_add_fill_region(&raw.store, fragments, 2U, &clip);
    (void)db_render_ir_begin_target(&raw.store, target);
    (void)db_render_ir_fill_linear_gradient(
        &raw.store, target, (db_render_ir_rect_t){.width = 16, .height = 16}, 0,
        TEST_CLIP_GRADIENT_END, 0,
        (db_render_ir_color_t){.rgba = {1.0, 0.0, 0.0, 1.0}},
        (db_render_ir_color_t){.rgba = {0.0, 0.0, 1.0, 1.0}}, clip);
    (void)db_render_ir_set_last_command_regions(&raw.store, clip, clip);
    (void)db_render_ir_end_target(&raw.store, target);
    (void)db_render_ir_set_last_command_regions(&raw.store, clip, clip);
    const db_render_ir_view_t raw_view = db_render_ir_store_view(&raw.store);
    DB_TEST_EXPECT_EQ_INT(state,
                          optimize_clip_store(&raw_view, &optimized, NULL),
                          DB_RENDER_IR_OK);
    const db_render_ir_view_t optimized_view =
        db_render_ir_store_view(&optimized.store);
    DB_TEST_EXPECT_EQ_U32(state, optimized_view.command_count, 3U);
    db_render_ir_iterator_t iterator = {0};
    db_render_ir_iterator_begin(&iterator, &optimized_view);
    (void)db_render_ir_iterator_next(&iterator);
    const db_render_ir_command_header_t *const command =
        db_render_ir_iterator_next(&iterator);
    DB_TEST_EXPECT_TRUE(state, command != NULL);
    if (command == NULL) {
        return;
    }
    DB_TEST_EXPECT_EQ_INT(state, command->opcode,
                          DB_RENDER_IR_OP_FILL_LINEAR_GRADIENT);
    const db_render_ir_linear_gradient_command_t *const gradient =
        DB_RENDER_IR_COMMAND_AS(db_render_ir_linear_gradient_command_t,
                                command);
    DB_TEST_EXPECT_EQ_INT(state, gradient->bounds.x, 0);
    DB_TEST_EXPECT_EQ_INT(state, gradient->bounds.y, 0);
    DB_TEST_EXPECT_EQ_INT(state, gradient->bounds.width, 16);
    DB_TEST_EXPECT_EQ_INT(state, gradient->bounds.height, 16);
    DB_TEST_EXPECT_EQ_INT(state, gradient->axis_start, 0);
    DB_TEST_EXPECT_EQ_INT(state, gradient->axis_end, TEST_CLIP_GRADIENT_END);
    db_render_ir_command_t raw_storage = {0};
    const db_render_ir_command_header_t *const raw_command =
        first_policy_draw(&raw_view, &raw_storage);
    DB_TEST_EXPECT_TRUE(state, raw_command != NULL);
    if (raw_command != NULL) {
        expect_policy_regions_equal(state, &raw_view, raw_command,
                                    &optimized_view, command);
    }
    DB_TEST_EXPECT_EQ_U64(
        state,
        db_render_ir_region_area(&optimized_view, command->touched_region),
        12U);
    DB_TEST_EXPECT_TRUE(
        state, db_render_ir_regions_equal(&optimized_view, command->clip_region,
                                          command->touched_region) != 0);
    DB_TEST_EXPECT_TRUE(state, db_render_ir_regions_equal(
                                   &optimized_view, command->touched_region,
                                   command->full_coverage_region) != 0);
    DB_TEST_EXPECT_TRUE(
        state, db_render_ir_regions_equal(
                   &optimized_view, command->touched_region,
                   db_render_ir_final_damage_region(&optimized_view)) != 0);
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
generated_sequences_match_independent_oracle(db_test_state_t *state) {
    static const int32_t origins[] = {-3, 0, 7, 15, 18};
    static const int32_t extents[] = {1, 4, 20};
    for (size_t origin_index = 0U;
         origin_index < (sizeof(origins) / sizeof(origins[0]));
         origin_index++) {
        for (size_t extent_index = 0U;
             extent_index < (sizeof(extents) / sizeof(extents[0]));
             extent_index++) {
            test_clip_store_t raw = {0};
            test_clip_store_t optimized = {0};
            init_clip_store(&raw);
            init_clip_store(&optimized);
            const db_render_ir_resource_id_t target =
                add_clip_target(state, &raw);
            const db_render_ir_fill_t fragments[] = {
                {.rect = {.x = 1, .y = 1, .width = 6, .height = 4}},
                {.rect = {.x = 9, .y = 7, .width = 5, .height = 6}},
            };
            db_render_ir_region_id_t clip = DB_RENDER_IR_INVALID_ID;
            DB_TEST_EXPECT_EQ_INT(
                state,
                db_render_ir_add_fill_region(&raw.store, fragments, 2U, &clip),
                DB_RENDER_IR_OK);
            DB_TEST_EXPECT_EQ_INT(state,
                                  db_render_ir_begin_target(&raw.store, target),
                                  DB_RENDER_IR_OK);
            DB_TEST_EXPECT_EQ_INT(
                state,
                db_render_ir_clear(
                    &raw.store, target,
                    (db_render_ir_color_t){.rgba = {0.1, 0.2, 0.3, 1.0}},
                    DB_RENDER_IR_INVALID_ID),
                DB_RENDER_IR_OK);
            const db_render_ir_fill_t fill = {
                .rect = {.x = origins[origin_index],
                         .y = origins[(origin_index + 2U) %
                                      (sizeof(origins) / sizeof(origins[0]))],
                         .width = extents[extent_index],
                         .height =
                             extents[(extent_index + 1U) %
                                     (sizeof(extents) / sizeof(extents[0]))]},
                .color = {.rgba = {0.8, 0.1, 0.4, 1.0}},
            };
            DB_TEST_EXPECT_EQ_INT(
                state,
                db_render_ir_fill_rects(&raw.store, target, &fill, 1U, clip),
                DB_RENDER_IR_OK);
            DB_TEST_EXPECT_EQ_INT(
                state,
                db_render_ir_fill_linear_gradient(
                    &raw.store, target,
                    (db_render_ir_rect_t){
                        .x = 2, .y = 2, .width = 12, .height = 12},
                    -2, 17, (uint8_t)(origin_index & 1U),
                    (db_render_ir_color_t){.rgba = {1.0, 0.0, 0.0, 1.0}},
                    (db_render_ir_color_t){.rgba = {0.0, 1.0, 1.0, 1.0}}, clip),
                DB_RENDER_IR_OK);
            DB_TEST_EXPECT_EQ_INT(state,
                                  db_render_ir_end_target(&raw.store, target),
                                  DB_RENDER_IR_OK);
            const db_render_ir_view_t raw_view =
                db_render_ir_store_view(&raw.store);
            DB_TEST_EXPECT_EQ_INT(
                state, optimize_clip_store(&raw_view, &optimized, NULL),
                DB_RENDER_IR_OK);
            const db_render_ir_view_t optimized_view =
                db_render_ir_store_view(&optimized.store);
            expect_policy_surfaces_equal(state, &raw_view, &optimized_view);
        }
    }
}

unsigned db_render_ir_clip_test_run_all(void) {
    static const db_test_case_t cases[] = {
        {"clipped_clear_preserves_effective_regions",
         clipped_clear_preserves_effective_regions},
        {"all_edge_fill_canonicalizes_to_target",
         all_edge_fill_canonicalizes_to_target},
        {"empty_effective_clip_eliminates_command",
         empty_effective_clip_eliminates_command},
        {"clipped_overwrite_is_eliminated", clipped_overwrite_is_eliminated},
        {"clipped_gradient_remains_one_semantic_command",
         clipped_gradient_remains_one_semantic_command},
        {"generated_sequences_match_independent_oracle",
         generated_sequences_match_independent_oracle},
    };
    return db_test_run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}
