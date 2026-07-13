#include "core/db_format_contract.h"
#include "support/test_harness.h"

#include "core/db_geometry.h"
#include "core/db_poll_policy.h"
#include "core/db_render_types.h"

#include <stdint.h>
#include <string.h>

#include "core/db_backing_recovery.h"
#include "renderers/gl_api.h"
#include "renderers/gl_common.h"
#include "renderers/gl_proc_runtime.h"
#include "renderers/gl_shadow_present_internal.h"

static uint32_t db_test_gl_error_sequence[DB_GL_ERROR_TRACE_CAPACITY] = {0};
static size_t db_test_gl_error_sequence_index = 0U;

enum {
    DB_TEST_GL_INVALID_ENUM = 0x0500,
    DB_TEST_GL_INVALID_VALUE = 0x0501,
};

static uint32_t db_test_get_error_stub(void) {
    const uint32_t error_code =
        db_test_gl_error_sequence[db_test_gl_error_sequence_index];
    if (db_test_gl_error_sequence_index < (DB_GL_ERROR_TRACE_CAPACITY - 1U)) {
        db_test_gl_error_sequence_index++;
    }
    return error_code;
}

static void
db_test_unpack_upload_storage_reuses_client_buffer(db_test_state_t *state) {
    db_gl_shadow_present_state_t present = {
        .upload_profile =
            {
                .requested_partial =
                    {
                        .target = DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER,
                        .requested_storage = DB_GL_STREAM_UPLOAD_STORAGE_CLIENT,
                        .supported_storage = DB_GL_STREAM_UPLOAD_STORAGE_CLIENT,
                        .effective_storage = DB_GL_STREAM_UPLOAD_STORAGE_CLIENT,
                        .requested_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA,
                        .supported_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA,
                        .effective_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA,
                    },
                .effective_partial =
                    {
                        .target = DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER,
                        .requested_storage = DB_GL_STREAM_UPLOAD_STORAGE_CLIENT,
                        .supported_storage = DB_GL_STREAM_UPLOAD_STORAGE_CLIENT,
                        .effective_storage = DB_GL_STREAM_UPLOAD_STORAGE_CLIENT,
                        .requested_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA,
                        .supported_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA,
                        .effective_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA,
                    },
            },
    };
    db_gl_upload_stream_init(&present.unpack_streams[0],
                             DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER,
                             present.upload_profile.effective_partial, 0U, 1);
    DB_TEST_EXPECT_TRUE(state,
                        db_gl_shadow_present_prepare_unpack_upload_storage(
                            &present, "test_gl_shadow_present", 64U) != 0);
    DB_TEST_EXPECT_TRUE(state,
                        present.unpack_streams[0].client_storage != NULL);
    DB_TEST_EXPECT_EQ_SIZE(
        state, present.unpack_streams[0].client_reserved_bytes, 64U);

    void *first_ptr = present.unpack_streams[0].client_storage;
    DB_TEST_EXPECT_TRUE(state,
                        db_gl_shadow_present_prepare_unpack_upload_storage(
                            &present, "test_gl_shadow_present", 32U) != 0);
    DB_TEST_EXPECT_TRUE(state,
                        present.unpack_streams[0].client_storage == first_ptr);
    DB_TEST_EXPECT_EQ_SIZE(
        state, present.unpack_streams[0].client_reserved_bytes, 64U);

    db_gl_shadow_present_shutdown(&present);
}

static void
db_test_ring_scheduler_selects_ready_alternate_slot(db_test_state_t *state) {
    db_gl_shadow_present_state_t present = {
        .slot_count = 2U,
        .write_slot_index = 0U,
        .present_slot_index = 0U,
        .preserve_mode = DB_GL_SHADOW_PRESENT_PRESERVE_RING_COHERENT,
        .upload_slots =
            {
                {.slot_valid = 1, .slot_matches_shadow = 1},
                {.slot_valid = 1, .slot_matches_shadow = 1},
            },
    };
    db_gl_shadow_present_slot_acquire_t acquire = {0};

    DB_TEST_EXPECT_TRUE(state, db_gl_shadow_present_choose_ring_write_slot(
                                   &present, 1U << 0U, &acquire) != 0);
    DB_TEST_EXPECT_EQ_U32(state, acquire.slot_index, 1U);
    DB_TEST_EXPECT_EQ_INT(state, acquire.fallback_to_single_source, 0);
    DB_TEST_EXPECT_EQ_INT(state, acquire.requires_blocking_reclaim, 0);
}

