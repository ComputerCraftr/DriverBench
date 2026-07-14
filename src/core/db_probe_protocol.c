#include "db_probe_protocol.h"

#include "db_byte_codec.h"
#include "db_conformance.h"
#include "db_conformance_cache.h"
#include "db_hash.h"
#include "db_render_types.h"

#include <stdint.h>
#include <string.h>

enum {
    DB_PROBE_SCHEMA = 1U,
    DB_PROBE_MAGIC_BYTES = 4U,
    DB_PROBE_SCHEMA_OFFSET = 4U,
    DB_PROBE_REQUEST_ID_OFFSET = 8U,
    DB_PROBE_IDENTITY_OFFSET = 16U,
    DB_PROBE_REQUEST_BACKEND_OFFSET = 24U,
    DB_PROBE_REQUEST_IMPLEMENTATION_OFFSET = 28U,
    DB_PROBE_REQUEST_FORMAT_OFFSET = 32U,
    DB_PROBE_REQUEST_UUID_OFFSET = 36U,
    DB_PROBE_RESULT_EXPECTED_HASH_OFFSET = 24U,
    DB_PROBE_RESULT_OBSERVED_HASH_OFFSET = 32U,
    DB_PROBE_RESULT_STATUS_OFFSET = 40U,
    DB_PROBE_RESULT_CONFORMANCE_OFFSET = 44U,
    DB_PROBE_CHECKSUM_OFFSET = 56U,
};

#define DB_PROBE_PROTOCOL_DOMAIN UINT32_C(0x50524F42)

static uint64_t checksum(const uint8_t *wire) {
    return db_fnv1a64_tree(wire, DB_PROBE_CHECKSUM_OFFSET,
                           DB_PROBE_PROTOCOL_DOMAIN, DB_FNV1A64_OFFSET);
}

static int header_valid(const uint8_t *wire, const char magic[4]) {
    return (memcmp(wire, magic, DB_PROBE_MAGIC_BYTES) == 0) &&
           (db_load_u32_le(&wire[DB_PROBE_SCHEMA_OFFSET]) == DB_PROBE_SCHEMA) &&
           (db_load_u64_le(&wire[DB_PROBE_CHECKSUM_OFFSET]) == checksum(wire));
}

const char *db_probe_status_name(db_probe_status_t status) {
    switch (status) {
    case DB_PROBE_STATUS_OK:
        return "ok";
    case DB_PROBE_STATUS_UNAVAILABLE:
        return "unavailable";
    case DB_PROBE_STATUS_TIMEOUT:
        return "timeout";
    case DB_PROBE_STATUS_CRASHED:
        return "crashed";
    case DB_PROBE_STATUS_CHILD_FAILURE:
        return "child_failure";
    case DB_PROBE_STATUS_MALFORMED:
        return "malformed";
    case DB_PROBE_STATUS_IDENTITY_MISMATCH:
        return "identity_mismatch";
    case DB_PROBE_STATUS_IO_ERROR:
        return "io_error";
    }
    return "unknown";
}

int db_probe_request_encode(const db_probe_request_t *request,
                            uint8_t output[DB_PROBE_REQUEST_WIRE_BYTES]) {
    if ((request == NULL) || (output == NULL)) {
        return 0;
    }
    memset(output, 0, DB_PROBE_REQUEST_WIRE_BYTES);
    output[0] = 'D';
    output[1] = 'B';
    output[2] = 'P';
    output[3] = 'Q';
    db_store_u32_le(&output[DB_PROBE_SCHEMA_OFFSET], DB_PROBE_SCHEMA);
    db_store_u64_le(&output[DB_PROBE_REQUEST_ID_OFFSET], request->request_id);
    db_store_u64_le(&output[DB_PROBE_IDENTITY_OFFSET], request->identity_hash);
    db_store_u32_le(&output[DB_PROBE_REQUEST_BACKEND_OFFSET],
                    (uint32_t)request->backend);
    db_store_u32_le(&output[DB_PROBE_REQUEST_IMPLEMENTATION_OFFSET],
                    (uint32_t)request->implementation);
    db_store_u32_le(&output[DB_PROBE_REQUEST_FORMAT_OFFSET],
                    (uint32_t)request->working_format);
    memcpy(&output[DB_PROBE_REQUEST_UUID_OFFSET], request->device_uuid,
           DB_CONFORMANCE_UUID_BYTES);
    db_store_u64_le(&output[DB_PROBE_CHECKSUM_OFFSET], checksum(output));
    return 1;
}

