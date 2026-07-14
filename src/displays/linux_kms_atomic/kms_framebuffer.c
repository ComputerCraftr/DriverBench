#include "kms_internal.h"

#include <gbm.h>
#include <stdint.h>
#include <stdlib.h>
#include <xf86drmMode.h>

struct fb *fb_from_bo(int fd, struct gbm_bo *bo, int is_surface_buffer) {
    struct fb *fb = (struct fb *)calloc(1, sizeof(*fb));
    if (fb == NULL) {
        runtime_failf("calloc fb");
    }
    fb->bo = bo;
    fb->is_surface_buffer = is_surface_buffer;

    const uint32_t width_px = gbm_bo_get_width(bo);
    const uint32_t height_px = gbm_bo_get_height(bo);
    const uint32_t stride = gbm_bo_get_stride(bo);
    const uint32_t handle = gbm_bo_get_handle(bo).u32;
    const uint32_t format = gbm_bo_get_format(bo);
    uint32_t handles[4] = {handle, 0, 0, 0};
    uint32_t pitches[4] = {stride, 0, 0, 0};
    uint32_t offsets[4] = {0, 0, 0, 0};

    if (drmModeAddFB2(fd, width_px, height_px, format, handles, pitches,
                      offsets, &fb->fb_id, 0) != 0) {
        runtime_errno_fail("drmModeAddFB2");
    }
    return fb;
}

void fb_release(int fd, struct gbm_surface *gbm_surf, struct fb *fb) {
    if (fb == NULL) {
        return;
    }
    if (fb->fb_id != 0U) {
        drmModeRmFB(fd, fb->fb_id);
    }
    if (fb->bo != NULL) {
        if ((fb->is_surface_buffer != 0) && (gbm_surf != NULL)) {
            gbm_surface_release_buffer(gbm_surf, fb->bo);
        } else if (fb->is_surface_buffer == 0) {
            gbm_bo_destroy(fb->bo);
        }
    }
    free(fb);
}
