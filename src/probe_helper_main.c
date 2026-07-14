#include "core/db_probe_protocol.h"

#include "core/db_byte_codec.h"
#include "core/db_conformance.h"
#include "core/db_conformance_cache.h"
#include "core/db_core.h"
#include "core/db_poll_policy.h"
#include "core/db_probe_process.h"
#include "core/db_process_environment.h"
#include "core/db_render_types.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

enum {
    DB_PROBE_CHILD_OUTPUT_BYTES = 262144U,
    DB_PROBE_EXECUTABLE_PATH_BYTES = 4096U,
    DB_PROBE_TEST_CHILD_FAILURE_EXIT = 9,
    DB_PROBE_TIMEOUT_TEST_SECONDS = 120U,
};

static int sibling_driverbench_path(char *output, size_t output_size) {
#ifdef __APPLE__
    if (output_size > UINT32_MAX) {
        return 0;
    }
    uint32_t length = (uint32_t)output_size;
    if (_NSGetExecutablePath(output, &length) != 0) {
        return 0;
    }
#else
    const ssize_t length = readlink("/proc/self/exe", output, output_size - 1U);
    if (length <= 0) {
        return 0;
    }
    output[(size_t)length] = '\0';
#endif
    char *const separator = strrchr(output, '/');
    if (separator == NULL) {
        return 0;
    }
    static const char executable_name[] = "driverbench";
    const size_t prefix = db_checked_ptrdiff_to_size(
        "probe_helper", "driverbench_path_prefix", separator + 1 - output);
    if (prefix > output_size ||
        sizeof(executable_name) > output_size - prefix) {
        return 0;
    }
    memcpy(separator + 1, executable_name, sizeof(executable_name));
    return 1;
}

static int spawn_capture(const char *executable, const char *const *arguments,
                         char *output, size_t output_capacity) {
    if ((executable == NULL) || (arguments == NULL) || (output == NULL) ||
        (output_capacity == 0U)) {
        return 0;
    }
    size_t argument_count = 0U;
    while (arguments[argument_count] != NULL) {
        argument_count++;
    }
    size_t argv_count = 0U;
    if (db_try_add_size(argument_count, 2U, &argv_count) == 0) {
        return 0;
    }
    void *const argv_storage = calloc(argv_count, sizeof(char *));
    if (argv_storage == NULL) {
        return 0;
    }
    char **const argv = (char **)argv_storage;
    argv[0] = strdup(executable);
    for (size_t index = 0U; index < argument_count; index++) {
        argv[index + 1U] = strdup(arguments[index]);
    }
    int valid = argv[0] != NULL;
    for (size_t index = 0U; index < argument_count; index++) {
        valid &= argv[index + 1U] != NULL;
    }
    int pipe_fds[2] = {-1, -1};
    if ((valid == 0) || (pipe(pipe_fds) != 0)) {
        valid = 0;
    }
    pid_t pid = -1;
    if (valid != 0) {
        posix_spawn_file_actions_t actions;
        int error = posix_spawn_file_actions_init(&actions);
        error |= posix_spawn_file_actions_adddup2(&actions, pipe_fds[1],
                                                  STDOUT_FILENO);
        error |= posix_spawn_file_actions_adddup2(&actions, pipe_fds[1],
                                                  STDERR_FILENO);
        error |= posix_spawn_file_actions_addclose(&actions, pipe_fds[0]);
        if (error == 0) {
            error = posix_spawn(&pid, executable, &actions, NULL, argv,
                                db_process_environment());
        }
        (void)posix_spawn_file_actions_destroy(&actions);
        valid = error == 0;
    }
    if (pipe_fds[1] >= 0) {
        (void)close(pipe_fds[1]);
    }
    size_t output_size = 0U;
    int status = 0;
    int done = 0;
    int pipe_eof = 0;
    if (valid != 0) {
        const db_poll_policy_t *const child_policy =
            db_progress_policy_get(DB_PROGRESS_CONFORMANCE_HELPER);
        const db_poll_policy_t *const reap_policy =
            db_progress_policy_get(DB_PROGRESS_CONFORMANCE_REAP);
        if ((child_policy == NULL) || (reap_policy == NULL)) {
            valid = 0;
        }
        const int flags = fcntl(pipe_fds[0], F_GETFL, 0);
        if (flags >= 0) {
            (void)fcntl(pipe_fds[0], F_SETFL, flags | O_NONBLOCK);
        }
        const db_deadline_t deadline = db_deadline_after(
            db_now_ns_monotonic(),
            (child_policy != NULL) ? child_policy->total_timeout_ns : 0U);
        while ((valid != 0) && ((done == 0) || (pipe_eof == 0)) &&
               (output_size + 1U < output_capacity)) {
            for (;;) {
                const ssize_t count = read(pipe_fds[0], output + output_size,
                                           output_capacity - output_size - 1U);
                if (count > 0) {
                    output_size += (size_t)count;
                    if (output_size + 1U >= output_capacity) {
                        valid = 0;
                        break;
                    }
                    continue;
                }
                if (count == 0) {
                    pipe_eof = 1;
                    break;
                }
                // NOLINTNEXTLINE(misc-include-cleaner) -- public <errno.h>.
                if (errno == EINTR) {
                    continue;
                }
                // NOLINTNEXTLINE(misc-include-cleaner) -- public <errno.h>.
                if (errno != EAGAIN) {
                    valid = 0;
                }
                break;
            }
            if (done == 0) {
                const pid_t waited = waitpid(pid, &status, WNOHANG);
                if (waited == pid) {
                    done = 1;
                } else if (waited < 0) {
                    valid = 0;
                }
            }
            if ((valid == 0) || ((done != 0) && (pipe_eof != 0))) {
                break;
            }
            if (db_deadline_expired(&deadline, db_now_ns_monotonic()) != 0) {
                valid = 0;
                break;
            }
            const int poll_result = db_probe_process_wait_output(
                pipe_fds[0], DB_PROBE_PROCESS_POLL_INTERVAL_MS);
            if (poll_result < 0) {
                // NOLINTNEXTLINE(misc-include-cleaner) -- public <errno.h>.
                if (errno == EINTR) {
                    continue;
                }
                valid = 0;
            }
        }
        if (done == 0) {
            db_probe_process_terminate_and_reap(
                pid,
                (reap_policy != NULL) ? reap_policy->total_timeout_ns : 0U);
        }
    }
    if (pipe_fds[0] >= 0) {
        (void)close(pipe_fds[0]);
    }
    output[output_size] = '\0';
    for (size_t index = 0U; index < argument_count + 1U; index++) {
        free(argv[index]);
    }
    free(argv_storage);
    return valid && done && WIFEXITED(status) && (WEXITSTATUS(status) == 0);
}