static void
db_test_ring_scheduler_falls_back_when_all_slots_busy(db_test_state_t *state) {
    db_gl_shadow_present_state_t present = {
        .slot_count = 2U,
        .write_slot_index = 1U,
        .present_slot_index = 0U,
        .preserve_mode = DB_GL_SHADOW_PRESENT_PRESERVE_RING_COHERENT,
        .upload_slots =
            {
                {.slot_valid = 1, .slot_matches_shadow = 1},
                {.slot_valid = 1, .slot_matches_shadow = 1},
            },
    };
    db_gl_shadow_present_slot_acquire_t acquire = {0};

    DB_TEST_EXPECT_TRUE(state, db_gl_shadow_present_choose_ring_write_slot(
                                   &present, 0x3U, &acquire) != 0);
    DB_TEST_EXPECT_EQ_U32(state, acquire.slot_index, 0U);
    DB_TEST_EXPECT_EQ_INT(state, acquire.fallback_to_single_source, 1);
    DB_TEST_EXPECT_EQ_INT(state, acquire.requires_blocking_reclaim, 1);
}

static void
db_test_present_role_capabilities_stay_split(db_test_state_t *state) {
    const db_gl_stream_upload_capability_t base_capability = {
        .target = DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER,
        .requested_storage = DB_GL_STREAM_UPLOAD_STORAGE_BUFFER_OBJECT,
        .supported_storage = DB_GL_STREAM_UPLOAD_STORAGE_BUFFER_OBJECT,
        .effective_storage = DB_GL_STREAM_UPLOAD_STORAGE_BUFFER_OBJECT,
        .requested_mode = DB_GL_STREAM_UPLOAD_MODE_MAP_RANGE,
        .supported_mode = DB_GL_STREAM_UPLOAD_MODE_MAP_RANGE,
        .effective_mode = DB_GL_STREAM_UPLOAD_MODE_MAP_RANGE,
        .sync_supported = 1,
        .sync_enabled = 1,
    };
    const db_gl_stream_upload_capability_t full_capability =
        db_gl_stream_upload_capability_for_role(&base_capability,
                                                DB_GL_UPLOAD_ROLE_PRESENT_FULL);
    const db_gl_stream_upload_capability_t partial_capability =
        db_gl_stream_upload_capability_for_role(
            &base_capability, DB_GL_UPLOAD_ROLE_PRESENT_PARTIAL);
    DB_TEST_EXPECT_EQ_INT(
        state, db_gl_stream_upload_uses_buffer_object(&full_capability), 1);
    DB_TEST_EXPECT_EQ_INT(
        state, db_gl_stream_upload_uses_buffer_object(&partial_capability), 1);
    DB_TEST_EXPECT_EQ_INT(state, full_capability.effective_mode,
                          DB_GL_STREAM_UPLOAD_MODE_MAP_RANGE);
    DB_TEST_EXPECT_EQ_INT(state, partial_capability.effective_mode,
                          DB_GL_STREAM_UPLOAD_MODE_MAP_RANGE);
    DB_TEST_EXPECT_EQ_INT(state, partial_capability.partial_updates_supported,
                          1);
    DB_TEST_EXPECT_EQ_INT(state, full_capability.partial_updates_supported, 0);
}

