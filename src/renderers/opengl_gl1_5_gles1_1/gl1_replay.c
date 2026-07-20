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
    size_t entry_bytes = 0U;
    if (db_render_ir_owned_store_required_bytes(
            GL1_REPLAY_COMMAND_CAPACITY, DB_CANONICAL_GEOMETRY_BLOCK_CAPACITY,
            GL1_REPLAY_RESOURCE_CAPACITY, DB_CANONICAL_GEOMETRY_BLOCK_CAPACITY,
            DB_CANONICAL_GEOMETRY_BLOCK_CAPACITY,
            DB_CANONICAL_GEOMETRY_BLOCK_CAPACITY, &entry_bytes) == 0) {
        g_state.replay.available = 0;
        return 1;
    }
    uint32_t capacity =
        DB_MIN(g_state.diagnostics.gl1_replay_capacity, DB_REPLAY_CAPACITY_MAX);
    while ((capacity > 0U) &&
           (entry_bytes > (GL1_REPLAY_BYTE_BUDGET / (capacity + 1U)))) {
        capacity--;
    }
    if (capacity == 0U) {
        g_state.replay.available = 0;
        return 1;
    }
    db_replay_policy_init(&g_state.replay.policy, capacity);
    g_state.replay.pending_entry = capacity;
    g_state.replay.allocation_bytes =
        entry_bytes * (g_state.replay.policy.replay_capacity + 1U);
    for (size_t index = 0U; index <= g_state.replay.policy.replay_capacity;
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
    for (size_t index = 0U; index < (DB_REPLAY_CAPACITY_MAX + 1U); index++) {
        db_render_ir_snapshot_shutdown(&g_state.replay.entries[index].update);
    }
    g_state.replay = (gl1_replay_history_t){0};
}

void db_gl1_replay_reset(void) {
    for (size_t index = 0U; index < g_state.replay.policy.replay_capacity;
         index++) {
        g_state.replay.entries[index].valid = 0;
    }
    db_gl1_replay_discard_pending();
    g_state.replay.next_entry = 0U;
    g_state.replay.direct_window_lineage_valid = 0;
    db_replay_policy_reset(&g_state.replay.policy);
}

void db_gl1_replay_discard_pending(void) {
    if (g_state.replay.pending_entry > DB_REPLAY_CAPACITY_MAX) {
        return;
    }
    gl1_replay_entry_t *const pending =
        &g_state.replay.entries[g_state.replay.pending_entry];
    pending->frame_index = 0U;
    pending->target_generation = 0U;
    pending->width = 0U;
    pending->height = 0U;
    pending->format = DB_PIXEL_FORMAT_RGBA8;
    pending->replay_boundary = 0;
    pending->valid = 0;
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

int db_gl1_replay_prepare(const db_frame_plan_t *plan, uint32_t width,
                          uint32_t height, db_pixel_format_t format,
                          uint64_t target_generation, int replay_boundary) {
    if ((plan == NULL) || (g_state.replay.available == 0)) {
        return 0;
    }
    gl1_replay_entry_t *const entry =
        &g_state.replay.entries[g_state.replay.pending_entry];
    db_gl1_replay_discard_pending();
    if ((replay_boundary != 0) || (plan->external_bindings.count > 0U)) {
        entry->replay_boundary = 1;
        entry->valid = 1;
        g_state.replay.target_generation = target_generation;
        return 1;
    }
    const db_render_ir_clone_status_t clone = db_render_ir_clone_replayable(
        &plan->update_ir, &plan->update_metadata, &entry->update);
    if (clone == DB_RENDER_IR_CLONE_NOT_REPLAYABLE) {
        entry->replay_boundary = 1;
        entry->valid = 1;
        return 1;
    }
    if (clone != DB_RENDER_IR_CLONE_OK) {
        db_gl1_replay_discard_pending();
        return 0;
    }
    entry->frame_index = plan->frame_index;
    entry->target_generation = target_generation;
    entry->width = width;
    entry->height = height;
    entry->format = format;
    entry->replay_boundary = 0;
    entry->valid = 1;
    g_state.replay.target_generation = target_generation;
    return 1;
}

void db_gl1_replay_prepare_boundary(void) {
    if (g_state.replay.available == 0) {
        return;
    }
    if (g_state.replay.pending_entry >=
        (sizeof(g_state.replay.entries) / sizeof(g_state.replay.entries[0]))) {
        runtime_failf("invalid pending replay slot before presentation");
    }
    db_gl1_replay_discard_pending();
    gl1_replay_entry_t *const pending =
        &g_state.replay.entries[g_state.replay.pending_entry];
    pending->replay_boundary = 1;
    pending->valid = 1;
}

void db_gl1_replay_publish_pending(void) {
    if (g_state.replay.available == 0) {
        return;
    }
    const size_t entry_count =
        sizeof(g_state.replay.entries) / sizeof(g_state.replay.entries[0]);
    const uint32_t capacity = g_state.replay.policy.replay_capacity;
    if ((g_state.replay.pending_entry >= entry_count) || (capacity == 0U) ||
        (capacity > DB_REPLAY_CAPACITY_MAX) ||
        (g_state.replay.next_entry >= capacity)) {
        runtime_failf("invalid replay state after accepted presentation");
    }
    gl1_replay_entry_t *const pending =
        &g_state.replay.entries[g_state.replay.pending_entry];
    if (pending->valid == 0) {
        runtime_failf("missing replay candidate after accepted presentation");
    }
    if (pending->replay_boundary != 0) {
        db_gl1_replay_reset();
        return;
    }
    const uint32_t destination = g_state.replay.next_entry;
    const gl1_replay_entry_t evicted = g_state.replay.entries[destination];
    g_state.replay.entries[destination] = *pending;
    *pending = evicted;
    db_gl1_replay_discard_pending();
    g_state.replay.next_entry = (g_state.replay.next_entry + 1U) % capacity;
    db_replay_policy_commit(&g_state.replay.policy);
}
