#include "gl_api.h"
#include "gl_common.h"
#include "gl_proc_runtime.h"

db_gl_upload_proc_table_t g_upload_proc_table = {0};
db_gl_proc_resolver_fn_t g_proc_resolver = NULL;
int g_blend_enabled_state = -1;
int g_client_state_color_array_enabled = -1;
int g_client_state_texcoord_array_enabled = -1;
int g_client_state_vertex_array_enabled = -1;
int g_cull_face_enabled_state = -1;
int g_depth_test_enabled_state = -1;
int g_dither_enabled_state = -1;
unsigned int g_bound_draw_framebuffer = 0U;
int g_bound_draw_framebuffer_valid = 0;
unsigned int g_bound_read_framebuffer = 0U;
int g_bound_read_framebuffer_valid = 0;
unsigned int g_bound_unpack_buffer = 0U;
int g_bound_unpack_buffer_valid = 0;
unsigned int g_bound_vertex_array = 0U;
int g_bound_vertex_array_valid = 0;
unsigned int g_current_program = 0U;
int g_current_program_valid = 0;
unsigned int g_texture2d_binding_by_unit[DB_GL_TRACKED_TEXTURE_UNIT_COUNT] = {
    0U};
int g_texture2d_binding_valid_by_unit[DB_GL_TRACKED_TEXTURE_UNIT_COUNT] = {0};
unsigned int g_texture2d_enabled_state = 0U;
int g_texture2d_enabled_state_valid = 0;
unsigned int g_active_texture_unit = GL_TEXTURE0;
int g_active_texture_unit_valid = 0;
int g_unpack_alignment_state = 4;
int g_unpack_alignment_state_valid = 0;
int g_unpack_row_length_state = 0;
int g_unpack_row_length_state_valid = 0;