static void db_test_upload_capability_demotes_in_order(db_test_state_t *state) {
    db_gl_stream_upload_capability_t capability = {
        .target = DB_GL_UPLOAD_TARGET_VBO_ARRAY_BUFFER,
        .requested_storage = DB_GL_STREAM_UPLOAD_STORAGE_BUFFER_OBJECT,
        .supported_storage = DB_GL_STREAM_UPLOAD_STORAGE_BUFFER_OBJECT,
        .effective_storage = DB_GL_STREAM_UPLOAD_STORAGE_BUFFER_OBJECT,
        .requested_mode = DB_GL_STREAM_UPLOAD_MODE_PERSISTENT,
        .supported_mode = DB_GL_STREAM_UPLOAD_MODE_PERSISTENT,
        .effective_mode = DB_GL_STREAM_UPLOAD_MODE_PERSISTENT,
        .sync_supported = 1,
        .sync_enabled = 1,
    };

    DB_TEST_EXPECT_TRUE(
        state, db_gl_stream_upload_demote(
                   &capability, DB_GL_UPLOAD_FAILURE_MAP_NULL, 1) != 0);
    DB_TEST_EXPECT_EQ_INT(state, capability.effective_mode,
                          DB_GL_STREAM_UPLOAD_MODE_MAP_RANGE);
    DB_TEST_EXPECT_EQ_INT(state, capability.sync_enabled, 0);
    DB_TEST_EXPECT_EQ_INT(state, capability.demotion_reason,
                          DB_GL_UPLOAD_FAILURE_MAP_NULL);

    DB_TEST_EXPECT_TRUE(
        state, db_gl_stream_upload_demote(
                   &capability, DB_GL_UPLOAD_FAILURE_UNMAP_FAILED, 1) != 0);
    DB_TEST_EXPECT_EQ_INT(state, capability.effective_mode,
                          DB_GL_STREAM_UPLOAD_MODE_MAP_BUFFER);

    DB_TEST_EXPECT_TRUE(
        state, db_gl_stream_upload_demote(
                   &capability, DB_GL_UPLOAD_FAILURE_API_UNAVAILABLE, 1) != 0);
    DB_TEST_EXPECT_EQ_INT(state, capability.effective_mode,
                          DB_GL_STREAM_UPLOAD_MODE_SUB_DATA);

    DB_TEST_EXPECT_TRUE(
        state, db_gl_stream_upload_demote(
                   &capability, DB_GL_UPLOAD_FAILURE_STORAGE_ALLOC, 1) != 0);
    DB_TEST_EXPECT_EQ_INT(state, capability.effective_storage,
                          DB_GL_STREAM_UPLOAD_STORAGE_CLIENT);
}

static void db_test_pixel_upload_payload_from_surface_reports_metadata(
    db_test_state_t *state) {
    uint32_t rgba8_pixels[16] = {0};
    uint16_t rgba16f_pixels[64] = {0};
    const db_pixel_surface_t rgba8_surface = {
        .pixel_width = 4U,
        .pixel_height = 4U,
        .pixels = rgba8_pixels,
        .format = DB_PIXEL_FORMAT_RGBA8,
    };
    const db_pixel_surface_t rgba16f_surface = {
        .pixel_width = 4U,
        .pixel_height = 4U,
        .pixels = rgba16f_pixels,
        .format = DB_PIXEL_FORMAT_RGBA16F,
    };

    const db_gl_pixel_upload_payload_t rgba8_payload =
        db_gl_pixel_upload_payload_from_surface(&rgba8_surface);
    const db_gl_pixel_upload_payload_t rgba16f_payload =
        db_gl_pixel_upload_payload_from_surface(&rgba16f_surface);

    DB_TEST_EXPECT_EQ_INT(state, rgba8_payload.format,
                          DB_GL_SHADOW_PRESENT_TEXTURE_RGBA8);
    DB_TEST_EXPECT_EQ_SIZE(state, rgba8_payload.row_stride_bytes, 16U);
    DB_TEST_EXPECT_EQ_SIZE(state, rgba8_payload.total_bytes, 64U);
    DB_TEST_EXPECT_EQ_INT(state, rgba16f_payload.format,
                          DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F);
    DB_TEST_EXPECT_EQ_SIZE(state, rgba16f_payload.row_stride_bytes, 32U);
    DB_TEST_EXPECT_EQ_SIZE(state, rgba16f_payload.total_bytes, 128U);
}

