#include "vk_internal.h"

#include <stddef.h>
#include <stdint.h>

#include "../../core/db_core.h"
#include "../../core/db_frame_plan.h"
#include "../../core/db_geometry.h"
#include "../../core/db_numeric.h"
#include "core/db_render_types.h"
#include <vulkan/vulkan_core.h>

#define BACKEND_NAME "renderer_vulkan_1_2_multi_gpu"

enum { DB_VK_DEFAULT_WORKER_SHARE_BPS = 5000U };

static uint32_t vk_stable_row_owner(uint32_t row, uint32_t row_count,
                                    uint32_t lane_count, const double *costs) {
    if ((row_count == 0U) || (lane_count <= 1U)) {
        return 0U;
    }
    double total_throughput = 0.0;
    for (uint32_t lane = 0U; lane < lane_count; lane++) {
        const double cost = (costs != NULL) ? costs[lane] : 0.0;
        total_throughput += (cost > 0.0) ? (1.0 / cost) : 1.0;
    }
    double boundary = 0.0;
    for (uint32_t lane = 0U; lane < lane_count; lane++) {
        const double cost = (costs != NULL) ? costs[lane] : 0.0;
        const double throughput = (cost > 0.0) ? (1.0 / cost) : 1.0;
        boundary += ((double)row_count * throughput) / total_throughput;
        if ((double)row < boundary) {
            return lane;
        }
    }
    return lane_count - 1U;
}

static void vk_build_primary_fallback(const db_frame_plan_t *frame_plan,
                                      uint32_t scheduling_epoch,
                                      uint32_t content_generation,
                                      db_vk_present_piece_t *pieces,
                                      db_vk_lane_assignment_t *assignments,
                                      db_vk_execution_plan_t *out_plan,
                                      size_t geometry_count) {
    pieces[0] = (db_vk_present_piece_t){
        .source_rect = {.extent = {.width = frame_plan->pixel_width,
                                   .height = frame_plan->pixel_height}},
        .destination_rect = {.extent = {.width = frame_plan->pixel_width,
                                        .height = frame_plan->pixel_height}},
        .geometry_count =
            (geometry_count <= UINT32_MAX) ? (uint32_t)geometry_count : 0U,
        .compose_mode = DB_VK_COMPOSE_SAMPLE_NEAREST,
    };
    assignments[0] = (db_vk_lane_assignment_t){
        .piece_count = 1U,
        .slot = frame_plan->frame_index % DB_VK_LANE_SLOT_COUNT,
    };
    *out_plan = (db_vk_execution_plan_t){
        .scheduling_epoch = scheduling_epoch,
        .content_generation = content_generation,
        .pieces = pieces,
        .piece_count = 1U,
        .assignments = assignments,
        .assignment_count = 1U,
        .policy = DB_VK_SCHEDULING_PRIMARY_ONLY,
        .primary_only_fallback = 1,
    };
}

static int vk_piece_rects_overlap(const VkRect2D *left, const VkRect2D *right) {
    if ((left == NULL) || (right == NULL)) {
        return 0;
    }
    const int64_t left_end_x =
        (int64_t)left->offset.x + (int64_t)left->extent.width;
    const int64_t left_end_y =
        (int64_t)left->offset.y + (int64_t)left->extent.height;
    const int64_t right_end_x =
        (int64_t)right->offset.x + (int64_t)right->extent.width;
    const int64_t right_end_y =
        (int64_t)right->offset.y + (int64_t)right->extent.height;
    return DB_BOOL(((int64_t)left->offset.x < right_end_x) &&
                   ((int64_t)right->offset.x < left_end_x) &&
                   ((int64_t)left->offset.y < right_end_y) &&
                   ((int64_t)right->offset.y < left_end_y));
}

