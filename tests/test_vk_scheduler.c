#include "support/test_harness.h"

#include "core/db_format_contract.h"
#include "core/db_frame_plan.h"
#include "core/db_render_ir.h"
#include "core/db_render_types.h"
#include "renderers/vulkan_1_2_multi_gpu/vk_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <vulkan/vulkan_core.h>

enum {
    TEST_GRID_EXTENT = 10U,
    TEST_PIXEL_WIDTH = 100U,
    TEST_PIXEL_HEIGHT = 80U,
    TEST_SLOT_FRAME = 12U,
    TEST_SCHEDULING_EPOCH = 9U,
    TEST_CONTENT_GENERATION = 4U,
    TEST_IR_COMMAND_BYTES = 1024U,
    TEST_SPLIT_DEFAULT_NS = 8000000U,
    TEST_SPLIT_COARSE_BEST_NS = 7000000U,
    TEST_SPLIT_REFINED_BEST_NS = 6000000U,
    TEST_SPLIT_COARSE_BEST_BPS = 5000U,
    TEST_SPLIT_REFINED_LOW_BPS = 3750U,
    TEST_SPLIT_REFINED_HIGH_BPS = 6250U,
    TEST_SPLIT_SEARCH_ITERATION_LIMIT =
        DB_VK_SPLIT_SEARCH_SHARE_COUNT * (DB_VK_SPLIT_SAMPLES_PER_SHARE + 1U),
};

static const double test_primary_ms = 10.0;
static const double test_candidate_fast_ms = 9.0;
static const double test_candidate_marginal_ms = 9.6;
static const double test_candidate_faster_ms = 8.0;
static const double test_primary_p95_ms = 12.0;
static const double test_candidate_good_p95_ms = 12.5;
static const double test_candidate_bad_p95_ms = 14.0;

static db_frame_plan_t
db_test_vk_piece_frame_plan(const db_render_ir_fill_t *fills, size_t count) {
    static max_align_t commands[TEST_IR_COMMAND_BYTES / sizeof(max_align_t)] = {
        0};
    static db_render_ir_fill_t fill_storage[8] = {};
    static db_render_ir_resource_t resources[1] = {};
    static db_render_ir_region_t regions[8] = {};
    static db_render_ir_band_t bands[8] = {};
    static db_render_ir_span_t spans[8] = {};
    db_render_ir_store_t store = {.commands = commands,
                                  .command_capacity = sizeof(commands),
                                  .fills = fill_storage,
                                  .fill_capacity = 8U,
                                  .resources = resources,
                                  .resource_capacity = 1U,
                                  .regions = regions,
                                  .region_capacity = 8U,
                                  .bands = bands,
                                  .band_capacity = 8U,
                                  .spans = spans,
                                  .span_capacity = 8U};
    db_render_ir_resource_id_t target = DB_RENDER_IR_INVALID_ID;
    (void)db_render_ir_add_resource(
        &store,
        &(const db_render_ir_resource_t){
            .kind = DB_RENDER_IR_RESOURCE_CANONICAL_TARGET,
            .width = TEST_GRID_EXTENT,
            .height = TEST_GRID_EXTENT,
            .format = DB_PIXEL_FORMAT_RGBA8},
        &target);
    (void)db_render_ir_begin_target(&store, target);
    (void)db_render_ir_fill_rects(&store, target, fills, count,
                                  DB_RENDER_IR_INVALID_ID);
    (void)db_render_ir_end_target(&store, target);
    return (db_frame_plan_t){
        .frame_index = 3U,
        .grid_cols = TEST_GRID_EXTENT,
        .grid_rows = TEST_GRID_EXTENT,
        .pixel_width = TEST_PIXEL_WIDTH,
        .pixel_height = TEST_PIXEL_HEIGHT,
        .update_ir = db_render_ir_store_view(&store),
    };
}