int db_probe_request_decode(const uint8_t input[DB_PROBE_REQUEST_WIRE_BYTES],
                            db_probe_request_t *request) {
    if ((input == NULL) || (request == NULL) ||
        (header_valid(input, "DBPQ") == 0)) {
        return 0;
    }
    const uint32_t backend =
        db_load_u32_le(&input[DB_PROBE_REQUEST_BACKEND_OFFSET]);
    const uint32_t implementation =
        db_load_u32_le(&input[DB_PROBE_REQUEST_IMPLEMENTATION_OFFSET]);
    const uint32_t format =
        db_load_u32_le(&input[DB_PROBE_REQUEST_FORMAT_OFFSET]);
    if ((backend > DB_PROBE_BACKEND_VULKAN) ||
        (implementation > DB_GRADIENT_IMPLEMENTATION_SEMANTIC) ||
        (format > DB_PIXEL_FORMAT_RGBA16F)) {
        return 0;
    }
    *request = (db_probe_request_t){
        .request_id = db_load_u64_le(&input[DB_PROBE_REQUEST_ID_OFFSET]),
        .identity_hash = db_load_u64_le(&input[DB_PROBE_IDENTITY_OFFSET]),
        .backend = (db_probe_backend_t)backend,
        .implementation = (db_gradient_implementation_t)implementation,
        .working_format = (db_pixel_format_t)format,
    };
    memcpy(request->device_uuid, &input[DB_PROBE_REQUEST_UUID_OFFSET],
           DB_CONFORMANCE_UUID_BYTES);
    return 1;
}

int db_probe_result_encode(const db_probe_result_t *result,
                           uint8_t output[DB_PROBE_RESULT_WIRE_BYTES]) {
    if ((result == NULL) || (output == NULL)) {
        return 0;
    }
    memset(output, 0, DB_PROBE_RESULT_WIRE_BYTES);
    output[0] = 'D';
    output[1] = 'B';
    output[2] = 'P';
    output[3] = 'R';
    db_store_u32_le(&output[DB_PROBE_SCHEMA_OFFSET], DB_PROBE_SCHEMA);
    db_store_u64_le(&output[DB_PROBE_REQUEST_ID_OFFSET], result->request_id);
    db_store_u64_le(&output[DB_PROBE_IDENTITY_OFFSET], result->identity_hash);
    db_store_u64_le(&output[DB_PROBE_RESULT_EXPECTED_HASH_OFFSET],
                    result->expected_hash);
    db_store_u64_le(&output[DB_PROBE_RESULT_OBSERVED_HASH_OFFSET],
                    result->observed_hash);
    db_store_u32_le(&output[DB_PROBE_RESULT_STATUS_OFFSET],
                    (uint32_t)result->status);
    db_store_u32_le(&output[DB_PROBE_RESULT_CONFORMANCE_OFFSET],
                    (uint32_t)result->result);
    db_store_u64_le(&output[DB_PROBE_CHECKSUM_OFFSET], checksum(output));
    return 1;
}

int db_probe_result_decode(const uint8_t input[DB_PROBE_RESULT_WIRE_BYTES],
                           db_probe_result_t *result) {
    if ((input == NULL) || (result == NULL) ||
        (header_valid(input, "DBPR") == 0)) {
        return 0;
    }
    const uint32_t status =
        db_load_u32_le(&input[DB_PROBE_RESULT_STATUS_OFFSET]);
    const uint32_t conformance =
        db_load_u32_le(&input[DB_PROBE_RESULT_CONFORMANCE_OFFSET]);
    if ((status > DB_PROBE_STATUS_IO_ERROR) ||
        (conformance > DB_CONFORMANCE_NONCONFORMING)) {
        return 0;
    }
    *result = (db_probe_result_t){
        .request_id = db_load_u64_le(&input[DB_PROBE_REQUEST_ID_OFFSET]),
        .identity_hash = db_load_u64_le(&input[DB_PROBE_IDENTITY_OFFSET]),
        .expected_hash =
            db_load_u64_le(&input[DB_PROBE_RESULT_EXPECTED_HASH_OFFSET]),
        .observed_hash =
            db_load_u64_le(&input[DB_PROBE_RESULT_OBSERVED_HASH_OFFSET]),
        .status = (db_probe_status_t)status,
        .result = (db_conformance_result_t)conformance,
    };
    return 1;
}