static void
db_test_upload_stream_prepare_storage_does_not_create_buffer_hot_path(
    db_test_state_t *state) {
    db_gl_upload_stream_t stream = {0};
    const db_gl_stream_upload_capability_t capability = {
        .target = DB_GL_UPLOAD_TARGET_VBO_ARRAY_BUFFER,
        .requested_storage = DB_GL_STREAM_UPLOAD_STORAGE_BUFFER_OBJECT,
        .supported_storage = DB_GL_STREAM_UPLOAD_STORAGE_BUFFER_OBJECT,
        .effective_storage = DB_GL_STREAM_UPLOAD_STORAGE_BUFFER_OBJECT,
        .requested_mode = DB_GL_STREAM_UPLOAD_MODE_MAP_BUFFER,
        .supported_mode = DB_GL_STREAM_UPLOAD_MODE_MAP_BUFFER,
        .effective_mode = DB_GL_STREAM_UPLOAD_MODE_MAP_BUFFER,
    };
    db_gl_upload_stream_init(&stream, DB_GL_UPLOAD_TARGET_VBO_ARRAY_BUFFER,
                             capability, 0U, 1);
    stream.hot_path_fixed_capacity_bytes = 64U;

    DB_TEST_EXPECT_TRUE(state,
                        db_gl_upload_stream_prepare_storage(
                            &stream, "test_gl_shadow_present", 32U) == 0);
    DB_TEST_EXPECT_EQ_INT(state, stream.capability.demotion_reason,
                          DB_GL_UPLOAD_FAILURE_TARGET_ACQUIRE);
    DB_TEST_EXPECT_EQ_INT(state, stream.capability.effective_storage,
                          DB_GL_STREAM_UPLOAD_STORAGE_BUFFER_OBJECT);
    DB_TEST_EXPECT_EQ_INT(state, stream.capability.effective_mode,
                          DB_GL_STREAM_UPLOAD_MODE_SUB_DATA);
}

static void
db_test_gl_error_trace_drain_records_multiple_errors(db_test_state_t *state) {
    db_gl_error_trace_t trace = {0};
    const db_gl_upload_proc_table_t saved_table = g_upload_proc_table;
    db_test_gl_error_sequence[0] = DB_TEST_GL_INVALID_ENUM;
    db_test_gl_error_sequence[1] = DB_TEST_GL_INVALID_VALUE;
    db_test_gl_error_sequence[2] = GL_NO_ERROR;
    db_test_gl_error_sequence_index = 0U;
    g_upload_proc_table.get_error = db_test_get_error_stub;

    const size_t drained_count =
        db_gl_error_trace_drain(&trace, "unit", "vbo_array", "write");

    g_upload_proc_table = saved_table;
    db_test_gl_error_sequence[0] = GL_NO_ERROR;
    db_test_gl_error_sequence[1] = GL_NO_ERROR;
    db_test_gl_error_sequence[2] = GL_NO_ERROR;
    db_test_gl_error_sequence_index = 0U;

    DB_TEST_EXPECT_EQ_SIZE(state, drained_count, 2U);
    DB_TEST_EXPECT_EQ_SIZE(state, trace.count, 2U);
    DB_TEST_EXPECT_EQ_U32(state, trace.records[0].error_code,
                          DB_TEST_GL_INVALID_ENUM);
    DB_TEST_EXPECT_EQ_U32(state, trace.records[1].error_code,
                          DB_TEST_GL_INVALID_VALUE);
    DB_TEST_EXPECT_STR_EQ(state, trace.records[0].phase, "unit");
    DB_TEST_EXPECT_STR_EQ(state, trace.records[0].target, "vbo_array");
    DB_TEST_EXPECT_STR_EQ(state, trace.records[0].context, "write");
}

