#include "gl1_internal.h"

#include "core/db_frame_plan.h"
#include "core/db_geometry.h"
#include "core/db_numeric.h"
#include "core/db_render_ir.h"
#include "core/db_render_ir_snapshot.h"
#include "core/db_render_types.h"
#include "core/db_replay_policy.h"

#include <stddef.h>
#include <stdint.h>

enum {
    GL1_REPLAY_COMMAND_CAPACITY = 4096U,
    GL1_REPLAY_RESOURCE_CAPACITY = 2U,
};

#define GL1_REPLAY_BYTE_BUDGET ((size_t)8U * (size_t)1024U * (size_t)1024U)

int db_gl1_replay_init(void) {
    db_replay_policy_init(&g_state.replay.policy,
                          g_state.diagnostics.gl1_replay_capacity);
    size_t entry_bytes = 0U;
    if ((db_render_ir_owned_store_required_bytes(
             GL1_REPLAY_COMMAND_CAPACITY, DB_CANONICAL_GEOMETRY_BLOCK_CAPACITY,
             GL1_REPLAY_RESOURCE_CAPACITY, DB_CANONICAL_GEOMETRY_BLOCK_CAPACITY,
             DB_CANONICAL_GEOMETRY_BLOCK_CAPACITY,
             DB_CANONICAL_GEOMETRY_BLOCK_CAPACITY, &entry_bytes) == 0) ||
        (entry_bytes >
         (GL1_REPLAY_BYTE_BUDGET / g_state.replay.policy.replay_capacity))) {
        g_state.replay.available = 0;
        return 1;
    }
    g_state.replay.allocation_bytes =
        entry_bytes * g_state.replay.policy.replay_capacity;
    for (size_t index = 0U; index < g_state.replay.policy.replay_capacity;
         index++) {
        if (db_render_ir_snapshot_init(&g_state.replay.entries[index].update,
                                       GL1_REPLAY_COMMAND_CAPACITY,
                                       DB_CANONICAL_GEOMETRY_BLOCK_CAPACITY,
                                       GL1_REPLAY_RESOURCE_CAPACITY,
                                       DB_CANONICAL_GEOMETRY_BLOCK_CAPACITY,
                                       DB_CANONICAL_GEOMETRY_BLOCK_CAPACITY,
                                       DB_CANONICAL_GEOMETRY_BLOCK_CAPACITY) ==
            0) {
            db_gl1_replay_shutdown();
            return 0;
        }
    }
    g_state.replay.available = 1;
    return 1;
}

void db_gl1_replay_shutdown(void) {
    for (size_t index = 0U; index < DB_REPLAY_CAPACITY_MAX; index++) {
        db_render_ir_snapshot_shutdown(&g_state.replay.entries[index].update);
    }
    g_state.replay = (gl1_replay_history_t){0};
}

void db_gl1_replay_reset(void) {
    for (size_t index = 0U; index < DB_REPLAY_CAPACITY_MAX; index++) {
        g_state.replay.entries[index].valid = 0;
    }
    g_state.replay.next_entry = 0U;
    db_replay_policy_reset(&g_state.replay.policy);
}

size_t db_gl1_replay_collect(const db_frame_plan_t *plan,
                             db_render_ir_view_t *views, size_t view_capacity,
                             int *use_rebuild) {
    if ((plan == NULL) || (views == NULL) || (use_rebuild == NULL)) {
        return 0U;
    }
    const uint32_t required = plan->presentation_replay_depth;
    int compatible =
        DB_BOOL((g_state.replay.available != 0) &&
                (plan->rebuild_required == 0) && (required <= view_capacity));
    const uint32_t capacity = g_state.replay.policy.replay_capacity;
    if ((required > plan->frame_index) || (required > capacity) ||
        (required > g_state.replay.policy.retained_count)) {
        compatible = 0;
    }
    if (compatible != 0) {
        for (uint32_t offset = 0U; offset < required; offset++) {
            const uint32_t entry_index =
                (g_state.replay.next_entry + capacity - required + offset) %
                capacity;
            const gl1_replay_entry_t *const entry =
                &g_state.replay.entries[entry_index];
            const uint32_t expected_frame =
                plan->frame_index - required + offset;
            if ((entry->valid == 0) || (entry->replay_boundary != 0) ||
                (entry->frame_index != expected_frame) ||
                (entry->target_generation !=
                 g_state.replay.target_generation) ||
                (entry->width != plan->pixel_width) ||
                (entry->height != plan->pixel_height) ||
                (entry->format !=
                 g_state.backing.format.surface_pixel_format)) {
                compatible = 0;
                break;
            }
            views[offset] = db_render_ir_snapshot_view(&entry->update);
        }
    }
    const db_replay_resolution_t resolution = db_replay_policy_resolve(
        &g_state.replay.policy, required + 1U, 1, compatible);
    *use_rebuild =
        DB_BOOL((plan->rebuild_required != 0) || (resolution.use_rebuild != 0));
    return (*use_rebuild != 0) ? 0U : resolution.history_stream_count;
}

int db_gl1_replay_commit(const db_frame_plan_t *plan, uint32_t width,
                         uint32_t height, db_pixel_format_t format,
                         int replay_boundary) {
    if ((plan == NULL) || (g_state.replay.available == 0)) {
        return 0;
    }
    const uint32_t capacity = g_state.replay.policy.replay_capacity;
    gl1_replay_entry_t *const entry =
        &g_state.replay.entries[g_state.replay.next_entry];
    if ((replay_boundary != 0) || (plan->external_bindings.count > 0U)) {
        db_gl1_replay_reset();
        return 1;
    }
    const db_render_ir_clone_status_t clone = db_render_ir_clone_replayable(
        &plan->update_ir, &plan->update_metadata, &entry->update);
    if (clone == DB_RENDER_IR_CLONE_NOT_REPLAYABLE) {
        db_gl1_replay_reset();
        return 1;
    }
    if (clone != DB_RENDER_IR_CLONE_OK) {
        db_gl1_replay_reset();
        return 0;
    }
    entry->frame_index = plan->frame_index;
    entry->target_generation = g_state.replay.target_generation;
    entry->width = width;
    entry->height = height;
    entry->format = format;
    entry->replay_boundary = 0;
    entry->valid = 1;
    g_state.replay.next_entry = (g_state.replay.next_entry + 1U) % capacity;
    db_replay_policy_commit(&g_state.replay.policy);
    return 1;
}