static void db_test_vk_measured_benefit_gate(db_test_state_t *state) {
    DB_TEST_EXPECT_TRUE(state,
                        db_vk_multi_gpu_measured_benefit(
                            test_primary_ms, test_candidate_fast_ms,
                            test_primary_p95_ms, test_candidate_good_p95_ms));
    DB_TEST_EXPECT_TRUE(state, !db_vk_multi_gpu_measured_benefit(
                                   test_primary_ms, test_candidate_marginal_ms,
                                   test_primary_p95_ms, test_primary_p95_ms));
    DB_TEST_EXPECT_TRUE(state,
                        !db_vk_multi_gpu_measured_benefit(
                            test_primary_ms, test_candidate_faster_ms,
                            test_primary_p95_ms, test_candidate_bad_p95_ms));
    DB_TEST_EXPECT_TRUE(state,
                        !db_vk_multi_gpu_measured_benefit(0.0, 1.0, 1.0, 1.0));
}

static void db_test_vk_external_interop_classification(db_test_state_t *state) {
    DB_TEST_EXPECT_TRUE(state, db_vk_external_interop_usable(1, 1, 1, 1));
    DB_TEST_EXPECT_TRUE(state, !db_vk_external_interop_usable(0, 1, 1, 1));
    DB_TEST_EXPECT_TRUE(state, !db_vk_external_interop_usable(1, 0, 1, 1));
    DB_TEST_EXPECT_TRUE(state, !db_vk_external_interop_usable(1, 1, 0, 1));
    DB_TEST_EXPECT_TRUE(state, !db_vk_external_interop_usable(1, 1, 1, 0));
}

static void
db_test_vk_device_group_peer_read_classification(db_test_state_t *state) {
    DB_TEST_EXPECT_TRUE(state, db_vk_device_group_peer_read_usable(
                                   VK_PEER_MEMORY_FEATURE_GENERIC_SRC_BIT));
    DB_TEST_EXPECT_TRUE(state, !db_vk_device_group_peer_read_usable(
                                   VK_PEER_MEMORY_FEATURE_GENERIC_DST_BIT));
}

static void db_test_vk_hdr_aware_primary_selection(db_test_state_t *state) {
    DeviceSelectionState selection = {.phys_count = 2U};
    for (uint32_t index = 0U; index < selection.phys_count; index++) {
        selection.phys_info[index].supports_graphics = 1;
        selection.phys_info[index].supports_present = 1;
        selection.phys_info[index].queue_family_index = 0U;
        selection.phys_info[index].properties.deviceType =
            VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    }
    selection.phys_info[1].supports_hdr10_surface_pair = 1;
    selection.phys_info[1].supports_hdr_metadata = 1;

    DB_TEST_EXPECT_EQ_U32(state,
                          db_vk_choose_primary_physical_index_for_output(
                              &selection, DB_OUTPUT_FORMAT_SDR),
                          0U);
    DB_TEST_EXPECT_EQ_U32(state,
                          db_vk_choose_primary_physical_index_for_output(
                              &selection, DB_OUTPUT_FORMAT_AUTO),
                          1U);
    DB_TEST_EXPECT_EQ_U32(state,
                          db_vk_choose_primary_physical_index_for_output(
                              &selection, DB_OUTPUT_FORMAT_HDR),
                          1U);

    selection.phys_info[1].supports_hdr_metadata = 0;
    DB_TEST_EXPECT_EQ_U32(state,
                          db_vk_choose_primary_physical_index_for_output(
                              &selection, DB_OUTPUT_FORMAT_AUTO),
                          0U);
}

static void db_test_vk_piece_plan_preserves_order(db_test_state_t *state) {
    const db_render_ir_fill_t blocks[] = {
        {.rect = {.x = 2, .y = 1, .width = 3, .height = 2},
         .color = {.rgba = {1.0, 0.0, 0.0, 1.0}}},
        {.rect = {.x = 4, .y = 2, .width = 2, .height = 3},
         .color = {.rgba = {0.0, 1.0, 0.0, 1.0}}},
    };
    const db_frame_plan_t frame =
        db_test_vk_piece_frame_plan(blocks, sizeof(blocks) / sizeof(blocks[0]));
    db_vk_present_piece_t pieces[2] = {0};
    db_vk_lane_assignment_t assignments[2] = {0};
    db_vk_execution_plan_t plan = {0};
    DB_TEST_EXPECT_TRUE(
        state, db_vk_build_execution_plan(
                   &frame, 2U, DB_VK_SCHEDULING_PRIMARY_ONLY, NULL, 7U, 11U,
                   pieces, sizeof(pieces) / sizeof(pieces[0]), assignments,
                   sizeof(assignments) / sizeof(assignments[0]), &plan));
    DB_TEST_EXPECT_EQ_U32(state, (uint32_t)plan.piece_count, 1U);
    DB_TEST_EXPECT_EQ_U32(state, pieces[0].piece_index, 0U);
    DB_TEST_EXPECT_EQ_U32(state, pieces[0].instance_count, 2U);
    DB_TEST_EXPECT_EQ_U32(state, pieces[0].command_range.command_count, 1U);
    DB_TEST_EXPECT_EQ_U32(state, pieces[0].source_rect.offset.x, 20U);
    DB_TEST_EXPECT_EQ_U32(state, pieces[0].source_rect.offset.y, 8U);
    DB_TEST_EXPECT_EQ_U32(state, pieces[0].source_rect.extent.width, 40U);
    DB_TEST_EXPECT_EQ_U32(state, pieces[0].source_rect.extent.height, 32U);
    DB_TEST_EXPECT_EQ_U32(state, assignments[0].lane, 0U);
}

