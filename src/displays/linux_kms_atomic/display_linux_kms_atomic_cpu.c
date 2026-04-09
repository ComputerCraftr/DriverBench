#include "display_linux_kms_atomic_internal.h"

#include <gbm.h>
#include <stddef.h>
#include <stdint.h>

#include "../../core/db_buffer_convert.h"
#include "../../core/db_core.h"
#include "../../renderers/cpu_renderer/renderer_cpu_renderer.h"
#include "../../renderers/renderer_benchmark_runtime.h"

static struct fb *
db_cpu_create_fb_from_framebuffer(struct gbm_device *gbm, int fd,
                                  const db_benchmark_pixel_surface_t *surface,
                                  uint32_t width, uint32_t height) {
    if ((surface == NULL) || (surface->pixel_width == 0U) ||
        (surface->pixel_height == 0U)) {
        diex("cpu framebuffer surface is invalid");
    }
    const uint32_t src_width = surface->pixel_width;
    const uint32_t src_height = surface->pixel_height;
    const int use_hdr_float_bo = (surface->uses_rgba16f != 0) ? 1 : 0;
    const uint32_t *const pixels_rgba8 = surface->pixels_rgba8;
    const uint16_t *const pixels_rgba16f = surface->pixels_rgba16f;
    uint32_t bo_flags = GBM_BO_USE_SCANOUT;
#ifdef GBM_BO_USE_WRITE
    bo_flags |= GBM_BO_USE_WRITE;
#else
    bo_flags |= GBM_BO_USE_RENDERING;
#endif
    struct gbm_bo *bo =
        gbm_bo_create(gbm, width, height, GBM_FORMAT_XRGB8888, bo_flags);
    if (bo == NULL) {
        diex("gbm_bo_create failed for CPU scanout buffer");
    }

    uint32_t map_stride_bytes = 0U;
    void *map_data = NULL;
    uint8_t *map_ptr =
        gbm_bo_map(bo, 0, 0, width, height, GBM_BO_TRANSFER_WRITE,
                   &map_stride_bytes, &map_data);
    if ((map_ptr == NULL) || (map_data == NULL)) {
        gbm_bo_destroy(bo);
        diex("gbm_bo_map failed for CPU scanout buffer");
    }

    const size_t dst_stride_pixels =
        (size_t)map_stride_bytes / sizeof(uint32_t);
    const int map_ptr_u32_aligned =
        ((((uintptr_t)map_ptr) % _Alignof(uint32_t)) == 0U) ? 1 : 0;
    const int map_stride_u32_aligned =
        ((map_stride_bytes % (uint32_t)sizeof(uint32_t)) == 0U) ? 1 : 0;
    if (use_hdr_float_bo != 0) {
        if (pixels_rgba16f == NULL) {
            gbm_bo_unmap(bo, map_data);
            gbm_bo_destroy(bo);
            diex("cpu hdr framebuffer is NULL");
        }
        if ((src_width == width) && (src_height == height) &&
            (map_ptr_u32_aligned != 0)) {
            uint32_t *const dst_pixels =
                (uint32_t *)DB_ASSUME_ALIGNED(map_ptr, _Alignof(uint32_t));
            db_convert_rgba16f_to_xrgb8888_block(
                dst_pixels, dst_stride_pixels, pixels_rgba16f,
                (size_t)src_width, 0U, height, 0U, width);
        } else {
            for (uint32_t row = 0U; row < height; row++) {
                const uint32_t src_row = db_checked_u64_to_u32(
                    BACKEND_NAME, "src_row",
                    ((uint64_t)row * (uint64_t)src_height) / (uint64_t)height);
                uint8_t *const dst_row_bytes =
                    map_ptr + ((size_t)row * (size_t)map_stride_bytes);
                uint32_t *const dst_row =
                    ((map_ptr_u32_aligned != 0) &&
                     (map_stride_u32_aligned != 0))
                        ? (uint32_t *)DB_ASSUME_ALIGNED(dst_row_bytes,
                                                        _Alignof(uint32_t))
                        : NULL;
                for (uint32_t col = 0U; col < width; col++) {
                    const uint32_t src_col = db_checked_u64_to_u32(
                        BACKEND_NAME, "src_col",
                        ((uint64_t)col * (uint64_t)src_width) /
                            (uint64_t)width);
                    const size_t src_base =
                        (((size_t)src_row * (size_t)src_width) +
                         (size_t)src_col) *
                        4U;
                    const uint32_t packed = db_pack_xrgb8888_from_rgb16f3(
                        &pixels_rgba16f[src_base]);
                    if (dst_row != NULL) {
                        dst_row[col] = packed;
                    } else {
                        db_copy_bytes(dst_row_bytes +
                                          ((size_t)col * sizeof(uint32_t)),
                                      &packed, sizeof(packed));
                    }
                }
            }
        }
    } else {
        if (pixels_rgba8 == NULL) {
            gbm_bo_unmap(bo, map_data);
            gbm_bo_destroy(bo);
            diex("cpu rgba8 framebuffer is NULL");
        }
        if ((src_width == width) && (src_height == height) &&
            (map_ptr_u32_aligned != 0)) {
            uint32_t *const dst_pixels =
                (uint32_t *)DB_ASSUME_ALIGNED(map_ptr, _Alignof(uint32_t));
            db_convert_rgba8_to_xrgb8888_block(dst_pixels, dst_stride_pixels,
                                               pixels_rgba8, (size_t)src_width,
                                               0U, height, 0U, width);
        } else {
            for (uint32_t row = 0U; row < height; row++) {
                const uint32_t src_row = db_checked_u64_to_u32(
                    BACKEND_NAME, "src_row",
                    ((uint64_t)row * (uint64_t)src_height) / (uint64_t)height);
                uint8_t *const dst_row_bytes =
                    map_ptr + ((size_t)row * (size_t)map_stride_bytes);
                uint32_t *const dst_row =
                    ((map_ptr_u32_aligned != 0) &&
                     (map_stride_u32_aligned != 0))
                        ? (uint32_t *)DB_ASSUME_ALIGNED(dst_row_bytes,
                                                        _Alignof(uint32_t))
                        : NULL;
                for (uint32_t col = 0U; col < width; col++) {
                    const uint32_t src_col = db_checked_u64_to_u32(
                        BACKEND_NAME, "src_col",
                        ((uint64_t)col * (uint64_t)src_width) /
                            (uint64_t)width);
                    const size_t src_index =
                        ((size_t)src_row * (size_t)src_width) + (size_t)src_col;
                    const uint32_t rgba = pixels_rgba8[src_index];
                    const uint32_t packed =
                        db_pack_xrgb8888_from_rgba8888(rgba);
                    if (dst_row != NULL) {
                        dst_row[col] = packed;
                    } else {
                        db_copy_bytes(dst_row_bytes +
                                          ((size_t)col * sizeof(uint32_t)),
                                      &packed, sizeof(packed));
                    }
                }
            }
        }
    }
    gbm_bo_unmap(bo, map_data);

    return fb_from_bo(fd, bo, 0);
}

struct fb *db_kms_atomic_next_cpu_fb(void *user_ctx, uint32_t frame_index) {
    db_kms_atomic_cpu_frame_producer_t *producer =
        (db_kms_atomic_cpu_frame_producer_t *)user_ctx;
    if (producer == NULL) {
        return NULL;
    }
    (void)db_renderer_cpu_renderer_render_frame_to_surface(
        frame_index, &producer->surface, NULL);
    return db_cpu_create_fb_from_framebuffer(producer->gbm, producer->kms_fd,
                                             &producer->surface,
                                             producer->width, producer->height);
}
