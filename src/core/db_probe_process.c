#include "db_probe_process.h"

#include "db_core.h"
#include "db_probe_protocol.h"
#include "db_process_environment.h"
#include "db_progress_policy.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int db_probe_process_wait_fd(int fd, short events, uint32_t timeout_ms) {
    if (timeout_ms > (uint32_t)INT_MAX) {
        return -1;
    }
    // clang attributes public poll declarations to libc internal headers.
    // NOLINTBEGIN(misc-include-cleaner)
    struct pollfd descriptor = {
        .fd = fd,
        .events = (short)(events | (short)POLLHUP),
    };
    const int result = poll(&descriptor, 1U, (int)timeout_ms);
    // NOLINTEND(misc-include-cleaner)
    return result;
}

static int db_probe_process_wait_output(int fd, uint32_t timeout_ms) {
    // NOLINTNEXTLINE(misc-include-cleaner) -- public <poll.h> ownership.
    return db_probe_process_wait_fd(fd, POLLIN, timeout_ms);
}

static uint32_t db_probe_timeout_ms(uint64_t timeout_ns) {
    static const uint64_t ns_per_ms = UINT64_C(1000000);
    uint64_t timeout_ms = timeout_ns / ns_per_ms;
    if ((timeout_ns % ns_per_ms) != 0U) {
        timeout_ms++;
    }
    if (timeout_ms > (uint64_t)INT_MAX) {
        timeout_ms = (uint64_t)INT_MAX;
    }
    return (uint32_t)timeout_ms;
}

static int db_probe_configure_spawn_group(posix_spawnattr_t *attributes) {
    int error = posix_spawnattr_setflags(attributes, POSIX_SPAWN_SETPGROUP);
    if (error == 0) {
        error = posix_spawnattr_setpgroup(attributes, 0);
    }
    return error;
}

static void db_probe_signal_process_group(pid_t pid, int signal_number) {
    if (kill(-pid, signal_number) != 0) {
        // A child that exited before group creation can still be reaped by PID.
        (void)kill(pid, signal_number);
    }
}

static int db_probe_process_transfer_complete(int fd, uint8_t *read_bytes,
                                              const uint8_t *write_bytes,
                                              size_t size, int writing,
                                              uint64_t timeout_ns) {
    const int original_flags = fcntl(fd, F_GETFL, 0);
    if ((original_flags < 0) ||
        (fcntl(fd, F_SETFL, original_flags | O_NONBLOCK) != 0)) {
        return 0;
    }
    db_progress_session_t session = {0};
    const uint64_t started_ns = db_now_ns_monotonic();
    const int session_started =
        (timeout_ns == 0U)
            ? db_progress_session_begin(
                  &session, DB_PROGRESS_CONFORMANCE_PIPE_IO, started_ns)
            : db_progress_session_begin_with_timeout(
                  &session, DB_PROGRESS_CONFORMANCE_PIPE_IO, started_ns,
                  timeout_ns);
    if (session_started == 0) {
        (void)fcntl(fd, F_SETFL, original_flags);
        return 0;
    }
    size_t transferred = 0U;
    int complete = 1;
    while (transferred < size) {
        const ssize_t count =
            (writing != 0)
                ? write(fd, write_bytes + transferred, size - transferred)
                : read(fd, read_bytes + transferred, size - transferred);
        if (count > 0) {
            transferred += (size_t)count;
            continue;
        }
        if (count == 0) {
            complete = 0;
            break;
        }
        // NOLINTNEXTLINE(misc-include-cleaner) -- public <errno.h> ownership.
        if ((errno != EINTR) && (errno != EAGAIN) && (errno != EWOULDBLOCK)) {
            complete = 0;
            break;
        }
        const uint64_t poll_timeout_ns =
            db_progress_session_next_timeout(&session, db_now_ns_monotonic());
        if (poll_timeout_ns == 0U) {
            complete = 0;
            break;
        }
        if (errno != EINTR) {
            // NOLINTNEXTLINE(misc-include-cleaner) -- public <poll.h>.
            const short events = (short)((writing != 0) ? POLLOUT : POLLIN);
            const int poll_result = db_probe_process_wait_fd(
                fd, events, db_probe_timeout_ms(poll_timeout_ns));
            if (poll_result < 0) {
                // NOLINTNEXTLINE(misc-include-cleaner) -- public <errno.h>.
                if (errno == EINTR) {
                    continue;
                }
                complete = 0;
                break;
            }
        }
        if (db_progress_session_record_retry(
                &session, db_now_ns_monotonic(), 0U,
                (writing != 0) ? "pipe_write_pending" : "pipe_read_pending") ==
            0) {
            complete = 0;
            break;
        }
    }
    if (fcntl(fd, F_SETFL, original_flags) != 0) {
        complete = 0;
    }
    return complete;
}