static void
db_test_vk_instance_bounds_use_top_left_origin(db_test_state_t *state) {
    const db_render_ir_fill_t fills[] = {
        {.rect = {.x = 0, .y = 0, .width = 10, .height = 1},
         .color = {.rgba = {1.0, 0.0, 0.0, 1.0}}},
        {.rect = {.x = 0, .y = 9, .width = 10, .height = 1},
         .color = {.rgba = {0.0, 1.0, 0.0, 1.0}}},
    };
    const db_frame_plan_t plan = db_test_vk_piece_frame_plan(fills, 2U);
    db_vk_ir_execute_instance_t instances[2] = {0};
    DB_TEST_EXPECT_EQ_SIZE(
        state, db_vk_write_frame_instances(&plan, instances, 2U), 2U);
    DB_TEST_EXPECT_TRUE(state, instances[0].rect[1] < 0.0F);
    DB_TEST_EXPECT_TRUE(state, instances[1].rect[1] > 0.0F);
    DB_TEST_EXPECT_TRUE(state, instances[0].rect[3] > 0.0F);
    DB_TEST_EXPECT_TRUE(state, instances[1].rect[3] > 0.0F);
}

static void db_test_vk_piece_plan_overflow_is_primary(db_test_state_t *state) {
    const db_render_ir_fill_t blocks[] = {
        {.rect = {.width = 1, .height = 1},
         .color = {.rgba = {1.0, 0.0, 0.0, 1.0}}},
        {.rect = {.y = 1, .width = 1, .height = 1},
         .color = {.rgba = {0.0, 1.0, 0.0, 1.0}}},
    };
    db_frame_plan_t frame =
        db_test_vk_piece_frame_plan(blocks, sizeof(blocks) / sizeof(blocks[0]));
    db_vk_present_piece_t piece = {0};
    db_vk_lane_assignment_t assignment = {0};
    db_vk_execution_plan_t plan = {0};
    DB_TEST_EXPECT_TRUE(
        state, db_vk_build_execution_plan(
                   &frame, 2U, DB_VK_SCHEDULING_THROUGHPUT_WEIGHTED_CHUNKS,
                   NULL, 2U, 4U, &piece, 1U, &assignment, 1U, &plan));
    DB_TEST_EXPECT_TRUE(state, plan.primary_only_fallback);
    DB_TEST_EXPECT_EQ_U32(state, assignment.lane, 0U);
    DB_TEST_EXPECT_EQ_U32(state, piece.source_rect.extent.width, 100U);
    DB_TEST_EXPECT_EQ_U32(state, piece.source_rect.extent.height, 80U);
}

static void db_test_vk_overlapping_pieces_are_primary(db_test_state_t *state) {
    const db_render_ir_fill_t blocks[] = {
        {.rect = {.x = 1, .y = 1, .width = 4, .height = 4},
         .color = {.rgba = {1.0, 0.0, 0.0, 1.0}}},
        {.rect = {.x = 3, .y = 3, .width = 4, .height = 4},
         .color = {.rgba = {0.0, 1.0, 0.0, 1.0}}},
    };
    const db_frame_plan_t frame =
        db_test_vk_piece_frame_plan(blocks, sizeof(blocks) / sizeof(blocks[0]));
    db_vk_present_piece_t pieces[2] = {0};
    db_vk_lane_assignment_t assignments[2] = {0};
    db_vk_execution_plan_t plan = {0};
    const double costs[2] = {10.0, 1.0};
    DB_TEST_EXPECT_TRUE(
        state, db_vk_build_execution_plan(
                   &frame, 2U, DB_VK_SCHEDULING_THROUGHPUT_WEIGHTED_CHUNKS,
                   costs, 1U, 1U, pieces, 2U, assignments, 2U, &plan));
    DB_TEST_EXPECT_EQ_U32(state, plan.policy, DB_VK_SCHEDULING_PRIMARY_ONLY);
    DB_TEST_EXPECT_EQ_U32(state, assignments[0].lane, 0U);
}

