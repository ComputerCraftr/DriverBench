#ifndef DRIVERBENCH_CORE_DB_PROBE_PROTOCOL_H
#define DRIVERBENCH_CORE_DB_PROBE_PROTOCOL_H

#include "core/db_conformance_cache.h"
#include "db_conformance.h"
#include "db_render_types.h"

#include <stddef.h>
#include <stdint.h>

#define DB_PROBE_REQUEST_WIRE_BYTES 64U
#define DB_PROBE_RESULT_WIRE_BYTES 64U

#define DB_PROBE_ENV_CACHE_DIR "DRIVERBENCH_PROBE_CACHE_DIR"
#define DB_PROBE_ENV_CHILD "DRIVERBENCH_PROBE_CHILD"
#define DB_PROBE_ENV_DEVICE_UUID "DRIVERBENCH_PROBE_DEVICE_UUID"
#define DB_PROBE_ENV_GRADIENT_IMPLEMENTATION                                   \
    "DRIVERBENCH_PROBE_GRADIENT_IMPLEMENTATION"
#define DB_PROBE_ENV_HELPER_COUNT_FILE "DRIVERBENCH_PROBE_HELPER_COUNT_FILE"
#define DB_PROBE_ENV_HELPER_TEST_MODE "DRIVERBENCH_PROBE_HELPER_TEST_MODE"

#define DB_PROBE_TEST_MODE_CHILD_FAILURE "child_failure"
#define DB_PROBE_TEST_MODE_CONFORMING "conforming"
#define DB_PROBE_TEST_MODE_CRASH "crash"
#define DB_PROBE_TEST_MODE_FRAGMENTED "fragmented"
#define DB_PROBE_TEST_MODE_IDENTITY "identity"
#define DB_PROBE_TEST_MODE_MALFORMED "malformed"
#define DB_PROBE_TEST_MODE_MALFORMED_CHECKSUM "malformed_checksum"
#define DB_PROBE_TEST_MODE_NONCONFORMING "nonconforming"
#define DB_PROBE_TEST_MODE_POSTWRITE_TIMEOUT "postwrite_timeout"
#define DB_PROBE_TEST_MODE_TIMEOUT "timeout"
#define DB_PROBE_TEST_MODE_TOPOLOGY_EXACT "topology_exact"
#define DB_PROBE_TEST_MODE_UNAVAILABLE "unavailable"

typedef enum {
    DB_PROBE_BACKEND_GL1 = 0,
    DB_PROBE_BACKEND_GL3,
    DB_PROBE_BACKEND_VULKAN,
} db_probe_backend_t;

typedef enum {
    DB_PROBE_STATUS_OK = 0,
    DB_PROBE_STATUS_UNAVAILABLE,
    DB_PROBE_STATUS_TIMEOUT,
    DB_PROBE_STATUS_CRASHED,
    DB_PROBE_STATUS_CHILD_FAILURE,
    DB_PROBE_STATUS_MALFORMED,
    DB_PROBE_STATUS_IDENTITY_MISMATCH,
    DB_PROBE_STATUS_IO_ERROR,
} db_probe_status_t;

typedef struct {
    uint64_t request_id;
    uint64_t identity_hash;
    db_probe_backend_t backend;
    db_gradient_implementation_t implementation;
    db_pixel_format_t working_format;
    uint8_t device_uuid[DB_CONFORMANCE_UUID_BYTES];
} db_probe_request_t;

typedef struct {
    uint64_t request_id;
    uint64_t identity_hash;
    uint64_t expected_hash;
    uint64_t observed_hash;
    db_probe_status_t status;
    db_conformance_result_t result;
} db_probe_result_t;

const char *db_probe_status_name(db_probe_status_t status);
int db_probe_request_encode(const db_probe_request_t *request,
                            uint8_t output[DB_PROBE_REQUEST_WIRE_BYTES]);
int db_probe_request_decode(const uint8_t input[DB_PROBE_REQUEST_WIRE_BYTES],
                            db_probe_request_t *request);
int db_probe_result_encode(const db_probe_result_t *result,
                           uint8_t output[DB_PROBE_RESULT_WIRE_BYTES]);
int db_probe_result_decode(const uint8_t input[DB_PROBE_RESULT_WIRE_BYTES],
                           db_probe_result_t *result);

#endif