static void db_test_gl_error_trace_drain_is_bounded(db_test_state_t *state) {
    db_gl_error_trace_t trace = {0};
    const db_gl_upload_proc_table_t saved_table = g_upload_proc_table;
    for (size_t index = 0U; index < DB_GL_ERROR_TRACE_CAPACITY; index++) {
        db_test_gl_error_sequence[index] = DB_TEST_GL_INVALID_ENUM;
    }
    db_test_gl_error_sequence_index = 0U;
    g_upload_proc_table.get_error = db_test_get_error_stub;

    const size_t drained_count =
        db_gl_error_trace_drain(&trace, "unit", "vbo_array", "bounded");

    g_upload_proc_table = saved_table;
    memset(db_test_gl_error_sequence, 0, sizeof(db_test_gl_error_sequence));
    db_test_gl_error_sequence_index = 0U;
    const db_poll_policy_t *const policy =
        db_progress_policy_get(DB_PROGRESS_GL_ERROR_DRAIN);
    DB_TEST_EXPECT_EQ_SIZE(state, drained_count, policy->max_attempts);
    DB_TEST_EXPECT_EQ_SIZE(state, trace.count, DB_GL_ERROR_TRACE_CAPACITY);
}

static void
db_test_present_mode_format_reports_split_uploads(db_test_state_t *state) {
    enum { DB_TEST_PRESENT_MODE_TEXT_SIZE = 256 };
    char text[DB_TEST_PRESENT_MODE_TEXT_SIZE] = {0};
    const db_gl_runtime_mode_desc_t mode = {
        .full_present_upload =
            {
                .target = DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER,
                .requested_storage = DB_GL_STREAM_UPLOAD_STORAGE_CLIENT,
                .supported_storage = DB_GL_STREAM_UPLOAD_STORAGE_CLIENT,
                .effective_storage = DB_GL_STREAM_UPLOAD_STORAGE_CLIENT,
                .requested_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA,
                .supported_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA,
                .effective_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA,
            },
        .partial_present_upload =
            {
                .target = DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER,
                .requested_storage = DB_GL_STREAM_UPLOAD_STORAGE_BUFFER_OBJECT,
                .supported_storage = DB_GL_STREAM_UPLOAD_STORAGE_BUFFER_OBJECT,
                .effective_storage = DB_GL_STREAM_UPLOAD_STORAGE_BUFFER_OBJECT,
                .requested_mode = DB_GL_STREAM_UPLOAD_MODE_MAP_RANGE,
                .supported_mode = DB_GL_STREAM_UPLOAD_MODE_MAP_RANGE,
                .effective_mode = DB_GL_STREAM_UPLOAD_MODE_MAP_RANGE,
            },
        .preserve_mode = DB_GL_SHADOW_PRESENT_REPLACE_CONTENTS,
    };
    db_gl_runtime_mode_format_present(text, sizeof(text), &mode);
    DB_TEST_EXPECT_TRUE(
        state, strstr(text, "full_present_upload=client_upload") != NULL);
    DB_TEST_EXPECT_TRUE(
        state, strstr(text, "partial_present_upload=map_range") != NULL);
}

static void
db_test_preserve_mode_active_slot_count_matches_mode(db_test_state_t *state) {
    DB_TEST_EXPECT_EQ_U32(state,
                          db_gl_shadow_present_active_slot_count(
                              DB_GL_SHADOW_PRESENT_REPLACE_CONTENTS, 3U),
                          2U);
    DB_TEST_EXPECT_EQ_U32(state,
                          db_gl_shadow_present_active_slot_count(
                              DB_GL_SHADOW_PRESENT_PRESERVE_SINGLE_SOURCE, 3U),
                          1U);
    DB_TEST_EXPECT_EQ_U32(state,
                          db_gl_shadow_present_active_slot_count(
                              DB_GL_SHADOW_PRESENT_PRESERVE_RING_COHERENT, 2U),
                          2U);
    DB_TEST_EXPECT_EQ_U32(state,
                          db_gl_shadow_present_active_slot_count(
                              DB_GL_SHADOW_PRESENT_PRESERVE_RING_COHERENT, 3U),
                          2U);
}