static void db_test_vk_sync_fd_state_machine(db_test_state_t *state) {
    DB_TEST_EXPECT_TRUE(
        state, db_vk_sync_state_transition_valid(DB_VK_SYNC_IDLE,
                                                 DB_VK_SYNC_SIGNAL_SUBMITTED));
    DB_TEST_EXPECT_TRUE(
        state, db_vk_sync_state_transition_valid(DB_VK_SYNC_SIGNAL_SUBMITTED,
                                                 DB_VK_SYNC_FD_EXPORTED));
    DB_TEST_EXPECT_TRUE(
        state, db_vk_sync_state_transition_valid(DB_VK_SYNC_FD_EXPORTED,
                                                 DB_VK_SYNC_FD_IMPORTED));
    DB_TEST_EXPECT_TRUE(
        state, db_vk_sync_state_transition_valid(DB_VK_SYNC_FD_IMPORTED,
                                                 DB_VK_SYNC_WAIT_SUBMITTED));
    DB_TEST_EXPECT_TRUE(
        state, db_vk_sync_state_transition_valid(DB_VK_SYNC_WAIT_SUBMITTED,
                                                 DB_VK_SYNC_PAYLOAD_CONSUMED));
    DB_TEST_EXPECT_TRUE(
        state, !db_vk_sync_state_transition_valid(DB_VK_SYNC_FD_EXPORTED,
                                                  DB_VK_SYNC_WAIT_SUBMITTED));
}

static void db_test_vk_slot_generation_validation(db_test_state_t *state) {
    const db_vk_execution_plan_t plan = {
        .scheduling_epoch = TEST_SCHEDULING_EPOCH,
        .content_generation = TEST_CONTENT_GENERATION};
    db_vk_lane_slot_t slot = {
        .content_generation = TEST_CONTENT_GENERATION,
        .scheduling_epoch = TEST_SCHEDULING_EPOCH,
        .last_applied_frame = TEST_SLOT_FRAME,
        .valid_piece_count = 1U,
    };
    DB_TEST_EXPECT_TRUE(
        state, db_vk_slot_result_is_current(&slot, &plan, TEST_SLOT_FRAME));
    slot.quarantined = 1;
    DB_TEST_EXPECT_TRUE(
        state, !db_vk_slot_result_is_current(&slot, &plan, TEST_SLOT_FRAME));
}

static void db_test_vk_transport_negotiates_ownership(db_test_state_t *state) {
    db_vk_transport_capabilities_t caps = {
        .dma_buf_external_image = 1,
        .dma_buf_modifier_compatible = 1,
        .sync_fd_semaphore = 1,
        .external_domain_supported = 1,
    };
    db_vk_transport_profile_t profile = db_vk_negotiate_transport(&caps);
    DB_TEST_EXPECT_TRUE(state, profile.supported);
    DB_TEST_EXPECT_EQ_U32(state, profile.transport,
                          DB_VK_TRANSPORT_DMA_BUF_IMAGE);
    DB_TEST_EXPECT_EQ_U32(state, profile.ownership_domain,
                          DB_VK_EXTERNAL_OWNERSHIP_EXTERNAL);

    caps.foreign_domain_required = 1;
    caps.foreign_domain_supported_by_both = 0;
    profile = db_vk_negotiate_transport(&caps);
    DB_TEST_EXPECT_TRUE(state, !profile.supported);
    caps.foreign_domain_supported_by_both = 1;
    profile = db_vk_negotiate_transport(&caps);
    DB_TEST_EXPECT_TRUE(state, profile.supported);
    DB_TEST_EXPECT_EQ_U32(state, profile.ownership_domain,
                          DB_VK_EXTERNAL_OWNERSHIP_FOREIGN);
}

