#include "db_render_ir_snapshot.h"
#include "db_core.h"
#include "db_render_ir.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void *allocate_array(size_t count, size_t element_size) {
    if ((count == 0U) || (element_size == 0U) ||
        (count > (SIZE_MAX / element_size))) {
        return NULL;
    }
    return malloc(count * element_size);
}

static int view_storage_is_valid(const db_render_ir_view_t *view) {
    return (view != NULL) &&
           ((view->command_size == 0U) || (view->commands != NULL)) &&
           ((view->fill_count == 0U) || (view->fills != NULL)) &&
           ((view->resource_count == 0U) || (view->resources != NULL)) &&
           ((view->region_count == 0U) || (view->regions != NULL)) &&
           ((view->band_count == 0U) || (view->bands != NULL)) &&
           ((view->span_count == 0U) || (view->spans != NULL));
}

int db_render_ir_owned_store_required_bytes(
    size_t command_capacity, size_t fill_capacity, size_t resource_capacity,
    size_t region_capacity, size_t band_capacity, size_t span_capacity,
    size_t *required_bytes) {
    if (required_bytes == NULL) {
        return 0;
    }
    const size_t counts[] = {command_capacity, fill_capacity, resource_capacity,
                             region_capacity,  band_capacity, span_capacity};
    const size_t sizes[] = {1U,
                            sizeof(db_render_ir_fill_t),
                            sizeof(db_render_ir_resource_t),
                            sizeof(db_render_ir_region_t),
                            sizeof(db_render_ir_band_t),
                            sizeof(db_render_ir_span_t)};
    size_t total = 0U;
    for (size_t index = 0U; index < sizeof(counts) / sizeof(counts[0]);
         index++) {
        size_t bytes = 0U;
        if ((db_try_mul_size(counts[index], sizes[index], &bytes) == 0) ||
            (db_try_add_size(total, bytes, &total) == 0)) {
            return 0;
        }
    }
    *required_bytes = total;
    return 1;
}

int db_render_ir_snapshot_init(db_render_ir_snapshot_t *snapshot,
                               size_t command_capacity, size_t fill_capacity,
                               size_t resource_capacity, size_t region_capacity,
                               size_t band_capacity, size_t span_capacity) {
    if ((snapshot == NULL) || (command_capacity == 0U) ||
        (fill_capacity == 0U) || (resource_capacity == 0U) ||
        (region_capacity == 0U) || (band_capacity == 0U) ||
        (span_capacity == 0U)) {
        return 0;
    }
    *snapshot = (db_render_ir_snapshot_t){0};
    db_render_ir_store_t *const store = &snapshot->store;
    store->commands = allocate_array(command_capacity, 1U);
    store->fills = allocate_array(fill_capacity, sizeof(*store->fills));
    store->resources =
        allocate_array(resource_capacity, sizeof(*store->resources));
    store->regions = allocate_array(region_capacity, sizeof(*store->regions));
    store->bands = allocate_array(band_capacity, sizeof(*store->bands));
    store->spans = allocate_array(span_capacity, sizeof(*store->spans));
    if ((store->commands == NULL) || (store->fills == NULL) ||
        (store->resources == NULL) || (store->regions == NULL) ||
        (store->bands == NULL) || (store->spans == NULL)) {
        db_render_ir_snapshot_shutdown(snapshot);
        return 0;
    }
    store->command_capacity = command_capacity;
    store->fill_capacity = fill_capacity;
    store->resource_capacity = resource_capacity;
    store->region_capacity = region_capacity;
    store->band_capacity = band_capacity;
    store->span_capacity = span_capacity;
    db_render_ir_store_reset(store);
    return 1;
}

void db_render_ir_snapshot_shutdown(db_render_ir_snapshot_t *snapshot) {
    if (snapshot == NULL) {
        return;
    }
    free(snapshot->store.commands);
    free(snapshot->store.fills);
    free(snapshot->store.resources);
    free(snapshot->store.regions);
    free(snapshot->store.bands);
    free(snapshot->store.spans);
    *snapshot = (db_render_ir_snapshot_t){0};
}