static void db_test_shadow_present_required_previous_frames_follows_ring_mode(
    db_test_state_t *state) {
    const db_gl_shadow_present_state_t single_source = {
        .preserve_mode = DB_GL_SHADOW_PRESENT_PRESERVE_SINGLE_SOURCE,
        .slot_count = 1U,
    };
    const db_gl_shadow_present_state_t ring = {
        .preserve_mode = DB_GL_SHADOW_PRESENT_PRESERVE_RING_COHERENT,
        .slot_count = 2U,
    };

    DB_TEST_EXPECT_EQ_U32(
        state, db_gl_shadow_present_required_previous_frames(&single_source),
        0U);
    DB_TEST_EXPECT_EQ_U32(
        state, db_gl_shadow_present_required_previous_frames(&ring), 1U);
}

static void db_test_backbuffer_seed_policy_is_explicit(db_test_state_t *state) {
    DB_TEST_EXPECT_EQ_U32(state, db_backing_seed_frame_count(0U), 0U);
    DB_TEST_EXPECT_EQ_U32(state, db_backing_seed_frame_count(1U), 1U);
    DB_TEST_EXPECT_EQ_U32(state, db_backing_seed_frame_count(2U), 2U);
}

static void
db_test_full_upload_slot_choice_consumes_ring_fallback(db_test_state_t *state) {
    db_gl_shadow_present_state_t present = {
        .slot_count = 2U,
        .write_slot_index = 1U,
        .present_slot_index = 0U,
        .preserve_mode = DB_GL_SHADOW_PRESENT_PRESERVE_RING_COHERENT,
        .upload_slots =
            {
                {.slot_valid = 1, .slot_matches_shadow = 1},
                {.slot_valid = 1, .slot_matches_shadow = 1},
            },
    };
    db_gl_shadow_present_full_upload_slot_choice_t choice = {0};

    DB_TEST_EXPECT_TRUE(state, db_gl_shadow_present_choose_full_upload_slot(
                                   &present, 1, 0U, 0x3U, &choice) != 0);
    DB_TEST_EXPECT_EQ_U32(state, choice.slot_index, 0U);
    DB_TEST_EXPECT_EQ_INT(state, choice.requires_blocking_reclaim, 1);
}

static void db_test_shadow_upload_trace_records_full_upload_fallback(
    db_test_state_t *state) {
    db_gl_shadow_upload_trace_t trace = {0};
    uint16_t rgba16f_pixels[64] = {0};
    const db_pixel_surface_t surface = {
        .pixel_width = 4U,
        .pixel_height = 4U,
        .pixels = rgba16f_pixels,
        .format = DB_PIXEL_FORMAT_RGBA16F,
    };
    const db_damage_block_t full_block = db_damage_block_full(4U, 4U);
    const db_gl_pixel_upload_payload_t payload =
        db_gl_pixel_upload_payload_from_surface(&surface);

    db_gl_shadow_upload_trace_reset(&trace);
    db_gl_shadow_upload_trace_capture_pixel_payload(&trace, &payload);
    db_gl_shadow_upload_trace_capture_full_upload_attempt(
        &trace, 0U, payload.total_bytes, "shadow_full_upload_target",
        "mapped_pbo", "map_buffer");
    db_gl_shadow_upload_trace_capture_upload_span(&trace, &full_block, 0U,
                                                  payload.total_bytes,
                                                  "shadow_full_upload_target");
    db_gl_shadow_upload_trace_note_history(&trace, 1U, 64U, 32U,
                                           "assembled_compact");
    db_gl_shadow_upload_trace_note_fallback(
        &trace, "shadow_full_upload_client_fallback");
    db_gl_shadow_upload_trace_note_execution(&trace,
                                             "client_buffer_then_subdata");
    db_gl_shadow_upload_trace_note_seed(&trace, "shadow_ring_initial_seed");
    trace.error_trace.records[0] = (db_gl_error_record_t){
        .error_code = DB_TEST_GL_INVALID_ENUM,
        .phase = "shadow_full_upload",
        .target = "pbo_unpack",
        .context = "begin_full_upload_target",
    };
    trace.error_trace.count = 1U;

    DB_TEST_EXPECT_EQ_INT(state, trace.full_upload_attempted, 1);
    DB_TEST_EXPECT_EQ_INT(state, trace.full_upload_executed, 1);
    DB_TEST_EXPECT_EQ_U32(state, trace.slot_index, 0U);
    DB_TEST_EXPECT_EQ_SIZE(state, trace.total_bytes, payload.total_bytes);
    DB_TEST_EXPECT_EQ_U32(state, trace.required_previous_frames, 1U);
    DB_TEST_EXPECT_EQ_SIZE(state, trace.historical_block_count, 64U);
    DB_TEST_EXPECT_EQ_SIZE(state, trace.repair_block_count, 32U);
    DB_TEST_EXPECT_EQ_INT(state, trace.seeded_shadow_ring, 1);
    DB_TEST_EXPECT_EQ_INT(state, trace.pixel_payload.format,
                          DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F);
    DB_TEST_EXPECT_EQ_SIZE(state, trace.upload_span_count, 1U);
    DB_TEST_EXPECT_EQ_SIZE(state, trace.error_trace.count, 1U);
    DB_TEST_EXPECT_STR_EQ(state, trace.error_trace.records[0].phase,
                          "shadow_full_upload");
}