static void db_test_vk_paired_calibration(db_test_state_t *state) {
    db_vk_calibration_pair_t pairs[DB_VK_CALIBRATION_PAIR_COUNT] = {0};
    for (size_t index = 0U; index < DB_VK_CALIBRATION_PAIR_COUNT; index++) {
        pairs[index] = (db_vk_calibration_pair_t){
            .primary_ms = test_primary_ms,
            .candidate_ms = test_candidate_fast_ms,
            .primary_state_hash = 1U,
            .candidate_state_hash = 1U,
            .primary_working_hash = 2U,
            .candidate_working_hash = 2U,
        };
    }
    db_vk_calibration_result_t result =
        db_vk_evaluate_calibration(pairs, DB_VK_CALIBRATION_PAIR_COUNT);
    DB_TEST_EXPECT_TRUE(state, result.complete);
    DB_TEST_EXPECT_TRUE(state, result.hashes_match);
    DB_TEST_EXPECT_TRUE(state, result.activate);

    pairs[3].candidate_working_hash = 3U;
    result = db_vk_evaluate_calibration(pairs, DB_VK_CALIBRATION_PAIR_COUNT);
    DB_TEST_EXPECT_TRUE(state, !result.hashes_match);
    DB_TEST_EXPECT_TRUE(state, !result.activate);

    pairs[3].candidate_working_hash = 2U;
    for (size_t index = 0U; index < DB_VK_CALIBRATION_PAIR_COUNT; index++) {
        pairs[index].candidate_ms = test_candidate_fast_ms;
    }
    pairs[DB_VK_CALIBRATION_PAIR_COUNT - 1U].candidate_ms =
        test_candidate_bad_p95_ms;
    pairs[DB_VK_CALIBRATION_PAIR_COUNT - 2U].candidate_ms =
        test_candidate_bad_p95_ms;
    result = db_vk_evaluate_calibration(pairs, DB_VK_CALIBRATION_PAIR_COUNT);
    DB_TEST_EXPECT_TRUE(state, !result.activate);
}

static void
db_test_vk_incomplete_calibration_stays_inactive(db_test_state_t *state) {
    const db_vk_calibration_pair_t pair = {
        .primary_ms = test_primary_ms,
        .candidate_ms = test_candidate_fast_ms,
        .primary_state_hash = 1U,
        .candidate_state_hash = 1U,
        .primary_working_hash = 2U,
        .candidate_working_hash = 2U,
    };
    const db_vk_calibration_result_t result =
        db_vk_evaluate_calibration(&pair, 1U);
    DB_TEST_EXPECT_TRUE(state, !result.complete);
    DB_TEST_EXPECT_TRUE(state, !result.activate);
}

static void db_test_vk_multi_gpu_phase_contract(db_test_state_t *state) {
    DB_TEST_EXPECT_TRUE(
        state, db_vk_multi_gpu_phase_transition_valid(DB_VK_MULTI_GPU_CLOSED,
                                                      DB_VK_MULTI_GPU_WARMING));
    DB_TEST_EXPECT_TRUE(
        state, db_vk_multi_gpu_phase_transition_valid(
                   DB_VK_MULTI_GPU_WARMING, DB_VK_MULTI_GPU_CALIBRATING));
    DB_TEST_EXPECT_TRUE(
        state, db_vk_multi_gpu_phase_transition_valid(
                   DB_VK_MULTI_GPU_CALIBRATING, DB_VK_MULTI_GPU_VALIDATED));
    DB_TEST_EXPECT_TRUE(
        state, db_vk_multi_gpu_phase_transition_valid(DB_VK_MULTI_GPU_VALIDATED,
                                                      DB_VK_MULTI_GPU_ACTIVE));
    DB_TEST_EXPECT_TRUE(
        state, !db_vk_multi_gpu_phase_transition_valid(DB_VK_MULTI_GPU_WARMING,
                                                       DB_VK_MULTI_GPU_ACTIVE));
    DB_TEST_EXPECT_TRUE(
        state, db_vk_multi_gpu_phase_transition_valid(DB_VK_MULTI_GPU_ACTIVE,
                                                      DB_VK_MULTI_GPU_CLOSED));
    DB_TEST_EXPECT_TRUE(
        state, strcmp(db_vk_multi_gpu_phase_name(DB_VK_MULTI_GPU_ACTIVE),
                      "active") == 0);
}