db_render_ir_status_t
db_render_ir_snapshot_capture(db_render_ir_snapshot_t *snapshot,
                              const db_render_ir_view_t *source) {
    if ((snapshot == NULL) || (view_storage_is_valid(source) == 0)) {
        return DB_RENDER_IR_INVALID;
    }
    db_render_ir_store_t *const destination = &snapshot->store;
    if ((source->command_size > destination->command_capacity) ||
        (source->fill_count > destination->fill_capacity) ||
        (source->resource_count > destination->resource_capacity) ||
        (source->region_count > destination->region_capacity) ||
        (source->band_count > destination->band_capacity) ||
        (source->span_count > destination->span_capacity)) {
        return DB_RENDER_IR_CAPACITY;
    }
    size_t fill_bytes = 0U;
    size_t resource_bytes = 0U;
    size_t region_bytes = 0U;
    size_t band_bytes = 0U;
    size_t span_bytes = 0U;
    if ((db_try_mul_size(source->fill_count, sizeof(*source->fills),
                         &fill_bytes) == 0) ||
        (db_try_mul_size(source->resource_count, sizeof(*source->resources),
                         &resource_bytes) == 0) ||
        (db_try_mul_size(source->region_count, sizeof(*source->regions),
                         &region_bytes) == 0) ||
        (db_try_mul_size(source->band_count, sizeof(*source->bands),
                         &band_bytes) == 0) ||
        (db_try_mul_size(source->span_count, sizeof(*source->spans),
                         &span_bytes) == 0)) {
        return DB_RENDER_IR_ARITHMETIC_OVERFLOW;
    }
    if (source->command_size > 0U) {
        memmove(destination->commands, source->commands, source->command_size);
    }
    if (source->fill_count > 0U) {
        memmove(destination->fills, source->fills, fill_bytes);
    }
    if (source->resource_count > 0U) {
        memmove(destination->resources, source->resources, resource_bytes);
    }
    if (source->region_count > 0U) {
        memmove(destination->regions, source->regions, region_bytes);
    }
    if (source->band_count > 0U) {
        memmove(destination->bands, source->bands, band_bytes);
    }
    if (source->span_count > 0U) {
        memmove(destination->spans, source->spans, span_bytes);
    }
    destination->command_size = source->command_size;
    destination->command_count = source->command_count;
    destination->fill_count = source->fill_count;
    destination->resource_count = source->resource_count;
    destination->region_count = source->region_count;
    destination->band_count = source->band_count;
    destination->span_count = source->span_count;
    destination->next_sequence = source->command_count;
    destination->status = DB_RENDER_IR_OK;
    return DB_RENDER_IR_OK;
}

db_render_ir_view_t
db_render_ir_snapshot_view(const db_render_ir_snapshot_t *snapshot) {
    return (snapshot == NULL) ? (db_render_ir_view_t){0}
                              : db_render_ir_store_view(&snapshot->store);
}

static int command_is_replayable(const db_render_ir_command_header_t *command) {
    if (command == NULL) {
        return 0;
    }
    switch ((db_render_ir_opcode_t)command->opcode) {
    case DB_RENDER_IR_OP_BEGIN_TARGET:
    case DB_RENDER_IR_OP_END_TARGET:
    case DB_RENDER_IR_OP_CLEAR:
    case DB_RENDER_IR_OP_FILL_RECTS:
    case DB_RENDER_IR_OP_FILL_LINEAR_GRADIENT:
        return 1;
    case DB_RENDER_IR_OP_UPLOAD_IMAGE:
    case DB_RENDER_IR_OP_INVALIDATE_RESOURCE:
        return 0;
    }
    return 0;
}

db_render_ir_clone_status_t
db_render_ir_clone_replayable(const db_render_ir_view_t *source,
                              const db_render_ir_metadata_t *metadata,
                              db_render_ir_owned_store_t *destination) {
    if ((source == NULL) || (metadata == NULL) || (destination == NULL) ||
        (view_storage_is_valid(source) == 0) ||
        (db_render_ir_validate(source) != DB_RENDER_IR_OK)) {
        return DB_RENDER_IR_CLONE_INVALID;
    }
    if (metadata->status != DB_RENDER_IR_OK) {
        return DB_RENDER_IR_CLONE_NOT_OPTIMIZED;
    }
    if (metadata->prior_content_required != 0) {
        return DB_RENDER_IR_CLONE_NOT_REPLAYABLE;
    }
    db_render_ir_iterator_t iterator = {0};
    db_render_ir_iterator_begin(&iterator, source);
    const db_render_ir_command_header_t *command = NULL;
    uint32_t command_count = 0U;
    while ((command = db_render_ir_iterator_next(&iterator)) != NULL) {
        if (command_is_replayable(command) == 0) {
            return DB_RENDER_IR_CLONE_NOT_REPLAYABLE;
        }
        command_count++;
    }
    if (command_count != source->command_count) {
        return DB_RENDER_IR_CLONE_INVALID;
    }
    const db_render_ir_status_t capture =
        db_render_ir_snapshot_capture(destination, source);
    switch (capture) {
    case DB_RENDER_IR_OK:
        return DB_RENDER_IR_CLONE_OK;
    case DB_RENDER_IR_CAPACITY:
        return DB_RENDER_IR_CLONE_CAPACITY;
    case DB_RENDER_IR_INVALID:
    case DB_RENDER_IR_ARITHMETIC_OVERFLOW:
        return DB_RENDER_IR_CLONE_INVALID;
    }
    return DB_RENDER_IR_CLONE_INVALID;
}

const char *db_render_ir_clone_status_name(db_render_ir_clone_status_t status) {
    switch (status) {
    case DB_RENDER_IR_CLONE_OK:
        return "ok";
    case DB_RENDER_IR_CLONE_INVALID:
        return "invalid";
    case DB_RENDER_IR_CLONE_CAPACITY:
        return "capacity";
    case DB_RENDER_IR_CLONE_NOT_OPTIMIZED:
        return "not_optimized";
    case DB_RENDER_IR_CLONE_NOT_REPLAYABLE:
        return "not_replayable";
    }
    return "unknown";
}
