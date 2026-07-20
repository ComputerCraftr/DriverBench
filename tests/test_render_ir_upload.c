#include "core/db_render_ir.h"
#include "core/db_render_ir_surface.h"
#include "core/db_render_types.h"
#include "support/test_harness.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    TEST_UPLOAD_COMMAND_BYTES = 1024U,
    TEST_UPLOAD_RESOURCE_CAPACITY = 4U,
};

typedef struct {
    max_align_t commands[TEST_UPLOAD_COMMAND_BYTES / sizeof(max_align_t)];
    db_render_ir_resource_t resources[TEST_UPLOAD_RESOURCE_CAPACITY];
    db_render_ir_store_t store;
} upload_store_t;

enum { TEST_UNCHANGED_BYTE = 0xa5U };

static void upload_store_init(upload_store_t *fixture) {
    *fixture = (upload_store_t){0};
    fixture->store = (db_render_ir_store_t){
        .commands = fixture->commands,
        .command_capacity = sizeof(fixture->commands),
        .resources = fixture->resources,
        .resource_capacity = TEST_UPLOAD_RESOURCE_CAPACITY,
    };
}

static void add_upload_command(db_test_state_t *state, upload_store_t *fixture,
                               uint32_t width, uint32_t height,
                               db_pixel_format_t target_format,
                               db_pixel_format_t source_format,
                               db_render_ir_rect_t source_rect,
                               int32_t destination_x, int32_t destination_y,
                               db_render_ir_resource_id_t *source) {
    db_render_ir_resource_id_t target = DB_RENDER_IR_INVALID_ID;
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_add_resource(
            &fixture->store,
            &(const db_render_ir_resource_t){
                .kind = DB_RENDER_IR_RESOURCE_CANONICAL_TARGET,
                .width = width,
                .height = height,
                .format = target_format,
            },
            &target),
        DB_RENDER_IR_OK);
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_add_resource(
                              &fixture->store,
                              &(const db_render_ir_resource_t){
                                  .kind = DB_RENDER_IR_RESOURCE_RASTER_SOURCE,
                                  .width = width,
                                  .height = height,
                                  .format = source_format,
                              },
                              source),
                          DB_RENDER_IR_OK);
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_begin_target(&fixture->store, target),
                          DB_RENDER_IR_OK);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_upload_image(
            &fixture->store, target, *source, source_rect, destination_x,
            destination_y,
            (db_render_ir_upload_semantics_t){
                .replacement = DB_RENDER_IR_UPLOAD_REPLACE_EXACT,
                .filter = DB_RENDER_IR_FILTER_NEAREST,
                .conversion = DB_RENDER_IR_CONVERSION_EXACT,
                .prior_content = DB_RENDER_IR_PRIOR_CONTENT_INDEPENDENT,
                .opacity = 1.0}),
        DB_RENDER_IR_OK);
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_end_target(&fixture->store, target),
                          DB_RENDER_IR_OK);
}