int db_probe_process_read_complete(int fd, uint8_t *bytes, size_t size) {
    if (size == 0U) {
        return 1;
    }
    return (bytes != NULL) ? db_probe_process_transfer_complete(fd, bytes, NULL,
                                                                size, 0, 0U)
                           : 0;
}

int db_probe_process_write_complete(int fd, const uint8_t *bytes, size_t size) {
    if (size == 0U) {
        return 1;
    }
    return (bytes != NULL) ? db_probe_process_transfer_complete(fd, NULL, bytes,
                                                                size, 1, 0U)
                           : 0;
}

static db_progress_outcome_t
db_probe_process_terminate_and_reap(pid_t pid, uint64_t grace_ns) {
    db_probe_signal_process_group(pid, SIGTERM);
    int status = 0;
    db_progress_outcome_t outcome = db_progress_outcome_make(
        DB_PROGRESS_TIMEOUT, 0U, 0U, 0U, "child_unreaped");
    for (uint32_t phase = 0U; phase < 2U; phase++) {
        const uint64_t start_ns = db_now_ns_monotonic();
        db_progress_session_t session = {0};
        const int session_started =
            (grace_ns == 0U)
                ? db_progress_session_begin(
                      &session, DB_PROGRESS_CONFORMANCE_REAP, start_ns)
                : db_progress_session_begin_with_timeout(
                      &session, DB_PROGRESS_CONFORMANCE_REAP, start_ns,
                      grace_ns);
        if (session_started == 0) {
            return db_progress_outcome_make(DB_PROGRESS_INVALID, 0U, 0U, 0U,
                                            "invalid_reap_policy");
        }
        for (;;) {
            const pid_t waited = waitpid(pid, &status, WNOHANG);
            // NOLINTNEXTLINE(misc-include-cleaner) -- public <errno.h>.
            if ((waited == pid) || ((waited < 0) && (errno == ECHILD))) {
                db_progress_session_complete(&session, DB_PROGRESS_COMPLETED,
                                             0U, "child_reaped");
                return db_progress_session_outcome(&session,
                                                   db_now_ns_monotonic());
            }
            if (waited < 0) {
                db_progress_session_complete(&session, DB_PROGRESS_FAILED,
                                             (uint32_t)errno, "waitpid_failed");
                return db_progress_session_outcome(&session,
                                                   db_now_ns_monotonic());
            }
            const uint64_t now_ns = db_now_ns_monotonic();
            const uint64_t timeout_ns =
                db_progress_session_next_timeout(&session, now_ns);
            if (timeout_ns == 0U) {
                break;
            }
            (void)db_probe_process_wait_output(-1,
                                               db_probe_timeout_ms(timeout_ns));
            if (db_progress_session_record_retry(&session,
                                                 db_now_ns_monotonic(), 0U,
                                                 "child_not_reaped") == 0) {
                break;
            }
        }
        outcome = db_progress_session_outcome(&session, db_now_ns_monotonic());
        if (phase == 0U) {
            db_probe_signal_process_group(pid, SIGKILL);
        }
    }
    db_progress_log_outcome("probe_process", "terminate_and_reap",
                            DB_PROGRESS_CONFORMANCE_REAP, &outcome);
    return outcome;
}

typedef struct {
    size_t received;
    int pipe_eof;
    int extra_output;
    int child_done;
    int child_status;
} db_probe_supervision_t;

static int drain_result_pipe(int fd, uint8_t *wire, size_t wire_size,
                             db_probe_supervision_t *state) {
    for (;;) {
        uint8_t extra = 0U;
        uint8_t *const destination =
            (state->received < wire_size) ? &wire[state->received] : &extra;
        const size_t remaining =
            (state->received < wire_size) ? wire_size - state->received : 1U;
        const ssize_t count = read(fd, destination, remaining);
        if (count > 0) {
            if (state->received < wire_size) {
                state->received += (size_t)count;
            } else {
                state->extra_output = 1;
            }
            continue;
        }
        if (count == 0) {
            state->pipe_eof = 1;
            return 1;
        }
        // NOLINTNEXTLINE(misc-include-cleaner) -- public <errno.h> ownership.
        if (errno == EINTR) {
            continue;
        }
        // NOLINTNEXTLINE(misc-include-cleaner) -- public <errno.h> ownership.
        return errno == EAGAIN;
    }
}

