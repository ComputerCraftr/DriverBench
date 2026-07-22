#include "core/db_conformance.h"
#include "core/db_conformance_cache.h"
#include "core/db_conformance_service.h"
#include "core/db_core.h"
#include "core/db_probe_process.h"
#include "core/db_probe_protocol.h"
#include "core/db_qualification_contracts.h"
#include "core/db_render_types.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum {
    DB_TEST_TIMEOUT_NS = 50000000U,
    DB_TEST_REAP_NS = 25000000U,
    DB_TEST_COUNT_PATH_BYTES = 256U,
    DB_TEST_CACHE_UNAVAILABLE_FAILURE = 6,
    DB_TEST_CACHE_TRANSIENT_FAILURE = 7,
    DB_TEST_COUNT_PATH_FAILURE = 8,
    DB_TEST_BATCH_FAILURE = 9,
    DB_TEST_BATCH_DEDUP_FAILURE = 10,
    DB_TEST_BATCH_DEADLINE_FAILURE = 11,
    DB_TEST_TOPOLOGY_SERVICE_FAILURE = 12,
    DB_TEST_TOPOLOGY_BUILD_VERSION_OFFSET = 10U,
};
static const uint64_t db_test_protocol_timeout_ns = UINT64_C(500000000);
static const char db_test_missing_helper_path[] =
    "/driverbench-test/missing-probe-helper";
static const char db_test_mode_count_failure[] = "count_failure";
static const char db_test_mode_malformed_repeat[] = "malformed_repeat";
static const char db_test_mode_none[] = "none";
static const char db_test_mode_service[] = "service";

static int service_failure(int code, const char *stage,
                           const db_conformance_decision_t *decision) {
    if (decision == NULL) {
        (void)fprintf(stderr, "probe service failure: stage=%s code=%d\n",
                      stage, code);
    } else {
        (void)fprintf(
            stderr,
            "probe service failure: stage=%s code=%d outcome=%s result=%s "
            "source=%d probe_status=%s cache_status=%s reason=%s\n",
            stage, code, db_qualification_outcome_name(decision->outcome),
            db_conformance_result_name(decision->result), (int)decision->source,
            db_probe_status_name(decision->probe_status),
            db_conformance_cache_status_name(decision->cache_status),
            decision->reason);
    }
    return code;
}

static db_probe_status_t run_probe(const char *helper_path, const char *mode,
                                   const db_probe_request_t *request,
                                   db_probe_result_t *result) {
    if (strcmp(mode, db_test_mode_none) == 0) {
        (void)unsetenv(DB_PROBE_ENV_HELPER_TEST_MODE);
    } else {
        (void)setenv(DB_PROBE_ENV_HELPER_TEST_MODE, mode, 1);
    }
    const int expect_timeout =
        (strcmp(mode, DB_PROBE_TEST_MODE_TIMEOUT) == 0) ||
        (strcmp(mode, DB_PROBE_TEST_MODE_POSTWRITE_TIMEOUT) == 0);
    const uint64_t timeout_ns = (expect_timeout != 0)
                                    ? DB_TEST_TIMEOUT_NS
                                    : db_test_protocol_timeout_ns;
    return db_probe_process_run_with_timeout(helper_path, request, result,
                                             timeout_ns, DB_TEST_REAP_NS);
}