static void
overlapping_upload_preserves_future_source_rows(db_test_state_t *state) {
    enum {
        WIDTH = 4U,
        HEIGHT = 4U,
        ROW_BYTES = WIDTH * 4U,
        PIXEL_BYTES = ROW_BYTES * HEIGHT,
        DESTINATION_OFFSET = 4U,
    };
    upload_store_t fixture = {0};
    upload_store_init(&fixture);
    db_render_ir_resource_id_t target = DB_RENDER_IR_INVALID_ID;
    db_render_ir_resource_id_t source = DB_RENDER_IR_INVALID_ID;
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_add_resource(
            &fixture.store,
            &(const db_render_ir_resource_t){
                .kind = DB_RENDER_IR_RESOURCE_CANONICAL_TARGET,
                .width = WIDTH,
                .height = HEIGHT,
                .format = DB_PIXEL_FORMAT_RGBA8,
            },
            &target),
        DB_RENDER_IR_OK);
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_add_resource(
                              &fixture.store,
                              &(const db_render_ir_resource_t){
                                  .kind = DB_RENDER_IR_RESOURCE_RASTER_SOURCE,
                                  .width = WIDTH,
                                  .height = HEIGHT,
                                  .format = DB_PIXEL_FORMAT_RGBA8,
                              },
                              &source),
                          DB_RENDER_IR_OK);
    (void)db_render_ir_upload_image(
        &fixture.store, target, source,
        (db_render_ir_rect_t){.width = WIDTH, .height = HEIGHT}, 0, 0,
        (db_render_ir_upload_semantics_t){
            .replacement = DB_RENDER_IR_UPLOAD_REPLACE_EXACT,
            .filter = DB_RENDER_IR_FILTER_NEAREST,
            .conversion = DB_RENDER_IR_CONVERSION_EXACT,
            .prior_content = DB_RENDER_IR_PRIOR_CONTENT_INDEPENDENT,
            .opacity = 1.0});

    uint8_t storage[PIXEL_BYTES + DESTINATION_OFFSET] = {0};
    uint8_t expected[PIXEL_BYTES] = {0};
    for (size_t index = 0U; index < PIXEL_BYTES; index++) {
        storage[index] = (uint8_t)(index + 1U);
        expected[index] = storage[index];
    }
    const db_render_ir_external_binding_t binding = {
        .resource = source,
        .width = WIDTH,
        .height = HEIGHT,
        .format = DB_PIXEL_FORMAT_RGBA8,
        .row_stride_bytes = ROW_BYTES,
        .size_bytes = PIXEL_BYTES,
        .pixels = storage,
    };
    const db_pixel_surface_t surface = {
        .pixel_width = WIDTH,
        .pixel_height = HEIGHT,
        .pixels = storage + DESTINATION_OFFSET,
        .format = DB_PIXEL_FORMAT_RGBA8,
    };
    const db_render_ir_view_t view = db_render_ir_store_view(&fixture.store);
    db_render_ir_upload_command_t resolved_upload = {0};
    db_render_ir_external_binding_t resolved_binding = {0};
    DB_TEST_EXPECT_TRUE(state, db_render_ir_resolve_full_upload(
                                   &view,
                                   (db_render_ir_external_binding_view_t){
                                       .bindings = &binding, .count = 1U},
                                   &resolved_upload, &resolved_binding) != 0);
    DB_TEST_EXPECT_EQ_U32(state, resolved_upload.source, source);
    DB_TEST_EXPECT_EQ_U32(state, resolved_binding.resource, source);
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_rasterize_surface_with_bindings(
                              &view,
                              (db_render_ir_external_binding_view_t){
                                  .bindings = &binding, .count = 1U},
                              WIDTH, HEIGHT, &surface),
                          DB_RENDER_IR_OK);
    DB_TEST_EXPECT_TRUE(
        state, memcmp(surface.pixels, expected, sizeof(expected)) == 0);
}