static int observe_child(pid_t pid, db_probe_supervision_t *state) {
    if (state->child_done != 0) {
        return 1;
    }
    const pid_t waited = waitpid(pid, &state->child_status, WNOHANG);
    if (waited == pid) {
        state->child_done = 1;
        return 1;
    }
    return waited == 0;
}

typedef enum {
    DB_PROBE_SUPERVISION_COMPLETE = 0,
    DB_PROBE_SUPERVISION_TIMEOUT,
    DB_PROBE_SUPERVISION_IO_ERROR,
} db_probe_supervision_result_t;

static db_probe_supervision_result_t db_probe_supervise_output(
    pid_t pid, int output_fd, uint8_t *output, size_t output_capacity,
    db_progress_policy_id_t policy_id, uint64_t timeout_ns,
    db_probe_supervision_t *supervision) {
    db_progress_session_t session = {0};
    const uint64_t start_ns = db_now_ns_monotonic();
    const int session_started =
        (timeout_ns == 0U)
            ? db_progress_session_begin(&session, policy_id, start_ns)
            : db_progress_session_begin_with_timeout(&session, policy_id,
                                                     start_ns, timeout_ns);
    if ((session_started == 0) || (supervision == NULL)) {
        return DB_PROBE_SUPERVISION_IO_ERROR;
    }
    while ((supervision->child_done == 0) || (supervision->pipe_eof == 0)) {
        if ((drain_result_pipe(output_fd, output, output_capacity,
                               supervision) == 0) ||
            (observe_child(pid, supervision) == 0)) {
            return DB_PROBE_SUPERVISION_IO_ERROR;
        }
        if ((supervision->child_done != 0) && (supervision->pipe_eof != 0)) {
            return DB_PROBE_SUPERVISION_COMPLETE;
        }
        const uint64_t poll_timeout_ns =
            db_progress_session_next_timeout(&session, db_now_ns_monotonic());
        if (poll_timeout_ns == 0U) {
            return DB_PROBE_SUPERVISION_TIMEOUT;
        }
        const int poll_result = db_probe_process_wait_output(
            output_fd, db_probe_timeout_ms(poll_timeout_ns));
        if (poll_result < 0) {
            // NOLINTNEXTLINE(misc-include-cleaner) -- public <errno.h>.
            if (errno == EINTR) {
                continue;
            }
            return DB_PROBE_SUPERVISION_IO_ERROR;
        }
        if (db_progress_session_record_retry(&session, db_now_ns_monotonic(),
                                             (uint32_t)poll_result,
                                             "child_pending") == 0) {
            return DB_PROBE_SUPERVISION_TIMEOUT;
        }
    }
    return DB_PROBE_SUPERVISION_COMPLETE;
}