static void
db_test_shadow_upload_trace_does_not_mark_execution_before_upload_runs(
    db_test_state_t *state) {
    enum {
        db_test_shadow_upload_bytes = 4096U,
    };
    db_gl_shadow_upload_trace_t trace = {0};

    db_gl_shadow_upload_trace_reset(&trace);
    db_gl_shadow_upload_trace_capture_full_upload_attempt(
        &trace, 1U, db_test_shadow_upload_bytes, "shadow_full_upload_target",
        "mapped_pbo", "map_buffer");

    DB_TEST_EXPECT_EQ_INT(state, trace.full_upload_attempted, 1);
    DB_TEST_EXPECT_EQ_INT(state, trace.full_upload_executed, 0);
    DB_TEST_EXPECT_TRUE(state, trace.executed_upload_mode_label == NULL);
}

static void
db_test_repair_full_upload_target_copies_full_surface_for_fresh_target(
    db_test_state_t *state) {
    const uint32_t db_test_src_pixel0 = UINT32_C(0x11223344);
    const uint32_t db_test_src_pixel1 = UINT32_C(0x55667788);
    const uint32_t db_test_src_pixel2 = UINT32_C(0x99AABBCC);
    const uint32_t db_test_src_pixel3 = UINT32_C(0xDDEEFF00);
    uint32_t src_pixels[4] = {db_test_src_pixel0, db_test_src_pixel1,
                              db_test_src_pixel2, db_test_src_pixel3};
    uint32_t dst_pixels[4] = {0};
    const db_pixel_surface_t source_surface = {
        .pixel_width = 2U,
        .pixel_height = 2U,
        .pixels = src_pixels,
        .format = DB_PIXEL_FORMAT_RGBA8,
    };
    db_gl_shadow_present_state_t present = {
        .slot_count = 1U,
    };
    const db_gl_shadow_present_full_upload_target_t target = {
        .pixel_surface =
            {
                .pixel_width = 2U,
                .pixel_height = 2U,
                .pixels = dst_pixels,
                .format = DB_PIXEL_FORMAT_RGBA8,
            },
        .slot_index = 0U,
        .mode = DB_GL_SHADOW_FULL_UPLOAD_TARGET_DIRECT_CLIENT_TEXTURE_UPLOAD,
    };

    db_gl_shadow_present_repair_full_upload_target(
        &present, &target, &source_surface, (db_pixel_block_view_t){NULL, 0U});

    DB_TEST_EXPECT_EQ_U32(state, dst_pixels[0], src_pixels[0]);
    DB_TEST_EXPECT_EQ_U32(state, dst_pixels[1], src_pixels[1]);
    DB_TEST_EXPECT_EQ_U32(state, dst_pixels[2], src_pixels[2]);
    DB_TEST_EXPECT_EQ_U32(state, dst_pixels[3], src_pixels[3]);
    DB_TEST_EXPECT_EQ_INT(state, present.upload_slots[0].slot_valid, 1);
}