static void
external_bindings_require_sorted_unique_resources(db_test_state_t *state) {
    upload_store_t fixture = {0};
    upload_store_init(&fixture);
    for (size_t index = 0U; index < 3U; index++) {
        db_render_ir_resource_id_t resource = DB_RENDER_IR_INVALID_ID;
        DB_TEST_EXPECT_EQ_INT(
            state,
            db_render_ir_add_resource(
                &fixture.store,
                &(const db_render_ir_resource_t){
                    .kind = DB_RENDER_IR_RESOURCE_RASTER_SOURCE,
                    .width = 1U,
                    .height = 1U,
                    .format = DB_PIXEL_FORMAT_RGBA8,
                },
                &resource),
            DB_RENDER_IR_OK);
        DB_TEST_EXPECT_EQ_U32(state, resource, (uint32_t)index);
    }
    uint32_t pixels[3] = {0};
    const db_render_ir_external_binding_t sorted[] = {
        {.resource = 0U,
         .width = 1U,
         .height = 1U,
         .format = DB_PIXEL_FORMAT_RGBA8,
         .row_stride_bytes = sizeof(pixels[0]),
         .size_bytes = sizeof(pixels[0]),
         .pixels = &pixels[0]},
        {.resource = 2U,
         .width = 1U,
         .height = 1U,
         .format = DB_PIXEL_FORMAT_RGBA8,
         .row_stride_bytes = sizeof(pixels[2]),
         .size_bytes = sizeof(pixels[2]),
         .pixels = &pixels[2]},
    };
    const db_render_ir_view_t view = db_render_ir_store_view(&fixture.store);
    const db_render_ir_external_binding_view_t sorted_view = {
        .bindings = sorted, .count = sizeof(sorted) / sizeof(sorted[0])};
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_validate_bindings(&view, sorted_view),
                          DB_RENDER_IR_OK);
    db_render_ir_external_binding_t found = {0};
    DB_TEST_EXPECT_TRUE(
        state, db_render_ir_find_binding(sorted_view, 2U, &found) != 0);
    DB_TEST_EXPECT_EQ_U32(state, found.resource, 2U);
    DB_TEST_EXPECT_TRUE(
        state, db_render_ir_find_binding(sorted_view, 1U, &found) == 0);
    found.resource = UINT32_C(0xa5a5a5a5);
    const db_render_ir_external_binding_view_t oversized = {
        .bindings = sorted,
        .count = DB_RENDER_IR_EXTERNAL_BINDING_CAPACITY + 1U,
    };
    DB_TEST_EXPECT_TRUE(state,
                        db_render_ir_find_binding(oversized, 0U, &found) == 0);
    DB_TEST_EXPECT_EQ_U32(state, found.resource, UINT32_C(0xa5a5a5a5));
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_validate_bindings(&view, oversized),
                          DB_RENDER_IR_INVALID);

    const db_render_ir_external_binding_t duplicate[] = {sorted[0], sorted[0]};
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_validate_bindings(
            &view, (db_render_ir_external_binding_view_t){.bindings = duplicate,
                                                          .count = 2U}),
        DB_RENDER_IR_INVALID);
    const db_render_ir_external_binding_t unsorted[] = {sorted[1], sorted[0]};
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_validate_bindings(
            &view, (db_render_ir_external_binding_view_t){.bindings = unsorted,
                                                          .count = 2U}),
        DB_RENDER_IR_INVALID);

    db_render_ir_external_binding_t wrapping = sorted[0];
    wrapping.pixels = db_test_pointer_from_uintptr(UINTPTR_MAX - 1U);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_validate_bindings(
            &view, (db_render_ir_external_binding_view_t){.bindings = &wrapping,
                                                          .count = 1U}),
        DB_RENDER_IR_INVALID);
}

