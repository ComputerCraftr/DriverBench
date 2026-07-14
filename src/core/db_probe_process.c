#include "db_probe_process.h"

#include "db_core.h"
#include "db_poll_policy.h"
#include "db_probe_protocol.h"
#include "db_process_environment.h"

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

int db_probe_process_wait_output(int fd, uint32_t timeout_ms) {
    if (timeout_ms > (uint32_t)INT_MAX) {
        return -1;
    }
    // clang attributes public poll declarations to libc internal headers.
    // NOLINTBEGIN(misc-include-cleaner)
    struct pollfd descriptor = {.fd = fd, .events = POLLIN | POLLHUP};
    const int result = poll(&descriptor, 1U, (int)timeout_ms);
    // NOLINTEND(misc-include-cleaner)
    return result;
}

int db_probe_process_read_complete(int fd, uint8_t *bytes, size_t size) {
    size_t received = 0U;
    while (received < size) {
        const ssize_t count = read(fd, bytes + received, size - received);
        // NOLINTNEXTLINE(misc-include-cleaner) -- public <errno.h> ownership.
        if ((count < 0) && (errno == EINTR)) {
            continue;
        }
        if (count <= 0) {
            return 0;
        }
        received += (size_t)count;
    }
    return 1;
}

int db_probe_process_write_complete(int fd, const uint8_t *bytes, size_t size) {
    size_t written = 0U;
    while (written < size) {
        const ssize_t result = write(fd, bytes + written, size - written);
        // NOLINTNEXTLINE(misc-include-cleaner) -- public <errno.h> ownership.
        if ((result < 0) && (errno == EINTR)) {
            continue;
        }
        if (result <= 0) {
            return 0;
        }
        written += (size_t)result;
    }
    return 1;
}

void db_probe_process_terminate_and_reap(pid_t pid, uint64_t grace_ns) {
    (void)kill(pid, SIGTERM);
    db_deadline_t deadline = db_deadline_after(db_now_ns_monotonic(), grace_ns);
    int status = 0;
    int kill_sent = 0;
    while (waitpid(pid, &status, WNOHANG) == 0) {
        if (db_deadline_expired(&deadline, db_now_ns_monotonic()) != 0) {
            if (kill_sent != 0) {
                return;
            }
            (void)kill(pid, SIGKILL);
            deadline = db_deadline_after(db_now_ns_monotonic(), grace_ns);
            kill_sent = 1;
        }
        (void)db_probe_process_wait_output(-1,
                                           DB_PROBE_PROCESS_POLL_INTERVAL_MS);
    }
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

db_probe_status_t db_probe_process_run_with_timeout(
    const char *helper_path, const db_probe_request_t *request,
    db_probe_result_t *result, uint64_t timeout_ns, uint64_t reap_grace_ns) {
    if ((helper_path == NULL) || (request == NULL) || (result == NULL) ||
        (timeout_ns == 0U)) {
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
        spawn_error = posix_spawn(&pid, helper_path, &actions, NULL, argv,
                                  db_process_environment());
    } else if (executable == NULL) {
        // NOLINTNEXTLINE(misc-include-cleaner) -- public <errno.h> ownership.
        spawn_error = ENOMEM;
    }
    free(executable);
    (void)posix_spawn_file_actions_destroy(&actions);
    (void)close(request_pipe[0]);
    (void)close(result_pipe[1]);
    if (spawn_error != 0) {
        (void)close(request_pipe[1]);
        (void)close(result_pipe[0]);
        return DB_PROBE_STATUS_IO_ERROR;
    }
    uint8_t request_wire[DB_PROBE_REQUEST_WIRE_BYTES] = {0};
    if ((db_probe_request_encode(request, request_wire) == 0) ||
        (db_probe_process_write_complete(request_pipe[1], request_wire,
                                         sizeof(request_wire)) == 0)) {
        (void)close(request_pipe[1]);
        (void)close(result_pipe[0]);
        db_probe_process_terminate_and_reap(pid, reap_grace_ns);
        return DB_PROBE_STATUS_IO_ERROR;
    }
    (void)close(request_pipe[1]);
    const int flags = fcntl(result_pipe[0], F_GETFL, 0);
    if (flags >= 0) {
        (void)fcntl(result_pipe[0], F_SETFL, flags | O_NONBLOCK);
    }
    uint8_t result_wire[DB_PROBE_RESULT_WIRE_BYTES] = {0};
    db_probe_supervision_t supervision = {0};
    const db_deadline_t deadline =
        db_deadline_after(db_now_ns_monotonic(), timeout_ns);
    while ((supervision.child_done == 0) || (supervision.pipe_eof == 0)) {
        if ((drain_result_pipe(result_pipe[0], result_wire, sizeof(result_wire),
                               &supervision) == 0) ||
            (observe_child(pid, &supervision) == 0)) {
            (void)close(result_pipe[0]);
            db_probe_process_terminate_and_reap(pid, reap_grace_ns);
            return DB_PROBE_STATUS_IO_ERROR;
        }
        if ((supervision.child_done != 0) && (supervision.pipe_eof != 0)) {
            break;
        }
        const uint64_t now_ns = db_now_ns_monotonic();
        if (db_deadline_expired(&deadline, now_ns) != 0) {
            (void)close(result_pipe[0]);
            db_probe_process_terminate_and_reap(pid, reap_grace_ns);
            return DB_PROBE_STATUS_TIMEOUT;
        }
        const int poll_result = db_probe_process_wait_output(
            result_pipe[0], DB_PROBE_PROCESS_POLL_INTERVAL_MS);
        if (poll_result < 0) {
            // NOLINTNEXTLINE(misc-include-cleaner) -- public <errno.h>
            // ownership.
            if (errno == EINTR) {
                continue;
            }
            (void)close(result_pipe[0]);
            db_probe_process_terminate_and_reap(pid, reap_grace_ns);
            return DB_PROBE_STATUS_IO_ERROR;
        }
    }
    (void)close(result_pipe[0]);
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
    const db_poll_policy_t *const helper =
        db_progress_policy_get(DB_PROGRESS_CONFORMANCE_HELPER);
    const db_poll_policy_t *const reap =
        db_progress_policy_get(DB_PROGRESS_CONFORMANCE_REAP);
    if ((helper == NULL) || (reap == NULL)) {
        return DB_PROBE_STATUS_IO_ERROR;
    }
    return db_probe_process_run_with_timeout(helper_path, request, result,
                                             helper->total_timeout_ns,
                                             reap->total_timeout_ns);
}