static void db_test_vk_import_memory_namespace(db_test_state_t *state) {
    const uint32_t worker_type_bits = 0x00000002U;
    const uint32_t exported_fd_type_bits = 0x00000011U;
    const uint32_t primary_alias_type_bits = 0x0000001bU;
    DB_TEST_EXPECT_EQ_U32(state,
                          db_vk_import_memory_type_bits(
                              exported_fd_type_bits, primary_alias_type_bits),
                          0x00000011U);
    DB_TEST_EXPECT_EQ_U32(state,
                          db_vk_import_memory_type_bits(
                              worker_type_bits, primary_alias_type_bits),
                          0x00000002U);
}

static void db_test_vk_shared_buffer_segments(db_test_state_t *state) {
    db_vk_present_piece_t pieces[3] = {
        {.destination_rect = {.extent = {.width = 3U, .height = 2U}},
         .piece_index = 0U},
        {.destination_rect = {.extent = {.width = 4U, .height = 2U}},
         .piece_index = 1U},
        {.destination_rect = {.extent = {.width = 3U, .height = 2U}},
         .piece_index = 2U},
    };
    const db_vk_execution_plan_t execution = {
        .scheduling_epoch = 7U,
        .content_generation = 8U,
        .pieces = pieces,
        .piece_count = 3U,
    };
    db_vk_shared_piece_layout_t layouts[3] = {0};
    db_vk_shared_buffer_plan_t plan = {0};
    DB_TEST_EXPECT_TRUE(
        state, db_vk_build_shared_buffer_plan(&execution, DB_PIXEL_FORMAT_RGBA8,
                                              256U, 64U, layouts, 3U, &plan));
    DB_TEST_EXPECT_EQ_U32(state, plan.segment_count, 2U);
    DB_TEST_EXPECT_EQ_U32(state, plan.layout_count, 3U);
    DB_TEST_EXPECT_EQ_U32(state, layouts[0].row_pitch, 12U);
    DB_TEST_EXPECT_EQ_U32(state, layouts[1].segment_index, 0U);
    DB_TEST_EXPECT_EQ_U32(state, layouts[1].byte_offset, 24U);
    DB_TEST_EXPECT_EQ_U32(state, layouts[2].segment_index, 1U);
    DB_TEST_EXPECT_EQ_U32(state, layouts[2].byte_offset, 0U);
    DB_TEST_EXPECT_TRUE(state,
                        (plan.total_size >= 256U) && (plan.total_size <= 304U));
}

static void db_test_vk_shared_buffer_piece_overflow(db_test_state_t *state) {
    db_vk_present_piece_t piece = {
        .destination_rect = {.extent = {.width = 64U, .height = 64U}},
    };
    const db_vk_execution_plan_t execution = {
        .pieces = &piece,
        .piece_count = 1U,
    };
    db_vk_shared_piece_layout_t layout = {0};
    db_vk_shared_buffer_plan_t plan = {0};
    DB_TEST_EXPECT_TRUE(state, db_vk_build_shared_buffer_plan(
                                   &execution, DB_PIXEL_FORMAT_RGBA16F, 64U,
                                   4096U, &layout, 1U, &plan));
    DB_TEST_EXPECT_EQ_U32(state, plan.layout_count, 0U);
    DB_TEST_EXPECT_EQ_U32(state, plan.rerouted_piece_count, 1U);

    piece.destination_rect.extent.width = UINT32_MAX;
    piece.destination_rect.extent.height = UINT32_MAX;
    plan = (db_vk_shared_buffer_plan_t){0};
    DB_TEST_EXPECT_TRUE(state, db_vk_build_shared_buffer_plan(
                                   &execution, DB_PIXEL_FORMAT_RGBA16F, 64U,
                                   UINT64_MAX, &layout, 1U, &plan));
    DB_TEST_EXPECT_EQ_U32(state, plan.layout_count, 0U);
    DB_TEST_EXPECT_EQ_U32(state, plan.rerouted_piece_count, 1U);
}

