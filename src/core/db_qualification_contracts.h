#ifndef DRIVERBENCH_CORE_DB_QUALIFICATION_CONTRACTS_H
#define DRIVERBENCH_CORE_DB_QUALIFICATION_CONTRACTS_H

#include "core/db_conformance_cache.h"
#include "db_conformance.h"
#include "db_probe_protocol.h"
#include "db_render_result.h"
#include "db_render_types.h"

#include <stddef.h>
#include <stdint.h>

enum {
    DB_QUALIFICATION_MAX_DESCRIPTORS = 24U,
    DB_QUALIFICATION_MAX_LANES = 8U,
    DB_QUALIFICATION_IDENTITY_TEXT_CAPACITY = 96U,
    DB_QUALIFICATION_REASON_CAPACITY = 48U,
};

typedef uint32_t db_lane_mask_t;

typedef enum {
    DB_QUALIFICATION_OUTCOME_CONFORMING = 0,
    DB_QUALIFICATION_OUTCOME_NONCONFORMING,
    DB_QUALIFICATION_OUTCOME_UNAVAILABLE,
    DB_QUALIFICATION_OUTCOME_INTERNAL_ERROR,
} db_qualification_outcome_t;

typedef struct {
    uint32_t vendor_id;
    uint32_t device_id;
    uint8_t uuid[DB_CONFORMANCE_UUID_BYTES];
} db_device_identity_t;

typedef struct {
    uint32_t driver_id;
    uint32_t api_version;
    char name[DB_QUALIFICATION_IDENTITY_TEXT_CAPACITY];
    char info[DB_QUALIFICATION_IDENTITY_TEXT_CAPACITY];
} db_driver_identity_t;

typedef struct {
    uint64_t value;
} db_float_control_signature_t;

typedef struct {
    uint64_t device_generation;
    uint64_t implementation_generation;
    uint64_t target_contract_generation;
} db_qualification_identity_generation_t;

typedef struct {
    db_probe_backend_t backend;
    db_render_target_strategy_t strategy;
    db_gradient_implementation_t implementation;
    uint32_t lane_index;
    int is_primary;
    db_device_identity_t device;
    db_driver_identity_t driver;
    db_pixel_format_t working_format;
    db_float_control_signature_t float_controls;
    uint64_t implementation_hash;
    uint64_t capability_hash;
    uint64_t palette_hash;
    uint32_t logical_width;
    uint32_t logical_height;
    char provider[DB_QUALIFICATION_IDENTITY_TEXT_CAPACITY];
    int compatibility_validated;
} db_renderer_probe_descriptor_t;

typedef struct {
    db_renderer_probe_descriptor_t
        descriptors[DB_QUALIFICATION_MAX_DESCRIPTORS];
    size_t count;
    db_qualification_identity_generation_t generation;
} db_renderer_qualification_descriptor_store_t;

typedef struct {
    uint64_t generation;
    uint64_t descriptor_identity;
    uint64_t candidate_id;
    db_qualification_outcome_t outcome;
    db_qualification_source_t source;
    db_conformance_cache_status_t cache_status;
    db_gradient_implementation_t implementation;
    db_lane_mask_t retained_lanes;
    uint32_t lane_count;
    db_render_target_strategy_t strategy;
    char reason[DB_QUALIFICATION_REASON_CAPACITY];
    int production_qualified;
    int diagnostic_forced;
} db_qualification_snapshot_t;

typedef enum {
    DB_RENDERER_PREPARE_OK = 0,
    DB_RENDERER_PREPARE_UNAVAILABLE,
    DB_RENDERER_PREPARE_STALE,
    DB_RENDERER_PREPARE_FAILED,
} db_renderer_prepare_status_t;

typedef enum {
    DB_RENDERER_COMMIT_OK = 0,
    DB_RENDERER_COMMIT_STALE,
    DB_RENDERER_COMMIT_FAILED,
} db_renderer_commit_status_t;

typedef struct {
    db_qualification_snapshot_t snapshot;
    uint64_t renderer_generation;
    void *backend_state;
    int prepared;
} db_renderer_selection_candidate_t;

typedef struct {
    uint64_t generation;
    db_gradient_implementation_t implementation;
    db_lane_mask_t retained_lanes;
    uint32_t lane_count;
    db_render_target_strategy_t strategy;
    db_qualification_source_t source;
    db_conformance_cache_status_t cache_status;
    char reason[DB_QUALIFICATION_REASON_CAPACITY];
    int production_qualified;
    int diagnostic_forced;
} db_renderer_applied_selection_t;

typedef struct {
    int (*describe)(void *renderer,
                    db_renderer_qualification_descriptor_store_t *output);
    db_renderer_prepare_status_t (*prepare_apply)(
        void *renderer, const db_qualification_snapshot_t *snapshot,
        db_renderer_selection_candidate_t *candidate);
    db_renderer_commit_status_t (*commit_apply)(
        void *renderer, db_renderer_selection_candidate_t *candidate,
        db_renderer_applied_selection_t *applied);
    void (*abort_apply)(void *renderer,
                        db_renderer_selection_candidate_t *candidate);
} db_renderer_qualification_ops_t;

int db_qualification_generation_equal(
    db_qualification_identity_generation_t left,
    db_qualification_identity_generation_t right);
uint64_t db_qualification_generation_hash(
    db_qualification_identity_generation_t generation);
int db_qualification_descriptor_validate(
    const db_renderer_probe_descriptor_t *descriptor);
int db_qualification_descriptor_store_append(
    db_renderer_qualification_descriptor_store_t *store,
    const db_renderer_probe_descriptor_t *descriptor);

#endif
