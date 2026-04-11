#ifndef DRIVERBENCH_GL_SHADOW_PRESENT_INTERNAL_H
#define DRIVERBENCH_GL_SHADOW_PRESENT_INTERNAL_H

#include "gl_common.h"

#include <stdint.h>

typedef struct {
    uint32_t slot_index;
    int fallback_to_single_source;
    int requires_blocking_reclaim;
    const char *reason;
} db_gl_shadow_present_slot_acquire_t;

typedef struct {
    uint32_t slot_index;
    int requires_blocking_reclaim;
} db_gl_shadow_present_full_upload_slot_choice_t;

uint32_t
db_gl_shadow_present_pixel_bytes(const db_gl_shadow_present_state_t *state);
void db_gl_shadow_present_delete_slot_sync(
    db_gl_shadow_present_upload_slot_t *slot);
void db_gl_shadow_present_wait_slot_sync(
    db_gl_shadow_present_upload_slot_t *slot);
int db_gl_shadow_present_slot_sync_pending_nonblocking(
    db_gl_shadow_present_upload_slot_t *slot);
uint32_t
db_gl_shadow_present_ring_busy_mask(db_gl_shadow_present_state_t *state);
void db_gl_shadow_present_refresh_effective_mode(
    db_gl_shadow_present_state_t *state, int invalidate_slots);
db_gl_shadow_present_upload_slot_t *
db_gl_shadow_present_slot_or_null(db_gl_shadow_present_state_t *state,
                                  uint32_t slot_index);
const db_gl_shadow_present_upload_slot_t *
db_gl_shadow_present_slot_const_or_null(
    const db_gl_shadow_present_state_t *state, uint32_t slot_index);
int db_gl_shadow_present_texture_slot_index(
    const db_gl_shadow_present_state_t *state, uint32_t *out_slot_index);
void db_gl_shadow_present_mark_texture_slot(db_gl_shadow_present_state_t *state,
                                            uint32_t slot_index);
int db_gl_shadow_present_prepare_unpack_upload_storage(
    db_gl_shadow_present_state_t *state, const char *backend,
    size_t required_bytes);
db_gl_upload_stream_t *
db_gl_shadow_present_acquire_unpack_stream(db_gl_shadow_present_state_t *state,
                                           const char *backend,
                                           size_t required_bytes);
void db_gl_shadow_present_upload_hdr_damage_blocks(
    db_gl_shadow_present_state_t *state, const char *backend,
    const db_pixel_surface_t *source, const db_damage_block_t *blocks,
    size_t block_count);
uint32_t db_gl_shadow_present_active_slot_count(
    db_gl_shadow_present_preserve_mode_t preserve_mode,
    uint32_t preserved_framebuffer_count);
uint32_t db_gl_shadow_present_required_previous_frames(
    const db_gl_shadow_present_state_t *state);
int db_gl_shadow_present_choose_full_upload_slot(
    const db_gl_shadow_present_state_t *state, int preserve_slot_contents,
    uint32_t slot_offset, uint32_t busy_mask,
    db_gl_shadow_present_full_upload_slot_choice_t *out);
uint32_t db_gl_shadow_present_next_write_slot_after_present(
    db_gl_shadow_present_preserve_mode_t preserve_mode,
    int preserve_slot_contents, uint32_t target_slot_index,
    uint32_t slot_count);
int db_gl_shadow_present_choose_ring_write_slot(
    const db_gl_shadow_present_state_t *state, uint32_t busy_mask,
    db_gl_shadow_present_slot_acquire_t *out);

#endif