static void partial_rgba8_upload_handles_padding_and_unaligned_input(
    db_test_state_t *state) {
    enum {
        WIDTH = 4U,
        HEIGHT = 3U,
        PIXEL_BYTES = 4U,
        SOURCE_STRIDE = 20U,
        SOURCE_BYTES = ((HEIGHT - 1U) * SOURCE_STRIDE) + (WIDTH * PIXEL_BYTES),
        DESTINATION_BYTES = WIDTH * HEIGHT * PIXEL_BYTES,
    };
    upload_store_t fixture = {0};
    upload_store_init(&fixture);
    db_render_ir_resource_id_t source = DB_RENDER_IR_INVALID_ID;
    add_upload_command(
        state, &fixture, WIDTH, HEIGHT, DB_PIXEL_FORMAT_RGBA8,
        DB_PIXEL_FORMAT_RGBA8,
        (db_render_ir_rect_t){.x = 1, .y = 1, .width = 2, .height = 2}, 0, 0,
        &source);
    uint8_t source_storage[SOURCE_BYTES + 1U] = {0};
    uint8_t *const source_pixels = &source_storage[1];
    for (uint32_t row = 0U; row < HEIGHT; row++) {
        for (uint32_t col = 0U; col < WIDTH; col++) {
            const size_t offset =
                ((size_t)row * SOURCE_STRIDE) + ((size_t)col * PIXEL_BYTES);
            source_pixels[offset] = (uint8_t)(1U + (row * WIDTH) + col);
            source_pixels[offset + 3U] = UINT8_MAX;
        }
    }
    uint8_t destination[DESTINATION_BYTES] = {0};
    const db_render_ir_external_binding_t binding = {
        .resource = source,
        .width = WIDTH,
        .height = HEIGHT,
        .format = DB_PIXEL_FORMAT_RGBA8,
        .row_stride_bytes = SOURCE_STRIDE,
        .size_bytes = SOURCE_BYTES,
        .pixels = source_pixels,
    };
    const db_pixel_surface_t surface = {
        .pixel_width = WIDTH,
        .pixel_height = HEIGHT,
        .pixels = destination,
        .format = DB_PIXEL_FORMAT_RGBA8,
    };
    const db_render_ir_view_t view = db_render_ir_store_view(&fixture.store);
    db_render_ir_upload_command_t rejected_partial = {.source =
                                                          UINT32_C(0xa5a5a5a5)};
    db_render_ir_external_binding_t rejected_binding = {
        .resource = UINT32_C(0xa5a5a5a5)};
    DB_TEST_EXPECT_TRUE(state, db_render_ir_resolve_full_upload(
                                   &view,
                                   (db_render_ir_external_binding_view_t){
                                       .bindings = &binding, .count = 1U},
                                   &rejected_partial, &rejected_binding) == 0);
    DB_TEST_EXPECT_EQ_U32(state, rejected_partial.source, UINT32_C(0xa5a5a5a5));
    DB_TEST_EXPECT_EQ_U32(state, rejected_binding.resource,
                          UINT32_C(0xa5a5a5a5));
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_rasterize_surface_with_bindings(
                              &view,
                              (db_render_ir_external_binding_view_t){
                                  .bindings = &binding, .count = 1U},
                              WIDTH, HEIGHT, &surface),
                          DB_RENDER_IR_OK);
    DB_TEST_EXPECT_EQ_INT(state, destination[0], 6);
    DB_TEST_EXPECT_EQ_INT(state, destination[4], 7);
    DB_TEST_EXPECT_EQ_INT(state,
                          destination[(size_t)WIDTH * (size_t)PIXEL_BYTES], 10);
    DB_TEST_EXPECT_EQ_INT(
        state, destination[((size_t)WIDTH * (size_t)PIXEL_BYTES) + 4U], 11);
    DB_TEST_EXPECT_EQ_INT(state, destination[8], 0);
}