static void db_test_vk_bounded_calibration_activation(db_test_state_t *state) {
    db_vk_calibration_state_t calibration = {0};
    db_vk_calibration_state_open(&calibration);
    const db_vk_calibration_pair_t pair = {
        .primary_ms = test_primary_ms,
        .candidate_ms = test_candidate_fast_ms,
        .primary_state_hash = 1U,
        .candidate_state_hash = 1U,
        .primary_working_hash = 2U,
        .candidate_working_hash = 2U,
    };
    for (uint32_t index = 0U; index < DB_VK_CALIBRATION_WARMUP_COUNT; index++) {
        db_vk_calibration_state_record(&calibration, &pair);
    }
    DB_TEST_EXPECT_EQ_U32(state, calibration.phase,
                          DB_VK_MULTI_GPU_CALIBRATING);
    for (uint32_t index = 0U; index < DB_VK_CALIBRATION_PAIR_COUNT; index++) {
        db_vk_calibration_state_record(&calibration, &pair);
    }
    DB_TEST_EXPECT_EQ_U32(state, calibration.warmup_count,
                          DB_VK_CALIBRATION_WARMUP_COUNT);
    DB_TEST_EXPECT_EQ_U32(state, calibration.pair_count,
                          DB_VK_CALIBRATION_PAIR_COUNT);
    DB_TEST_EXPECT_EQ_U32(state, calibration.phase, DB_VK_MULTI_GPU_ACTIVE);
}

static void
db_test_vk_split_search_refines_and_breaks_ties(db_test_state_t *state) {
    db_vk_split_search_t search = {0};
    for (uint32_t iteration = 0U;
         (iteration < TEST_SPLIT_SEARCH_ITERATION_LIMIT) &&
         (search.complete == 0);
         iteration++) {
        const uint32_t share = db_vk_split_search_next_share(&search);
        uint64_t duration = TEST_SPLIT_DEFAULT_NS;
        if (share == TEST_SPLIT_COARSE_BEST_BPS) {
            duration = TEST_SPLIT_COARSE_BEST_NS;
        } else if ((share == TEST_SPLIT_REFINED_LOW_BPS) ||
                   (share == TEST_SPLIT_REFINED_HIGH_BPS)) {
            duration = TEST_SPLIT_REFINED_BEST_NS;
        }
        const db_vk_split_sample_t sample = {
            .host_critical_path_ns = duration,
            .uncertainty_ns = 1000U,
            .calibrated = 1,
            .valid = 1,
        };
        db_vk_split_search_record(&search, &sample);
    }
    DB_TEST_EXPECT_TRUE(state, search.complete);
    DB_TEST_EXPECT_EQ_U32(state, search.selected_share_bps, 3750U);
    DB_TEST_EXPECT_EQ_U32(state, db_vk_split_search_next_share(&search), 0U);
}

static void db_test_vk_timestamp_math(db_test_state_t *state) {
    DB_TEST_EXPECT_TRUE(state, db_vk_timestamp_delta(250U, 5U, 8U) == 11U);
    DB_TEST_EXPECT_TRUE(state, db_vk_timestamp_delta(10U, 20U, 64U) == 10U);
    DB_TEST_EXPECT_TRUE(state,
                        db_vk_timestamp_deviation_acceptable(50000U, 500000U));
    DB_TEST_EXPECT_TRUE(state,
                        !db_vk_timestamp_deviation_acceptable(60000U, 500000U));
    DB_TEST_EXPECT_TRUE(state,
                        db_vk_timestamp_deviation_acceptable(60000U, 2000000U));
}