static int parse_aggregate_hash(const char *output, uint64_t *hash) {
    const char *cursor = output;
    const char *last = NULL;
    static const char field[] = "framebuffer_hash_aggregate=0x";
    while ((cursor = strstr(cursor, field)) != NULL) {
        last = cursor + strlen(field);
        cursor = last;
    }
    if ((last == NULL) || (hash == NULL)) {
        return 0;
    }
    char *end = NULL;
    const unsigned long long value = strtoull(last, &end, 16);
    if (end == last) {
        return 0;
    }
    *hash = (uint64_t)value;
    return 1;
}

static void
device_uuid_text(const uint8_t uuid[DB_CONFORMANCE_UUID_BYTES],
                 char output[(DB_CONFORMANCE_UUID_BYTES * 2U) + 1U]) {
    if (db_hex_encode_lower(uuid, DB_CONFORMANCE_UUID_BYTES, output,
                            (DB_CONFORMANCE_UUID_BYTES * 2U) + 1U) == 0) {
        output[0] = '\0';
    }
}

static db_probe_result_t execute_live_probe(const db_probe_request_t *request) {
    db_probe_result_t result = {
        .request_id = request->request_id,
        .identity_hash = request->identity_hash,
        .status = DB_PROBE_STATUS_UNAVAILABLE,
        .result = DB_CONFORMANCE_UNTESTED,
    };
    if ((request->backend != DB_PROBE_BACKEND_VULKAN) &&
        (request->implementation == DB_GRADIENT_IMPLEMENTATION_EXACT_LOOKUP)) {
        return result;
    }
    char executable[DB_PROBE_EXECUTABLE_PATH_BYTES] = {0};
    if (sibling_driverbench_path(executable, sizeof(executable)) == 0) {
        return result;
    }
    const char *const format =
        (request->working_format == DB_PIXEL_FORMAT_RGBA16F) ? "rgba16f"
                                                             : "rgba8";
    const char *const cpu_args[] = {
        "--api",
        "cpu",
        "--display",
        "offscreen",
        "--working-format",
        format,
        "--benchmark-mode",
        "gradient_fill",
        "--random-seed",
        "123456",
        "--bench-speed",
        "20",
        "--frame-limit",
        "32",
        "--hash",
        "pixel",
        NULL,
    };
    char *const reference_output = calloc(DB_PROBE_CHILD_OUTPUT_BYTES, 1U);
    char *const observed_output = calloc(DB_PROBE_CHILD_OUTPUT_BYTES, 1U);
    if ((reference_output == NULL) || (observed_output == NULL) ||
        (spawn_capture(executable, cpu_args, reference_output,
                       DB_PROBE_CHILD_OUTPUT_BYTES) == 0) ||
        (parse_aggregate_hash(reference_output, &result.expected_hash) == 0)) {
        free(reference_output);
        free(observed_output);
        return result;
    }
    (void)setenv("DRIVERBENCH_PROBE_CHILD", "1", 1);
    char uuid[(DB_CONFORMANCE_UUID_BYTES * 2U) + 1U];
    device_uuid_text(request->device_uuid, uuid);
    (void)setenv("DRIVERBENCH_PROBE_DEVICE_UUID", uuid, 1);
    const char *const implementation =
        db_gradient_implementation_name(request->implementation);
    (void)setenv("DRIVERBENCH_PROBE_GRADIENT_IMPLEMENTATION", implementation,
                 1);
    const char *const backend_gradient =
        (request->implementation == DB_GRADIENT_IMPLEMENTATION_SEMANTIC)
            ? "semantic"
            : "row-fill";
    const char *const vulkan_args[] = {
        "--api",
        "vulkan",
        "--display",
        "glfw_window",
        "--glfw-hidden-window",
        "1",
        "--working-format",
        format,
        "--benchmark-mode",
        "gradient_fill",
        "--random-seed",
        "123456",
        "--bench-speed",
        "20",
        "--frame-limit",
        "32",
        "--hash",
        "pixel",
        "--vk-gradient",
        backend_gradient,
        NULL,
    };
    const char *const gl3_args[] = {
        "--api",
        "opengl",
        "--renderer",
        "gl3_3",
        "--display",
        "offscreen",
        "--working-format",
        format,
        "--benchmark-mode",
        "gradient_fill",
        "--random-seed",
        "123456",
        "--bench-speed",
        "20",
        "--frame-limit",
        "32",
        "--hash",
        "pixel",
        "--gl3-gradient",
        backend_gradient,
        NULL,
    };
    const char *const gl1_gradient =
        (request->implementation == DB_GRADIENT_IMPLEMENTATION_SEMANTIC)
            ? "interpolated"
            : "row-fill";
    const char *const gl1_args[] = {
        "--api",
        "opengl",
        "--renderer",
        "gl1_5_gles1_1",
        "--display",
        "offscreen",
        "--working-format",
        format,
        "--benchmark-mode",
        "gradient_fill",
        "--random-seed",
        "123456",
        "--bench-speed",
        "20",
        "--frame-limit",
        "32",
        "--hash",
        "pixel",
        "--gl1-target",
        "persistent-fbo",
        "--gl1-gradient",
        gl1_gradient,
        NULL,
    };
    const char *const *gpu_args = vulkan_args;
    const char *expected_path = "gradient_path=vulkan_row_fill";
    if (request->implementation == DB_GRADIENT_IMPLEMENTATION_SEMANTIC) {
        expected_path = "gradient_path=vulkan_semantic_gradient";
    } else if (request->implementation ==
               DB_GRADIENT_IMPLEMENTATION_EXACT_LOOKUP) {
        expected_path = "gradient_path=vulkan_exact_lookup";
    }
    if (request->backend == DB_PROBE_BACKEND_GL3) {
        gpu_args = gl3_args;
        expected_path = "gradient_path=gl3_row_fill";
        if (request->implementation == DB_GRADIENT_IMPLEMENTATION_SEMANTIC) {
            expected_path = "gradient_path=gl3_semantic_gradient";
        }
    } else if (request->backend == DB_PROBE_BACKEND_GL1) {
        gpu_args = gl1_args;
        expected_path = "gradient_path=gl1_row_fill";
        if (request->implementation == DB_GRADIENT_IMPLEMENTATION_SEMANTIC) {
            expected_path = "gradient_path=gl1_interpolated_gradient";
        }
    }
    const int executed =
        spawn_capture(executable, gpu_args, observed_output,
                      DB_PROBE_CHILD_OUTPUT_BYTES) &&
        parse_aggregate_hash(observed_output, &result.observed_hash) &&
        (strstr(observed_output, expected_path) != NULL);
    free(reference_output);
    free(observed_output);
    if (executed != 0) {
        result.status = DB_PROBE_STATUS_OK;
        result.result = (result.expected_hash == result.observed_hash)
                            ? DB_CONFORMANCE_CONFORMING
                            : DB_CONFORMANCE_NONCONFORMING;
    }
    return result;
}