int db_vk_build_execution_plan_with_worker_share(
    const db_frame_plan_t *frame_plan, uint32_t lane_count,
    db_vk_scheduling_policy_t policy, uint32_t worker_share_bps,
    const double *ema_ms_per_work_unit, uint32_t scheduling_epoch,
    uint32_t content_generation, db_vk_present_piece_t *pieces,
    size_t piece_capacity, db_vk_lane_assignment_t *assignments,
    size_t assignment_capacity, db_vk_execution_plan_t *out_plan) {
    if ((frame_plan == NULL) || (pieces == NULL) || (assignments == NULL) ||
        (out_plan == NULL) || (piece_capacity == 0U) ||
        (assignment_capacity == 0U)) {
        return 0;
    }
    lane_count = DB_CLAMP(lane_count, 1U, DB_VK_MAX_LANES);
    const db_colored_f64_block_view_t geometry =
        (frame_plan->rebuild_seed.kind == DB_FRAME_REBUILD_SEED_GEOMETRY)
            ? frame_plan->rebuild_seed.geometry
            : frame_plan->geometry.current_blocks;
    if ((frame_plan->geometry.overflowed != 0) ||
        (geometry.count > DB_VK_MAX_PIECES_PER_FRAME) ||
        (geometry.count > piece_capacity) ||
        (geometry.count > assignment_capacity) ||
        (geometry.count > UINT32_MAX)) {
        vk_build_primary_fallback(frame_plan, scheduling_epoch,
                                  content_generation, pieces, assignments,
                                  out_plan, geometry.count);
        return 1;
    }

    uint32_t lane_work[DB_VK_MAX_LANES] = {0};
    size_t emitted = 0U;
    for (size_t index = 0U; index < geometry.count; index++) {
        const db_colored_f64_block_t *const block = &geometry.blocks[index];
        const db_grid_block_t grid_block = {
            .row_start = block->row_start,
            .row_count = block->row_count,
            .col_start = block->col_start,
            .col_count = block->col_count,
        };
        db_damage_block_t pixel_block = {0};
        if (db_grid_block_to_pixel_block(
                frame_plan->grid_cols, frame_plan->grid_rows, &grid_block,
                frame_plan->pixel_width, frame_plan->pixel_height,
                &pixel_block) == 0) {
            continue;
        }
        uint32_t lane = 0U;
        if ((policy == DB_VK_SCHEDULING_STABLE_ROWS) && (lane_count > 1U)) {
            const uint32_t midpoint =
                grid_block.row_start + (grid_block.row_count / 2U);
            if ((lane_count == 2U) && (worker_share_bps > 0U)) {
                const uint64_t primary_rows =
                    ((uint64_t)frame_plan->grid_rows *
                     (uint64_t)(10000U - DB_MIN(worker_share_bps, 10000U))) /
                    10000U;
                lane = DB_BOOL((uint64_t)midpoint >= primary_rows);
            } else {
                lane = vk_stable_row_owner(midpoint, frame_plan->grid_rows,
                                           lane_count, ema_ms_per_work_unit);
            }
        } else if (((policy == DB_VK_SCHEDULING_GREEDY_DAMAGE_CHUNKS) ||
                    (policy == DB_VK_SCHEDULING_THROUGHPUT_WEIGHTED_CHUNKS)) &&
                   (lane_count > 1U)) {
            lane = db_vk_select_owner_for_work(
                lane_count,
                db_grid_block_span_units_or_fail("piece_units", &grid_block),
                UINT64_MAX, 0U, ema_ms_per_work_unit, lane_work);
        }
        const uint32_t units =
            db_grid_block_span_units_or_fail("piece_units", &grid_block);
        lane_work[lane] = db_checked_add_u32(BACKEND_NAME, "lane_work",
                                             lane_work[lane], units);
        const int32_t pixel_x = db_checked_u32_to_i32(BACKEND_NAME, "piece_x",
                                                      pixel_block.col_start);
        const int32_t pixel_y = db_checked_u32_to_i32(BACKEND_NAME, "piece_y",
                                                      pixel_block.row_start);
        pieces[emitted] = (db_vk_present_piece_t){
            .source_rect = {.offset = {.x = pixel_x, .y = pixel_y},
                            .extent = {.width = pixel_block.col_count,
                                       .height = pixel_block.row_count}},
            .destination_rect = {.offset = {.x = pixel_x, .y = pixel_y},
                                 .extent = {.width = pixel_block.col_count,
                                            .height = pixel_block.row_count}},
            .geometry_first = db_checked_size_to_u32(
                BACKEND_NAME, "piece_geometry_first", index),
            .geometry_count = 1U,
            .piece_index =
                db_checked_size_to_u32(BACKEND_NAME, "piece_index", emitted),
            .compose_mode = DB_VK_COMPOSE_SAMPLE_NEAREST,
        };
        assignments[emitted] = (db_vk_lane_assignment_t){
            .piece_first = (uint32_t)emitted,
            .piece_count = 1U,
            .lane = lane,
            .slot = frame_plan->frame_index % DB_VK_LANE_SLOT_COUNT,
            .batch = 0U,
        };
        emitted++;
    }
    int overlapping = 0;
    for (size_t right = 1U; (right < emitted) && (overlapping == 0); right++) {
        for (size_t left = 0U; left < right; left++) {
            if (vk_piece_rects_overlap(&pieces[left].destination_rect,
                                       &pieces[right].destination_rect) != 0) {
                overlapping = 1;
                break;
            }
        }
    }
    if (overlapping != 0) {
        for (size_t index = 0U; index < emitted; index++) {
            assignments[index].lane = 0U;
        }
        policy = DB_VK_SCHEDULING_PRIMARY_ONLY;
    }
    *out_plan = (db_vk_execution_plan_t){
        .scheduling_epoch = scheduling_epoch,
        .content_generation = content_generation,
        .pieces = pieces,
        .piece_count = emitted,
        .assignments = assignments,
        .assignment_count = emitted,
        .policy = policy,
    };
    return 1;
}

