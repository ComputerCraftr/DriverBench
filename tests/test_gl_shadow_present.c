#include "support/test_harness.h"

#include <string.h>

#include "renderers/renderer_gl_common.h"
#include "renderers/renderer_gl_shadow_present_internal.h"
#include "renderers/renderer_history_common.h"

static void
db_test_unpack_upload_storage_reuses_client_buffer(db_test_state_t *state) {
    db_gl_shadow_present_state_t present = {
        .requested_partial_upload_capability =
            {
                .target = DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER,
                .requested_storage = DB_GL_STREAM_UPLOAD_STORAGE_CLIENT,
                .supported_storage = DB_GL_STREAM_UPLOAD_STORAGE_CLIENT,
                .effective_storage = DB_GL_STREAM_UPLOAD_STORAGE_CLIENT,
                .requested_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA,
                .supported_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA,
                .effective_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA,
            },
        .effective_partial_upload_capability =
            {
                .target = DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER,
                .requested_storage = DB_GL_STREAM_UPLOAD_STORAGE_CLIENT,
                .supported_storage = DB_GL_STREAM_UPLOAD_STORAGE_CLIENT,
                .effective_storage = DB_GL_STREAM_UPLOAD_STORAGE_CLIENT,
                .requested_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA,
                .supported_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA,
                .effective_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA,
            },
    };
    db_gl_upload_stream_init(
        &present.unpack_stream, DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER,
        present.effective_partial_upload_capability, 0U, 1);
    DB_TEST_EXPECT_TRUE(state,
                        db_gl_shadow_present_prepare_unpack_upload_storage(
                            &present, "test_gl_shadow_present", 64U) != 0);
    DB_TEST_EXPECT_TRUE(state, present.unpack_stream.client_storage != NULL);
    DB_TEST_EXPECT_EQ_SIZE(state, present.unpack_stream.reserved_bytes, 64U);

    void *first_ptr = present.unpack_stream.client_storage;
    DB_TEST_EXPECT_TRUE(state,
                        db_gl_shadow_present_prepare_unpack_upload_storage(
                            &present, "test_gl_shadow_present", 32U) != 0);
    DB_TEST_EXPECT_TRUE(state,
                        present.unpack_stream.client_storage == first_ptr);
    DB_TEST_EXPECT_EQ_SIZE(state, present.unpack_stream.reserved_bytes, 64U);

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
    DB_TEST_EXPECT_STR_EQ(state, acquire.reason, "all_ring_slots_busy");
}

static void
db_test_present_role_capabilities_stay_split(db_test_state_t *state) {
    const db_gl_upload_probe_result_t probe = {
        .use_map_range_upload = 1,
    };
    const db_gl_stream_upload_capability_t full_capability =
        db_gl_stream_upload_capability_for_role(
            DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER, &probe, 1,
            DB_GL_UPLOAD_ROLE_PRESENT_FULL,
            DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F);
    const db_gl_stream_upload_capability_t partial_capability =
        db_gl_stream_upload_capability_for_role(
            DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER, &probe, 1,
            DB_GL_UPLOAD_ROLE_PRESENT_PARTIAL,
            DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F);
#ifdef __APPLE__
    DB_TEST_EXPECT_EQ_INT(
        state, db_gl_stream_upload_uses_buffer_object(&full_capability), 0);
    DB_TEST_EXPECT_EQ_INT(
        state, db_gl_stream_upload_uses_buffer_object(&partial_capability), 1);
#else
    DB_TEST_EXPECT_EQ_INT(
        state, db_gl_stream_upload_uses_buffer_object(&full_capability), 1);
    DB_TEST_EXPECT_EQ_INT(
        state, db_gl_stream_upload_uses_buffer_object(&partial_capability), 1);
#endif
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
                              DB_GL_SHADOW_PRESENT_REPLACE_CONTENTS),
                          1U);
    DB_TEST_EXPECT_EQ_U32(state,
                          db_gl_shadow_present_active_slot_count(
                              DB_GL_SHADOW_PRESENT_PRESERVE_SINGLE_SOURCE),
                          1U);
    DB_TEST_EXPECT_EQ_U32(state,
                          db_gl_shadow_present_active_slot_count(
                              DB_GL_SHADOW_PRESENT_PRESERVE_RING_COHERENT),
                          2U);
}

static void db_test_backbuffer_seed_policy_is_explicit(db_test_state_t *state) {
    DB_TEST_EXPECT_EQ_U32(state, db_history_backbuffer_seed_frame_count(0U),
                          0U);
    DB_TEST_EXPECT_EQ_U32(state, db_history_backbuffer_seed_frame_count(1U),
                          1U);
    DB_TEST_EXPECT_EQ_U32(state, db_history_backbuffer_seed_frame_count(2U),
                          2U);
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

static void
db_test_replace_mode_does_not_rotate_write_slot(db_test_state_t *state) {
    DB_TEST_EXPECT_EQ_U32(state,
                          db_gl_shadow_present_next_write_slot_after_present(
                              DB_GL_SHADOW_PRESENT_REPLACE_CONTENTS, 0, 0U, 2U),
                          0U);
    DB_TEST_EXPECT_EQ_U32(state,
                          db_gl_shadow_present_next_write_slot_after_present(
                              DB_GL_SHADOW_PRESENT_REPLACE_CONTENTS, 0, 1U, 2U),
                          1U);
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
        {"present_mode_format_reports_split_uploads",
         db_test_present_mode_format_reports_split_uploads},
        {"preserve_mode_active_slot_count_matches_mode",
         db_test_preserve_mode_active_slot_count_matches_mode},
        {"backbuffer_seed_policy_is_explicit",
         db_test_backbuffer_seed_policy_is_explicit},
        {"full_upload_slot_choice_consumes_ring_fallback",
         db_test_full_upload_slot_choice_consumes_ring_fallback},
        {"replace_mode_does_not_rotate_write_slot",
         db_test_replace_mode_does_not_rotate_write_slot},
    };
    return db_test_run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}