static int test_cache_service(const char *helper_path) {
    char directory[] = "/tmp/driverbench-probe-service-XXXXXX";
    if (mkdtemp(directory) == NULL) {
        return 1;
    }
    (void)setenv(DB_PROBE_ENV_CACHE_DIR, directory, 1);
    (void)setenv(DB_PROBE_ENV_HELPER_TEST_MODE, DB_PROBE_TEST_MODE_CONFORMING,
                 1);
    const db_conformance_key_t key = {
        .schema_version = 1U,
        .evaluator_version = 1U,
        .domain_version = 1U,
        .build_version = 1U,
        .backend = DB_PROBE_BACKEND_VULKAN,
        .implementation = DB_GRADIENT_IMPLEMENTATION_SEMANTIC,
        .working_format = DB_PIXEL_FORMAT_RGBA16F,
        .vendor_id = 1U,
        .device_id = 2U,
        .logical_width = 1000U,
        .logical_height = 600U,
        .gradient_window_rows = 32U,
        .provider = "test",
        .strategy = "semantic",
        .driver_name = "test_driver",
        .driver_info = "test_info",
    };
    db_conformance_query_t query = {.helper_path = helper_path};
    db_conformance_decision_t decision = db_conformance_qualify(&key, &query);
    if ((decision.result != DB_CONFORMANCE_CONFORMING) ||
        (decision.source != DB_QUALIFICATION_SOURCE_HELPER) ||
        (decision.outcome != DB_QUALIFICATION_OUTCOME_CONFORMING)) {
        return service_failure(2, "initial_helper", &decision);
    }
    query.helper_path = db_test_missing_helper_path;
    decision = db_conformance_qualify(&key, &query);
    if ((decision.result != DB_CONFORMANCE_CONFORMING) ||
        (decision.source != DB_QUALIFICATION_SOURCE_CACHE)) {
        return service_failure(3, "cache_hit", &decision);
    }
    query.helper_path = helper_path;
    query.rerun_probe = 1;
    (void)setenv(DB_PROBE_ENV_HELPER_TEST_MODE,
                 DB_PROBE_TEST_MODE_NONCONFORMING, 1);
    decision = db_conformance_qualify(&key, &query);
    if ((decision.result != DB_CONFORMANCE_NONCONFORMING) ||
        (decision.source != DB_QUALIFICATION_SOURCE_HELPER) ||
        (decision.outcome != DB_QUALIFICATION_OUTCOME_NONCONFORMING)) {
        return service_failure(4, "rerun_nonconforming", &decision);
    }
    query = (db_conformance_query_t){.helper_path = db_test_missing_helper_path,
                                     .diagnostic_forced = 1};
    decision = db_conformance_qualify(&key, &query);
    if ((decision.result != DB_CONFORMANCE_UNTESTED) ||
        (decision.source != DB_QUALIFICATION_SOURCE_DIAGNOSTIC) ||
        (decision.outcome != DB_QUALIFICATION_OUTCOME_INTERNAL_ERROR)) {
        return service_failure(5, "forced_diagnostic", &decision);
    }
    db_conformance_key_t transient_key = key;
    transient_key.build_version++;
    query = (db_conformance_query_t){.helper_path = helper_path};
    (void)setenv(DB_PROBE_ENV_HELPER_TEST_MODE, DB_PROBE_TEST_MODE_UNAVAILABLE,
                 1);
    decision = db_conformance_qualify(&transient_key, &query);
    if ((decision.outcome != DB_QUALIFICATION_OUTCOME_UNAVAILABLE) ||
        (decision.result != DB_CONFORMANCE_UNTESTED)) {
        return service_failure(DB_TEST_CACHE_UNAVAILABLE_FAILURE,
                               "transient_unavailable", &decision);
    }
    query.helper_path = db_test_missing_helper_path;
    decision = db_conformance_qualify(&transient_key, &query);
    if ((decision.source == DB_QUALIFICATION_SOURCE_CACHE) ||
        (decision.outcome != DB_QUALIFICATION_OUTCOME_INTERNAL_ERROR)) {
        return service_failure(DB_TEST_CACHE_TRANSIENT_FAILURE,
                               "transient_not_cached", &decision);
    }

    char count_path[DB_TEST_COUNT_PATH_BYTES] = {0};
    if (db_snprintf(count_path, sizeof(count_path), "%s/count", directory) <=
        0) {
        return service_failure(DB_TEST_COUNT_PATH_FAILURE, "count_path", NULL);
    }
    (void)setenv(DB_PROBE_ENV_HELPER_COUNT_FILE, count_path, 1);
    (void)setenv(DB_PROBE_ENV_HELPER_TEST_MODE, DB_PROBE_TEST_MODE_CONFORMING,
                 1);
    db_conformance_key_t duplicate_keys[2] = {key, key};
    duplicate_keys[0].build_version += 2U;
    duplicate_keys[1] = duplicate_keys[0];
    db_conformance_decision_t duplicate_decisions[2] = {};
    query =
        (db_conformance_query_t){.helper_path = helper_path, .ignore_cache = 1};
    if (db_conformance_qualify_batch(duplicate_keys, 2U, &query,
                                     UINT64_C(1000000000),
                                     duplicate_decisions) == 0) {
        return service_failure(DB_TEST_BATCH_FAILURE, "batch_call", NULL);
    }
    struct stat count_status = {0};
    if ((stat(count_path, &count_status) != 0) || (count_status.st_size != 1) ||
        (duplicate_decisions[1].outcome !=
         DB_QUALIFICATION_OUTCOME_CONFORMING)) {
        return service_failure(DB_TEST_BATCH_DEDUP_FAILURE, "batch_dedup",
                               &duplicate_decisions[1]);
    }
    (void)unsetenv(DB_PROBE_ENV_HELPER_COUNT_FILE);

    db_conformance_decision_t expired = {0};
    if ((db_conformance_qualify_batch(&key, 1U, &query, 1U, &expired) == 0) ||
        (expired.outcome != DB_QUALIFICATION_OUTCOME_UNAVAILABLE) ||
        (strcmp(expired.reason, "aggregate_deadline") != 0)) {
        return service_failure(DB_TEST_BATCH_DEADLINE_FAILURE,
                               "aggregate_deadline", &expired);
    }

    (void)setenv(DB_PROBE_ENV_HELPER_TEST_MODE, DB_PROBE_TEST_MODE_CONFORMING,
                 1);
    db_qualification_topology_request_t topology_request = {
        .lane_count = 2U,
        .lanes =
            {
                {.is_primary = 1},
                {.is_primary = 0},
            },
    };
    static const db_gradient_implementation_t implementations[] = {
        DB_GRADIENT_IMPLEMENTATION_SEMANTIC,
        DB_GRADIENT_IMPLEMENTATION_EXACT_LOOKUP,
        DB_GRADIENT_IMPLEMENTATION_ROW_INSTANCES,
    };
    for (size_t lane_index = 0U; lane_index < topology_request.lane_count;
         lane_index++) {
        for (size_t implementation_index = 0U;
             implementation_index < DB_CONFORMANCE_IMPLEMENTATIONS_PER_LANE;
             implementation_index++) {
            topology_request.keys[lane_index][implementation_index] = key;
            topology_request.keys[lane_index][implementation_index]
                .implementation = implementations[implementation_index];
            topology_request.keys[lane_index][implementation_index]
                .build_version +=
                DB_TEST_TOPOLOGY_BUILD_VERSION_OFFSET + (uint32_t)lane_index;
        }
    }
    db_qualification_service_workspace_t workspace = {0};
    db_qualification_topology_result_t topology_result = {0};
    if ((db_qualification_service_resolve_topology(
             &topology_request, &query, UINT64_C(1000000000), &workspace,
             &topology_result) == 0) ||
        (topology_result.topology.qualified == 0) ||
        (topology_result.topology.implementation !=
         DB_GRADIENT_IMPLEMENTATION_SEMANTIC) ||
        (topology_result.topology.retained_lane_mask != UINT32_C(3)) ||
        (topology_result.source != DB_QUALIFICATION_SOURCE_HELPER)) {
        return service_failure(DB_TEST_TOPOLOGY_SERVICE_FAILURE,
                               "topology_service", NULL);
    }
    return 0;
}