db_probe_status_t db_probe_process_run_with_timeout(
    const char *helper_path, const db_probe_request_t *request,
    db_probe_result_t *result, uint64_t timeout_ns, uint64_t reap_grace_ns) {
    if ((helper_path == NULL) || (request == NULL) || (result == NULL) ||
        (timeout_ns == 0U)) {
        return DB_PROBE_STATUS_IO_ERROR;
    }
    db_progress_session_t helper_budget = {0};
    const uint64_t helper_started_ns = db_now_ns_monotonic();
    if (db_progress_session_begin_with_timeout(
            &helper_budget, DB_PROGRESS_CONFORMANCE_HELPER, helper_started_ns,
            timeout_ns) == 0) {
        return DB_PROBE_STATUS_IO_ERROR;
    }
    int request_pipe[2] = {-1, -1};
    int result_pipe[2] = {-1, -1};
    if ((pipe(request_pipe) != 0) || (pipe(result_pipe) != 0)) {
        if (request_pipe[0] >= 0) {
            (void)close(request_pipe[0]);
            (void)close(request_pipe[1]);
        }
        return DB_PROBE_STATUS_IO_ERROR;
    }
    posix_spawn_file_actions_t actions;
    int spawn_error = posix_spawn_file_actions_init(&actions);
    posix_spawnattr_t attributes;
    const int attributes_initialized = posix_spawnattr_init(&attributes) == 0;
    if (attributes_initialized != 0) {
        spawn_error |= db_probe_configure_spawn_group(&attributes);
    } else {
        // NOLINTNEXTLINE(misc-include-cleaner) -- public <errno.h>.
        spawn_error = EINVAL;
    }
    spawn_error |= posix_spawn_file_actions_adddup2(&actions, request_pipe[0],
                                                    STDIN_FILENO);
    spawn_error |= posix_spawn_file_actions_adddup2(&actions, result_pipe[1],
                                                    STDOUT_FILENO);
    spawn_error |= posix_spawn_file_actions_addclose(&actions, request_pipe[1]);
    spawn_error |= posix_spawn_file_actions_addclose(&actions, result_pipe[0]);
    char *const executable = strdup(helper_path);
    char *const argv[] = {executable, NULL};
    pid_t pid = -1;
    if ((spawn_error == 0) && (executable != NULL)) {
        spawn_error = posix_spawn(&pid, helper_path, &actions, &attributes,
                                  argv, db_process_environment());
    } else if (executable == NULL) {
        // NOLINTNEXTLINE(misc-include-cleaner) -- public <errno.h> ownership.
        spawn_error = ENOMEM;
    }
    free(executable);
    if (attributes_initialized != 0) {
        (void)posix_spawnattr_destroy(&attributes);
    }
    (void)posix_spawn_file_actions_destroy(&actions);
    (void)close(request_pipe[0]);
    (void)close(result_pipe[1]);
    if (spawn_error != 0) {
        (void)close(request_pipe[1]);
        (void)close(result_pipe[0]);
        return DB_PROBE_STATUS_IO_ERROR;
    }
    uint8_t request_wire[DB_PROBE_REQUEST_WIRE_BYTES] = {0};
    const uint64_t request_timeout_ns = db_deadline_remaining_ns(
        &helper_budget.deadline, db_now_ns_monotonic());
    if ((db_probe_request_encode(request, request_wire) == 0) ||
        (request_timeout_ns == 0U) ||
        (db_probe_process_transfer_complete(request_pipe[1], NULL, request_wire,
                                            sizeof(request_wire), 1,
                                            request_timeout_ns) == 0)) {
        (void)close(request_pipe[1]);
        (void)close(result_pipe[0]);
        (void)db_probe_process_terminate_and_reap(pid, reap_grace_ns);
        return DB_PROBE_STATUS_IO_ERROR;
    }
    (void)close(request_pipe[1]);
    const int flags = fcntl(result_pipe[0], F_GETFL, 0);
    if (flags >= 0) {
        (void)fcntl(result_pipe[0], F_SETFL, flags | O_NONBLOCK);
    }
    uint8_t result_wire[DB_PROBE_RESULT_WIRE_BYTES] = {0};
    db_probe_supervision_t supervision = {0};
    const uint64_t supervision_timeout_ns = db_deadline_remaining_ns(
        &helper_budget.deadline, db_now_ns_monotonic());
    const db_probe_supervision_result_t supervision_result =
        (supervision_timeout_ns == 0U)
            ? DB_PROBE_SUPERVISION_TIMEOUT
            : db_probe_supervise_output(pid, result_pipe[0], result_wire,
                                        sizeof(result_wire),
                                        DB_PROGRESS_CONFORMANCE_HELPER,
                                        supervision_timeout_ns, &supervision);
    (void)close(result_pipe[0]);
    if (supervision_result != DB_PROBE_SUPERVISION_COMPLETE) {
        (void)db_probe_process_terminate_and_reap(pid, reap_grace_ns);
        return (supervision_result == DB_PROBE_SUPERVISION_TIMEOUT)
                   ? DB_PROBE_STATUS_TIMEOUT
                   : DB_PROBE_STATUS_IO_ERROR;
    }
    if (!WIFEXITED(supervision.child_status)) {
        return DB_PROBE_STATUS_CRASHED;
    }
    if (WEXITSTATUS(supervision.child_status) != 0) {
        return DB_PROBE_STATUS_CHILD_FAILURE;
    }
    if ((supervision.received != sizeof(result_wire)) ||
        (supervision.extra_output != 0) ||
        (db_probe_result_decode(result_wire, result) == 0)) {
        return DB_PROBE_STATUS_MALFORMED;
    }
    if ((result->request_id != request->request_id) ||
        (result->identity_hash != request->identity_hash)) {
        return DB_PROBE_STATUS_IDENTITY_MISMATCH;
    }
    return result->status;
}