static void
db_test_upload_slots_rotate_except_single_source(db_test_state_t *state) {
    DB_TEST_EXPECT_EQ_U32(state,
                          db_gl_shadow_present_next_write_slot_after_present(
                              DB_GL_SHADOW_PRESENT_REPLACE_CONTENTS, 0, 0U, 2U),
                          1U);
    DB_TEST_EXPECT_EQ_U32(state,
                          db_gl_shadow_present_next_write_slot_after_present(
                              DB_GL_SHADOW_PRESENT_REPLACE_CONTENTS, 0, 1U, 2U),
                          0U);
    DB_TEST_EXPECT_EQ_U32(
        state,
        db_gl_shadow_present_next_write_slot_after_present(
            DB_GL_SHADOW_PRESENT_PRESERVE_SINGLE_SOURCE, 1, 1U, 2U),
        1U);
    DB_TEST_EXPECT_EQ_U32(
        state,
        db_gl_shadow_present_next_write_slot_after_present(
            DB_GL_SHADOW_PRESENT_PRESERVE_RING_COHERENT, 1, 1U, 2U),
        0U);
}

unsigned db_gl_shadow_present_test_run_all(void) {
    static const db_test_case_t cases[] = {
        {"unpack_upload_storage_reuses_client_buffer",
         db_test_unpack_upload_storage_reuses_client_buffer},
        {"ring_scheduler_selects_ready_alternate_slot",
         db_test_ring_scheduler_selects_ready_alternate_slot},
        {"ring_scheduler_falls_back_when_all_slots_busy",
         db_test_ring_scheduler_falls_back_when_all_slots_busy},
        {"present_role_capabilities_stay_split",
         db_test_present_role_capabilities_stay_split},
        {"upload_capability_demotes_in_order",
         db_test_upload_capability_demotes_in_order},
        {"pixel_upload_payload_from_surface_reports_metadata",
         db_test_pixel_upload_payload_from_surface_reports_metadata},
        {"upload_stream_prepare_storage_does_not_create_buffer_hot_path",
         db_test_upload_stream_prepare_storage_does_not_create_buffer_hot_path},
        {"gl_error_trace_drain_records_multiple_errors",
         db_test_gl_error_trace_drain_records_multiple_errors},
        {"gl_error_trace_drain_is_bounded",
         db_test_gl_error_trace_drain_is_bounded},
        {"present_mode_format_reports_split_uploads",
         db_test_present_mode_format_reports_split_uploads},
        {"preserve_mode_active_slot_count_matches_mode",
         db_test_preserve_mode_active_slot_count_matches_mode},
        {"shadow_present_required_previous_frames_follows_ring_mode",
         db_test_shadow_present_required_previous_frames_follows_ring_mode},
        {"backbuffer_seed_policy_is_explicit",
         db_test_backbuffer_seed_policy_is_explicit},
        {"full_upload_slot_choice_consumes_ring_fallback",
         db_test_full_upload_slot_choice_consumes_ring_fallback},
        {"shadow_upload_trace_records_full_upload_fallback",
         db_test_shadow_upload_trace_records_full_upload_fallback},
        {"shadow_upload_trace_does_not_mark_execution_before_upload_runs",
         db_test_shadow_upload_trace_does_not_mark_execution_before_upload_runs},
        {"repair_full_upload_target_copies_full_surface_for_fresh_target",
         db_test_repair_full_upload_target_copies_full_surface_for_fresh_target},
        {"upload_slots_rotate_except_single_source",
         db_test_upload_slots_rotate_except_single_source},
    };
    return db_test_run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}