static void
partial_rgba16f_upload_preserves_native_half_bits(db_test_state_t *state) {
    enum {
        WIDTH = 3U,
        HEIGHT = 2U,
        PIXEL_BYTES = 8U,
        SOURCE_STRIDE = 28U,
        SOURCE_BYTES = SOURCE_STRIDE + (WIDTH * PIXEL_BYTES),
        DESTINATION_BYTES = WIDTH * HEIGHT * PIXEL_BYTES,
        COPY_BYTES = 2U * PIXEL_BYTES,
    };
    upload_store_t fixture = {0};
    upload_store_init(&fixture);
    db_render_ir_resource_id_t source = DB_RENDER_IR_INVALID_ID;
    add_upload_command(
        state, &fixture, WIDTH, HEIGHT, DB_PIXEL_FORMAT_RGBA16F,
        DB_PIXEL_FORMAT_RGBA16F,
        (db_render_ir_rect_t){.x = 1, .y = 0, .width = 2, .height = 2}, 0, 0,
        &source);
    uint8_t source_storage[SOURCE_BYTES + 1U] = {0};
    uint8_t *const source_pixels = &source_storage[1];
    for (size_t index = 0U; index < SOURCE_BYTES; index++) {
        source_pixels[index] = (uint8_t)(index + 1U);
    }
    uint8_t destination[DESTINATION_BYTES];
    memset(destination, TEST_UNCHANGED_BYTE, sizeof(destination));
    const db_render_ir_external_binding_t binding = {
        .resource = source,
        .width = WIDTH,
        .height = HEIGHT,
        .format = DB_PIXEL_FORMAT_RGBA16F,
        .row_stride_bytes = SOURCE_STRIDE,
        .size_bytes = SOURCE_BYTES,
        .pixels = source_pixels,
    };
    const db_pixel_surface_t surface = {
        .pixel_width = WIDTH,
        .pixel_height = HEIGHT,
        .pixels = destination,
        .format = DB_PIXEL_FORMAT_RGBA16F,
    };
    const db_render_ir_view_t view = db_render_ir_store_view(&fixture.store);
    DB_TEST_EXPECT_EQ_INT(state,
                          db_render_ir_rasterize_surface_with_bindings(
                              &view,
                              (db_render_ir_external_binding_view_t){
                                  .bindings = &binding, .count = 1U},
                              WIDTH, HEIGHT, &surface),
                          DB_RENDER_IR_OK);
    DB_TEST_EXPECT_TRUE(state, memcmp(destination, source_pixels + PIXEL_BYTES,
                                      COPY_BYTES) == 0);
    DB_TEST_EXPECT_TRUE(
        state,
        memcmp(destination + ((size_t)WIDTH * (size_t)PIXEL_BYTES),
               source_pixels + SOURCE_STRIDE + PIXEL_BYTES, COPY_BYTES) == 0);
    DB_TEST_EXPECT_EQ_INT(state, destination[(size_t)2U * (size_t)PIXEL_BYTES],
                          TEST_UNCHANGED_BYTE);
}

static void
partial_upload_handles_both_overlap_directions(db_test_state_t *state) {
    enum {
        WIDTH = 4U,
        HEIGHT = 4U,
        ROW_BYTES = WIDTH * 4U,
        COPY_BYTES = 3U * ROW_BYTES,
    };
    const db_render_ir_rect_t source_rects[] = {
        {.x = 0, .y = 0, .width = WIDTH, .height = 3},
        {.x = 0, .y = 1, .width = WIDTH, .height = 3},
    };
    const int32_t destination_rows[] = {1, 0};
    for (size_t direction = 0U; direction < 2U; direction++) {
        upload_store_t fixture = {0};
        upload_store_init(&fixture);
        db_render_ir_resource_id_t source = DB_RENDER_IR_INVALID_ID;
        add_upload_command(state, &fixture, WIDTH, HEIGHT,
                           DB_PIXEL_FORMAT_RGBA8, DB_PIXEL_FORMAT_RGBA8,
                           source_rects[direction], 0,
                           destination_rows[direction], &source);
        uint8_t pixels[HEIGHT * ROW_BYTES] = {0};
        uint8_t original[sizeof(pixels)] = {0};
        for (size_t index = 0U; index < sizeof(pixels); index++) {
            pixels[index] = (uint8_t)(index + 1U);
        }
        memcpy(original, pixels, sizeof(original));
        const db_render_ir_external_binding_t binding = {
            .resource = source,
            .width = WIDTH,
            .height = HEIGHT,
            .format = DB_PIXEL_FORMAT_RGBA8,
            .row_stride_bytes = ROW_BYTES,
            .size_bytes = sizeof(pixels),
            .pixels = pixels,
        };
        const db_pixel_surface_t surface = {
            .pixel_width = WIDTH,
            .pixel_height = HEIGHT,
            .pixels = pixels,
            .format = DB_PIXEL_FORMAT_RGBA8,
        };
        const db_render_ir_view_t view =
            db_render_ir_store_view(&fixture.store);
        DB_TEST_EXPECT_EQ_INT(state,
                              db_render_ir_rasterize_surface_with_bindings(
                                  &view,
                                  (db_render_ir_external_binding_view_t){
                                      .bindings = &binding, .count = 1U},
                                  WIDTH, HEIGHT, &surface),
                              DB_RENDER_IR_OK);
        const size_t source_offset =
            (size_t)(uint32_t)source_rects[direction].y * ROW_BYTES;
        const size_t destination_offset =
            (size_t)(uint32_t)destination_rows[direction] * ROW_BYTES;
        DB_TEST_EXPECT_TRUE(state,
                            memcmp(pixels + destination_offset,
                                   original + source_offset, COPY_BYTES) == 0);
    }
}