int main(int argc, char **argv) {
    if ((argc != 3) || (argv[1] == NULL) || (argv[2] == NULL)) {
        return 2;
    }
    if (strcmp(argv[2], db_test_mode_service) == 0) {
        return test_cache_service(argv[1]);
    }
    const db_probe_request_t request = {
        .request_id = UINT64_C(1234),
        .identity_hash = UINT64_C(0x123456789abcdef0),
        .backend = DB_PROBE_BACKEND_VULKAN,
        .implementation = DB_GRADIENT_IMPLEMENTATION_SEMANTIC,
        .working_format = DB_PIXEL_FORMAT_RGBA16F,
    };
    db_probe_result_t result = {0};
    if (strcmp(argv[2], db_test_mode_count_failure) == 0) {
        (void)setenv(DB_PROBE_ENV_HELPER_COUNT_FILE, "/", 1);
        const db_probe_status_t count_status = run_probe(
            argv[1], DB_PROBE_TEST_MODE_CONFORMING, &request, &result);
        (void)unsetenv(DB_PROBE_ENV_HELPER_COUNT_FILE);
        return count_status != DB_PROBE_STATUS_CHILD_FAILURE;
    }
    if (strcmp(argv[2], db_test_mode_malformed_repeat) == 0) {
        for (unsigned iteration = 0U; iteration < 64U; iteration++) {
            if (run_probe(argv[1], DB_PROBE_TEST_MODE_MALFORMED, &request,
                          &result) != DB_PROBE_STATUS_MALFORMED) {
                return 1;
            }
        }
        return 0;
    }
    const db_probe_status_t status =
        run_probe(argv[1], argv[2], &request, &result);
    if (strcmp(argv[2], DB_PROBE_TEST_MODE_CONFORMING) == 0) {
        return !((status == DB_PROBE_STATUS_OK) &&
                 (result.result == DB_CONFORMANCE_CONFORMING));
    }
    if (strcmp(argv[2], DB_PROBE_TEST_MODE_NONCONFORMING) == 0) {
        return !((status == DB_PROBE_STATUS_OK) &&
                 (result.result == DB_CONFORMANCE_NONCONFORMING));
    }
    if (strcmp(argv[2], DB_PROBE_TEST_MODE_CRASH) == 0) {
        return status != DB_PROBE_STATUS_CRASHED;
    }
    if (strcmp(argv[2], DB_PROBE_TEST_MODE_CHILD_FAILURE) == 0) {
        return status != DB_PROBE_STATUS_CHILD_FAILURE;
    }
    if ((strcmp(argv[2], DB_PROBE_TEST_MODE_TIMEOUT) == 0) ||
        (strcmp(argv[2], DB_PROBE_TEST_MODE_POSTWRITE_TIMEOUT) == 0)) {
        return status != DB_PROBE_STATUS_TIMEOUT;
    }
    if ((strcmp(argv[2], DB_PROBE_TEST_MODE_MALFORMED) == 0) ||
        (strcmp(argv[2], DB_PROBE_TEST_MODE_MALFORMED_CHECKSUM) == 0)) {
        return status != DB_PROBE_STATUS_MALFORMED;
    }
    if (strcmp(argv[2], DB_PROBE_TEST_MODE_IDENTITY) == 0) {
        return status != DB_PROBE_STATUS_IDENTITY_MISMATCH;
    }
    if (strcmp(argv[2], DB_PROBE_TEST_MODE_FRAGMENTED) == 0) {
        return status != DB_PROBE_STATUS_UNAVAILABLE;
    }
    return (status != DB_PROBE_STATUS_OK) &&
           (status != DB_PROBE_STATUS_UNAVAILABLE) &&
           (status != DB_PROBE_STATUS_TIMEOUT);
}
