#ifndef DRIVERBENCH_CORE_DB_PROBE_PROCESS_H
#define DRIVERBENCH_CORE_DB_PROBE_PROCESS_H

#include "db_probe_protocol.h"

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

enum { DB_PROBE_PROCESS_POLL_INTERVAL_MS = 10 };

db_probe_status_t db_probe_process_run(const char *helper_path,
                                       const db_probe_request_t *request,
                                       db_probe_result_t *result);
db_probe_status_t db_probe_process_run_with_timeout(
    const char *helper_path, const db_probe_request_t *request,
    db_probe_result_t *result, uint64_t timeout_ns, uint64_t reap_grace_ns);
int db_probe_process_read_complete(int fd, uint8_t *bytes, size_t size);
int db_probe_process_write_complete(int fd, const uint8_t *bytes, size_t size);
int db_probe_process_wait_output(int fd, uint32_t timeout_ms);
void db_probe_process_terminate_and_reap(pid_t pid, uint64_t grace_ns);

#endif