int main(void) {
    uint8_t request_wire[DB_PROBE_REQUEST_WIRE_BYTES] = {0};
    db_probe_request_t request = {0};
    if ((db_probe_process_read_complete(STDIN_FILENO, request_wire,
                                        sizeof(request_wire)) == 0) ||
        (db_probe_request_decode(request_wire, &request) == 0)) {
        return 2;
    }
    const char *const count_path =
        getenv("DRIVERBENCH_PROBE_HELPER_COUNT_FILE");
    if ((count_path != NULL) && (count_path[0] != '\0')) {
        const int count_fd =
            open(count_path, O_WRONLY | O_CREAT | O_APPEND, 0600);
        if (count_fd < 0) {
            return DB_PROBE_TEST_CHILD_FAILURE_EXIT;
        }
        const uint8_t invocation = 1U;
        const int count_written = db_probe_process_write_complete(
            count_fd, &invocation, sizeof(invocation));
        const int count_closed = close(count_fd);
        if ((count_written == 0) || (count_closed != 0)) {
            return DB_PROBE_TEST_CHILD_FAILURE_EXIT;
        }
    }
    const char *const mode = getenv("DRIVERBENCH_PROBE_HELPER_TEST_MODE");
    if ((mode != NULL) && (strcmp(mode, "crash") == 0)) {
        (void)raise(SIGABRT);
        return 3;
    }
    if ((mode != NULL) && (strcmp(mode, "timeout") == 0)) {
        (void)sleep(DB_PROBE_TIMEOUT_TEST_SECONDS);
    }
    if ((mode != NULL) && (strcmp(mode, "malformed") == 0)) {
        const uint8_t malformed[] = {'b', 'a', 'd'};
        return db_probe_process_write_complete(STDOUT_FILENO, malformed,
                                               sizeof(malformed))
                   ? 0
                   : 4;
    }
    db_probe_result_t result = {
        .request_id = request.request_id,
        .identity_hash = request.identity_hash,
        .status = DB_PROBE_STATUS_UNAVAILABLE,
        .result = DB_CONFORMANCE_UNTESTED,
    };
    if (mode == NULL) {
        result = execute_live_probe(&request);
    }
    if ((mode != NULL) && (strcmp(mode, "conforming") == 0)) {
        result.status = DB_PROBE_STATUS_OK;
        result.result = DB_CONFORMANCE_CONFORMING;
        result.expected_hash = request.identity_hash;
        result.observed_hash = request.identity_hash;
    } else if ((mode != NULL) && (strcmp(mode, "nonconforming") == 0)) {
        result.status = DB_PROBE_STATUS_OK;
        result.result = DB_CONFORMANCE_NONCONFORMING;
        result.expected_hash = request.identity_hash;
        result.observed_hash = request.identity_hash ^ UINT64_C(1);
    } else if ((mode != NULL) && (strcmp(mode, "topology_exact") == 0)) {
        result.status = DB_PROBE_STATUS_OK;
        result.result =
            (request.implementation == DB_GRADIENT_IMPLEMENTATION_SEMANTIC)
                ? DB_CONFORMANCE_NONCONFORMING
                : DB_CONFORMANCE_CONFORMING;
        result.expected_hash = request.identity_hash;
        result.observed_hash = (result.result == DB_CONFORMANCE_CONFORMING)
                                   ? request.identity_hash
                                   : request.identity_hash ^ UINT64_C(1);
    } else if ((mode != NULL) && (strcmp(mode, "identity") == 0)) {
        result.identity_hash++;
    }
    uint8_t result_wire[DB_PROBE_RESULT_WIRE_BYTES] = {0};
    int written = db_probe_result_encode(&result, result_wire);
    if ((written != 0) && (mode != NULL) &&
        (strcmp(mode, "malformed_checksum") == 0)) {
        result_wire[0] ^= UINT8_C(1);
    }
    if ((written != 0) && (mode != NULL) && (strcmp(mode, "fragmented") == 0)) {
        const size_t prefix_size = 17U;
        written = db_probe_process_write_complete(STDOUT_FILENO, result_wire,
                                                  prefix_size) &&
                  db_probe_process_write_complete(
                      STDOUT_FILENO, result_wire + prefix_size,
                      sizeof(result_wire) - prefix_size);
    } else {
        written = written &&
                  db_probe_process_write_complete(STDOUT_FILENO, result_wire,
                                                  sizeof(result_wire));
    }
    if ((written != 0) && (mode != NULL) &&
        (strcmp(mode, "postwrite_timeout") == 0)) {
        (void)sleep(DB_PROBE_TIMEOUT_TEST_SECONDS);
    }
    if ((written != 0) && (mode != NULL) &&
        (strcmp(mode, "child_failure") == 0)) {
        return DB_PROBE_TEST_CHILD_FAILURE_EXIT;
    }
    return (written != 0) ? 0 : 5;
}