db_probe_status_t db_probe_process_run(const char *helper_path,
                                       const db_probe_request_t *request,
                                       db_probe_result_t *result) {
    db_progress_session_t helper = {0};
    db_progress_session_t reap = {0};
    const uint64_t now_ns = db_now_ns_monotonic();
    if ((db_progress_session_begin(&helper, DB_PROGRESS_CONFORMANCE_HELPER,
                                   now_ns) == 0) ||
        (db_progress_session_begin(&reap, DB_PROGRESS_CONFORMANCE_REAP,
                                   now_ns) == 0)) {
        return DB_PROBE_STATUS_IO_ERROR;
    }
    return db_probe_process_run_with_timeout(
        helper_path, request, result,
        db_deadline_remaining_ns(&helper.deadline, now_ns),
        db_deadline_remaining_ns(&reap.deadline, now_ns));
}

int db_probe_process_capture_output(const char *executable,
                                    char *const arguments[], char *output,
                                    size_t output_capacity) {
    if ((executable == NULL) || (arguments == NULL) || (output == NULL) ||
        (output_capacity < 2U)) {
        return 0;
    }
    db_progress_session_t capture_budget = {0};
    const uint64_t capture_started_ns = db_now_ns_monotonic();
    if (db_progress_session_begin(&capture_budget,
                                  DB_PROGRESS_CONFORMANCE_CHILD_CAPTURE,
                                  capture_started_ns) == 0) {
        return 0;
    }
    int pipe_fds[2] = {-1, -1};
    if (pipe(pipe_fds) != 0) {
        return 0;
    }
    posix_spawn_file_actions_t actions;
    int spawn_error = posix_spawn_file_actions_init(&actions);
    posix_spawnattr_t attributes;
    const int attributes_initialized = posix_spawnattr_init(&attributes) == 0;
    if (attributes_initialized != 0) {
        spawn_error |= db_probe_configure_spawn_group(&attributes);
    } else {
        spawn_error = EINVAL;
    }
    spawn_error |=
        posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDOUT_FILENO);
    spawn_error |=
        posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDERR_FILENO);
    spawn_error |= posix_spawn_file_actions_addclose(&actions, pipe_fds[0]);
    pid_t pid = -1;
    if (spawn_error == 0) {
        spawn_error = posix_spawn(&pid, executable, &actions, &attributes,
                                  arguments, db_process_environment());
    }
    if (attributes_initialized != 0) {
        (void)posix_spawnattr_destroy(&attributes);
    }
    (void)posix_spawn_file_actions_destroy(&actions);
    (void)close(pipe_fds[1]);
    if (spawn_error != 0) {
        (void)close(pipe_fds[0]);
        return 0;
    }
    const int flags = fcntl(pipe_fds[0], F_GETFL, 0);
    if (flags >= 0) {
        (void)fcntl(pipe_fds[0], F_SETFL, flags | O_NONBLOCK);
    }

    db_progress_session_t reap = {0};
    const uint64_t start_ns = db_now_ns_monotonic();
    if (db_progress_session_begin(&reap, DB_PROGRESS_CONFORMANCE_REAP,
                                  start_ns) == 0) {
        (void)close(pipe_fds[0]);
        (void)db_probe_process_terminate_and_reap(pid, 0U);
        return 0;
    }

    db_probe_supervision_t supervision = {0};
    const uint64_t capture_timeout_ns = db_deadline_remaining_ns(
        &capture_budget.deadline, db_now_ns_monotonic());
    const db_probe_supervision_result_t supervision_result =
        (capture_timeout_ns == 0U)
            ? DB_PROBE_SUPERVISION_TIMEOUT
            : db_probe_supervise_output(pid, pipe_fds[0], (uint8_t *)output,
                                        output_capacity - 1U,
                                        DB_PROGRESS_CONFORMANCE_CHILD_CAPTURE,
                                        capture_timeout_ns, &supervision);
    (void)close(pipe_fds[0]);
    output[supervision.received] = '\0';
    if (supervision.child_done == 0) {
        (void)db_probe_process_terminate_and_reap(
            pid,
            db_deadline_remaining_ns(&reap.deadline, db_now_ns_monotonic()));
    }
    return (supervision_result == DB_PROBE_SUPERVISION_COMPLETE) &&
           (supervision.child_done != 0) && (supervision.pipe_eof != 0) &&
           (supervision.extra_output == 0) &&
           WIFEXITED(supervision.child_status) &&
           (WEXITSTATUS(supervision.child_status) == 0);
}
