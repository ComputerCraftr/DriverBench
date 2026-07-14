#include "gl_gradient_qualification.h"

#include "core/db_core.h"
#include "core/db_gradient_divergence.h"
#include "core/db_hash.h"
#include "core/db_numeric.h"
#include "core/db_render_types.h"
#include "renderers/gl_common.h"
#include "renderers/gl_hash_readback.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

int db_gl_qualify_current_framebuffer(
    const char *backend, const db_pixel_surface_t *reference,
    db_gl_framebuffer_hash_scratch_t *scratch,
    const db_gradient_compare_context_t *context, const char *divergence_path) {
    if ((backend == NULL) || (reference == NULL) ||
        (reference->pixels == NULL) || (scratch == NULL) ||
        (reference->pixel_width == 0U) || (reference->pixel_height == 0U)) {
        return 0;
    }
    size_t pixel_count = 0U;
    size_t rgba8_bytes = 0U;
    if ((db_try_mul_size(reference->pixel_width, reference->pixel_height,
                         &pixel_count) == 0) ||
        (db_try_mul_size(pixel_count, DB_RGBA8_BYTES_PER_PIXEL, &rgba8_bytes) ==
         0)) {
        return 0;
    }
    uint8_t *const expected = malloc(rgba8_bytes);
    uint8_t *const observed = malloc(rgba8_bytes);
    if ((expected == NULL) || (observed == NULL)) {
        free(expected);
        free(observed);
        return 0;
    }
    const size_t reference_stride = db_checked_mul_size(
        backend, "gradient_reference_stride", reference->pixel_width,
        db_pixel_surface_pixel_bytes(reference));
    const uint16_t *const native = db_gl_read_framebuffer_rgba16f_or_fail(
        backend, reference->pixel_width, reference->pixel_height, scratch);
    const size_t native_stride =
        db_checked_mul_size(backend, "gradient_native_stride",
                            reference->pixel_width, DB_RGBA16F_BYTES_PER_PIXEL);
    const int converted =
        db_working_rgba8_canonicalize(reference->pixels, reference->format,
                                      reference->pixel_width,
                                      reference->pixel_height, reference_stride,
                                      0, expected, rgba8_bytes) &&
        db_working_rgba8_canonicalize(
            native, DB_PIXEL_FORMAT_RGBA16F, reference->pixel_width,
            reference->pixel_height, native_stride, 1, observed, rgba8_bytes);
    (void)db_gl_upload_stream_end_read(&scratch->stream, backend);
    db_gradient_divergence_t divergence = {0};
    if (converted != 0) {
        divergence = db_gradient_compare_rgba8(
            expected, observed, reference->pixel_width, reference->pixel_height,
            context);
    } else {
        divergence.divergent = 1;
        divergence.stage = DB_GRADIENT_DIVERGENCE_CONVERSION;
        if (context != NULL) {
            divergence.context = *context;
        }
    }
    if ((divergence.divergent != 0) && (divergence_path != NULL) &&
        (divergence_path[0] != '\0')) {
        (void)db_gradient_divergence_write(divergence_path, &divergence);
    }
    free(expected);
    free(observed);
    return DB_BOOL(divergence.divergent == 0);
}
