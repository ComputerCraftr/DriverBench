#include "core/db_progress_policy.h"
#include "kms_internal.h"

#include <errno.h>
#include <stdint.h>
#include <sys/select.h>
#include <sys/time.h> // IWYU pragma: keep
#include <xf86drm.h>
#include <xf86drmMode.h>

#ifdef __has_include
#if __has_include(<drm/drm.h>)
#include <drm/drm.h>
#elif __has_include(<libdrm/drm.h>)
#include <libdrm/drm.h>
#else
#error "Missing libdrm drm.h"
#endif
#else
#include <drm/drm.h>
#endif

#define DB_KMS_NS_PER_SECOND 1000000000ULL
#define DB_KMS_NS_PER_MICROSECOND 1000ULL

typedef struct {
    int fd;
    int *waiting;
    drmEventContext *event_context;
} db_kms_page_flip_wait_t;

static db_progress_outcome_t
db_kms_page_flip_wait_attempt(void *user_data, uint64_t timeout_ns) {
    db_kms_page_flip_wait_t *const context =
        (db_kms_page_flip_wait_t *)user_data;
    // select(2) exposes these through <sys/time.h>, but include-cleaner maps
    // the public typedefs to libc implementation headers.
    // NOLINTNEXTLINE(misc-include-cleaner)
    struct timeval timeout = {
        // NOLINTNEXTLINE(misc-include-cleaner)
        .tv_sec = (time_t)(timeout_ns / DB_KMS_NS_PER_SECOND),
        // NOLINTNEXTLINE(misc-include-cleaner)
        .tv_usec = (suseconds_t)((timeout_ns % DB_KMS_NS_PER_SECOND) /
                                 DB_KMS_NS_PER_MICROSECOND),
    };
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(context->fd, &fds);
    const int select_result =
        select(context->fd + 1, &fds, NULL, NULL, &timeout);
    if (select_result < 0) {
        if (errno == EINTR) { // NOLINT(misc-include-cleaner)
            return db_progress_outcome_make(DB_PROGRESS_TIMEOUT, 0U, 0U,
                                            (uint32_t)errno, "interrupted");
        }
        return db_progress_outcome_make(DB_PROGRESS_FAILED, 0U, 0U,
                                        (uint32_t)errno, "select_failed");
    }
    if (select_result == 0) {
        return db_progress_outcome_make(DB_PROGRESS_TIMEOUT, 0U, 0U, 0U,
                                        "event_pending");
    }
    if (drmHandleEvent(context->fd, context->event_context) != 0) {
        return db_progress_outcome_make(DB_PROGRESS_FAILED, 0U, 0U,
                                        (uint32_t)errno, "drm_event_failed");
    }
    return db_progress_outcome_make(
        (*context->waiting == 0) ? DB_PROGRESS_COMPLETED : DB_PROGRESS_TIMEOUT,
        0U, 0U, 0U,
        (*context->waiting == 0) ? "page_flip_complete" : "event_pending");
}

void page_flip_handler(int fd, unsigned frame, unsigned sec, unsigned usec,
                       void *data) {
    (void)fd;
    (void)frame;
    (void)sec;
    (void)usec;
    int *waiting = (int *)data;
    *waiting = 0;
}

void db_kms_atomic_flip_to_fb(const struct kms_atomic *kms, uint32_t fb_id,
                              drmEventContext *event_context) {
    drmModeAtomicReq *request = drmModeAtomicAlloc();
    if (request == NULL) {
        runtime_failf("drmModeAtomicAlloc");
    }
    drmModeAtomicAddProperty(request, kms->plane_id, kms->plane_prop_fb_id,
                             fb_id);

    int waiting = 1;
    // libdrm has distro-dependent public providers that include-cleaner cannot
    // resolve through the conditional include above.
    // NOLINTNEXTLINE(misc-include-cleaner)
    const uint32_t flags = DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT;
    if (drmModeAtomicCommit(kms->fd, request, flags, &waiting) != 0) {
        runtime_errno_fail("drmModeAtomicCommit flip");
    }
    drmModeAtomicFree(request);
    db_kms_page_flip_wait_t context = {
        .fd = kms->fd,
        .waiting = &waiting,
        .event_context = event_context,
    };
    const db_progress_outcome_t result = db_progress_execute(
        DB_PROGRESS_KMS_PAGE_FLIP, db_kms_page_flip_wait_attempt, &context);
    db_progress_log_outcome("display_linux_kms_atomic", "page_flip",
                            DB_PROGRESS_KMS_PAGE_FLIP, &result);
    if (result.status != DB_PROGRESS_COMPLETED) {
        runtime_failf("timed out waiting for KMS page flip");
    }
}