static void db_test_vk_hdr_surface_selection_requires_complete_pair(
    db_test_state_t *state) {
    const VkSurfaceFormatKHR formats[] = {
        {.format = VK_FORMAT_B8G8R8A8_UNORM,
         .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        {.format = VK_FORMAT_A2B10G10R10_UNORM_PACK32,
         .colorSpace = VK_COLOR_SPACE_HDR10_ST2084_EXT},
    };
    const db_vk_surface_format_selection_t selected =
        db_vk_resolve_surface_format_for_output(formats, 2U,
                                                DB_OUTPUT_FORMAT_AUTO, 1);
    DB_TEST_EXPECT_EQ_INT(state, selected.hdr_enabled, 1);
    DB_TEST_EXPECT_EQ_INT(state, selected.surface_format.format,
                          VK_FORMAT_A2B10G10R10_UNORM_PACK32);
    DB_TEST_EXPECT_EQ_INT(state, selected.capability.native_format_supported,
                          1);
    DB_TEST_EXPECT_EQ_INT(state, selected.capability.colorspace_supported, 1);
    const db_vk_surface_format_selection_t no_metadata =
        db_vk_resolve_surface_format_for_output(formats, 2U,
                                                DB_OUTPUT_FORMAT_AUTO, 0);
    DB_TEST_EXPECT_EQ_INT(state, no_metadata.hdr_enabled, 0);
    DB_TEST_EXPECT_EQ_INT(state, no_metadata.surface_format.format,
                          VK_FORMAT_B8G8R8A8_UNORM);
    DB_TEST_EXPECT_STR_EQ(state, no_metadata.capability.unavailable_reason,
                          "vulkan_hdr_metadata_extension_unavailable");
    const VkSurfaceFormatKHR wrong_colorspace[] = {
        {.format = VK_FORMAT_A2B10G10R10_UNORM_PACK32,
         .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        formats[0],
    };
    const db_vk_surface_format_selection_t no_colorspace =
        db_vk_resolve_surface_format_for_output(wrong_colorspace, 2U,
                                                DB_OUTPUT_FORMAT_AUTO, 1);
    DB_TEST_EXPECT_EQ_INT(state, no_colorspace.hdr_enabled, 0);
    DB_TEST_EXPECT_EQ_INT(state,
                          no_colorspace.capability.native_format_supported, 1);
    DB_TEST_EXPECT_EQ_INT(state, no_colorspace.capability.colorspace_supported,
                          0);
    DB_TEST_EXPECT_STR_EQ(state, no_colorspace.capability.unavailable_reason,
                          "vulkan_hdr10_colorspace_unavailable");

    const db_vk_surface_format_selection_t explicit_hdr =
        db_vk_resolve_surface_format_for_output(formats, 2U,
                                                DB_OUTPUT_FORMAT_HDR, 1);
    DB_TEST_EXPECT_EQ_INT(state, explicit_hdr.hdr_enabled, 1);
}

unsigned db_vk_scheduler_test_run_all(void) {
    static const db_test_case_t cases[] = {
        {"vk_measured_benefit_gate", db_test_vk_measured_benefit_gate},
        {"vk_external_interop_classification",
         db_test_vk_external_interop_classification},
        {"vk_device_group_peer_read_classification",
         db_test_vk_device_group_peer_read_classification},
        {"vk_hdr_aware_primary_selection",
         db_test_vk_hdr_aware_primary_selection},
        {"vk_piece_plan_preserves_order",
         db_test_vk_piece_plan_preserves_order},
        {"vk_instance_bounds_use_top_left_origin",
         db_test_vk_instance_bounds_use_top_left_origin},
        {"vk_piece_plan_overflow_is_primary",
         db_test_vk_piece_plan_overflow_is_primary},
        {"vk_overlapping_pieces_are_primary",
         db_test_vk_overlapping_pieces_are_primary},
        {"vk_sync_fd_state_machine", db_test_vk_sync_fd_state_machine},
        {"vk_slot_generation_validation",
         db_test_vk_slot_generation_validation},
        {"vk_transport_negotiates_ownership",
         db_test_vk_transport_negotiates_ownership},
        {"vk_paired_calibration", db_test_vk_paired_calibration},
        {"vk_incomplete_calibration_stays_inactive",
         db_test_vk_incomplete_calibration_stays_inactive},
        {"vk_multi_gpu_phase_contract", db_test_vk_multi_gpu_phase_contract},
        {"vk_import_memory_namespace", db_test_vk_import_memory_namespace},
        {"vk_shared_buffer_segments", db_test_vk_shared_buffer_segments},
        {"vk_shared_buffer_piece_overflow",
         db_test_vk_shared_buffer_piece_overflow},
        {"vk_bounded_calibration_activation",
         db_test_vk_bounded_calibration_activation},
        {"vk_split_search_refines_and_breaks_ties",
         db_test_vk_split_search_refines_and_breaks_ties},
        {"vk_timestamp_math", db_test_vk_timestamp_math},
        {"vk_hdr_surface_selection_requires_complete_pair",
         db_test_vk_hdr_surface_selection_requires_complete_pair},
    };
    return db_test_run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}