static void
upload_rejects_undersized_binding_and_format_mismatch(db_test_state_t *state) {
    upload_store_t fixture = {0};
    upload_store_init(&fixture);
    db_render_ir_resource_id_t source = DB_RENDER_IR_INVALID_ID;
    add_upload_command(
        state, &fixture, 2U, 2U, DB_PIXEL_FORMAT_RGBA8, DB_PIXEL_FORMAT_RGBA8,
        (db_render_ir_rect_t){.width = 1, .height = 1}, 0, 0, &source);
    uint8_t pixels[16] = {0};
    db_render_ir_external_binding_t binding = {
        .resource = source,
        .width = 2U,
        .height = 2U,
        .format = DB_PIXEL_FORMAT_RGBA8,
        .row_stride_bytes = 8U,
        .size_bytes = sizeof(pixels) - 1U,
        .pixels = pixels,
    };
    const db_render_ir_view_t view = db_render_ir_store_view(&fixture.store);
    db_render_ir_upload_command_t rejected_upload = {.source =
                                                         UINT32_C(0xa5a5a5a5)};
    db_render_ir_external_binding_t rejected_binding = {
        .resource = UINT32_C(0xa5a5a5a5)};
    DB_TEST_EXPECT_TRUE(state, db_render_ir_resolve_full_upload(
                                   &view,
                                   (db_render_ir_external_binding_view_t){
                                       .bindings = &binding, .count = 1U},
                                   &rejected_upload, &rejected_binding) == 0);
    DB_TEST_EXPECT_EQ_U32(state, rejected_upload.source, UINT32_C(0xa5a5a5a5));
    DB_TEST_EXPECT_EQ_U32(state, rejected_binding.resource,
                          UINT32_C(0xa5a5a5a5));
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_render_ir_validate_bindings(
            &view, (db_render_ir_external_binding_view_t){.bindings = &binding,
                                                          .count = 1U}),
        DB_RENDER_IR_INVALID);

    upload_store_init(&fixture);
    add_upload_command(
        state, &fixture, 2U, 2U, DB_PIXEL_FORMAT_RGBA16F, DB_PIXEL_FORMAT_RGBA8,
        (db_render_ir_rect_t){.width = 1, .height = 1}, 0, 0, &source);
    const db_render_ir_view_t mismatched =
        db_render_ir_store_view(&fixture.store);
    DB_TEST_EXPECT_EQ_INT(state, db_render_ir_validate(&mismatched),
                          DB_RENDER_IR_INVALID);
}

unsigned db_render_ir_upload_test_run_all(void) {
    static const db_test_case_t cases[] = {
        {"overlapping_upload_preserves_future_source_rows",
         overlapping_upload_preserves_future_source_rows},
        {"external_bindings_require_sorted_unique_resources",
         external_bindings_require_sorted_unique_resources},
        {"partial_rgba8_upload_handles_padding_and_unaligned_input",
         partial_rgba8_upload_handles_padding_and_unaligned_input},
        {"partial_rgba16f_upload_preserves_native_half_bits",
         partial_rgba16f_upload_preserves_native_half_bits},
        {"partial_upload_handles_both_overlap_directions",
         partial_upload_handles_both_overlap_directions},
        {"upload_rejects_undersized_binding_and_format_mismatch",
         upload_rejects_undersized_binding_and_format_mismatch},
    };
    return db_test_run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}