int db_vk_build_execution_plan(
    const db_frame_plan_t *frame_plan, uint32_t lane_count,
    db_vk_scheduling_policy_t policy, const double *ema_ms_per_work_unit,
    uint32_t scheduling_epoch, uint32_t content_generation,
    db_vk_present_piece_t *pieces, size_t piece_capacity,
    db_vk_lane_assignment_t *assignments, size_t assignment_capacity,
    db_vk_execution_plan_t *out_plan) {
    return db_vk_build_execution_plan_with_worker_share(
        frame_plan, lane_count, policy, DB_VK_DEFAULT_WORKER_SHARE_BPS,
        ema_ms_per_work_unit, scheduling_epoch, content_generation, pieces,
        piece_capacity, assignments, assignment_capacity, out_plan);
}

int db_vk_sync_state_transition_valid(db_vk_sync_state_t from,
                                      db_vk_sync_state_t to) {
    switch (from) {
    case DB_VK_SYNC_IDLE:
    case DB_VK_SYNC_PAYLOAD_CONSUMED:
        return DB_BOOL(to == DB_VK_SYNC_SIGNAL_SUBMITTED);
    case DB_VK_SYNC_SIGNAL_SUBMITTED:
        return DB_BOOL(to == DB_VK_SYNC_FD_EXPORTED);
    case DB_VK_SYNC_FD_EXPORTED:
        return DB_BOOL(to == DB_VK_SYNC_FD_IMPORTED);
    case DB_VK_SYNC_FD_IMPORTED:
        return DB_BOOL(to == DB_VK_SYNC_WAIT_SUBMITTED);
    case DB_VK_SYNC_WAIT_SUBMITTED:
        return DB_BOOL(to == DB_VK_SYNC_PAYLOAD_CONSUMED);
    }
    return 0;
}

int db_vk_slot_result_is_current(const db_vk_lane_slot_t *slot,
                                 const db_vk_execution_plan_t *plan,
                                 uint64_t frame_index) {
    return DB_BOOL((slot != NULL) && (plan != NULL) &&
                   (slot->quarantined == 0) &&
                   (slot->content_generation == plan->content_generation) &&
                   (slot->scheduling_epoch == plan->scheduling_epoch) &&
                   (slot->last_applied_frame == frame_index) &&
                   (slot->valid_piece_count > 0U));
}
