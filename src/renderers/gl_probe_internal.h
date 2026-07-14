#ifndef DRIVERBENCH_GL_PROBE_INTERNAL_H
#define DRIVERBENCH_GL_PROBE_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "core/db_render_types.h"

void db_gl_probe_drain_errors(void);
int db_gl_probe_step_error_free(void);
int db_gl_probe_finish(int success);
int db_gl_probe_rgb_matches(const uint8_t *pixels, size_t offset, uint8_t red,
                            uint8_t green, uint8_t blue);
size_t db_gl_upload_probe_size_bytes(size_t bytes);
void db_gl_upload_probe_fill_pattern(uint8_t *pattern, size_t count);
int db_gl_verify_buffer_prefix(const uint8_t *expected, size_t expected_size);
int db_gl_probe_texture_create_rgba16f(unsigned int *out_texture,
                                       uint32_t width, uint32_t height);
int db_gl_context_probe_persistent_upload(size_t bytes,
                                          const float *initial_vertices,
                                          void **mapped_out);
int db_gl_context_probe_map_range_upload(size_t bytes,
                                         const float *initial_vertices);
int db_gl_context_supports_unpack_row_length_upload(void);
int db_gl_probe_shadow_present_partial_upload_support(db_pixel_format_t format);
int db_gl_context_supports_full_npot_texture_2d(void);
int db_gl_context_supports_shadow_present_exact_size_texture_2d(void);
void db_gl_set_unpack_row_length_pixels(uint32_t pixel_count);

#endif
