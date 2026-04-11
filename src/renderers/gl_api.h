#ifndef DRIVERBENCH_GL_API_H
#define DRIVERBENCH_GL_API_H

#include <stddef.h>

#ifndef GL_ENUM_TYPEDEF
#define GL_ENUM_TYPEDEF
typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef int GLsizei;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef ptrdiff_t GLintptr;
typedef ptrdiff_t GLsizeiptr;
typedef unsigned char GLubyte;
typedef unsigned long long GLuint64;
#endif

#ifndef GL_SYNC_TYPEDEF
#define GL_SYNC_TYPEDEF
struct db_gl_sync;
typedef struct db_gl_sync *GLsync;
#endif

#ifndef GL_FALSE
#define GL_FALSE 0U
#endif
#ifndef GL_TRUE
#define GL_TRUE 1U
#endif
#ifndef GL_NO_ERROR
#define GL_NO_ERROR 0U
#endif

#ifndef GL_BLEND
#define GL_BLEND 0x0BE2U
#endif
#ifndef GL_CLAMP
#define GL_CLAMP 0x2900U
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812FU
#endif
#ifndef GL_COLOR_ARRAY
#define GL_COLOR_ARRAY 0x8076U
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0U
#endif
#ifndef GL_COLOR_BUFFER_BIT
#define GL_COLOR_BUFFER_BIT 0x00004000U
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS 0x8B81U
#endif
#ifndef GL_CULL_FACE
#define GL_CULL_FACE 0x0B44U
#endif
#ifndef GL_DEPTH_TEST
#define GL_DEPTH_TEST 0x0B71U
#endif
#ifndef GL_DITHER
#define GL_DITHER 0x0BD0U
#define GL_SCISSOR_TEST 0x0C11U
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9U
#endif
#ifndef GL_DRAW_FRAMEBUFFER_BINDING
#define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6U
#endif
#ifndef GL_DYNAMIC_DRAW
#define GL_DYNAMIC_DRAW 0x88E8U
#endif
#ifndef GL_DYNAMIC_STORAGE_BIT
#define GL_DYNAMIC_STORAGE_BIT 0x0100U
#endif
#ifndef GL_EXTENSIONS
#define GL_EXTENSIONS 0x1F03U
#endif
#ifndef GL_FLOAT
#define GL_FLOAT 0x1406U
#endif
#ifndef GL_HALF_FLOAT
#define GL_HALF_FLOAT 0x140BU
#endif
#ifndef GL_HALF_FLOAT_OES
#define GL_HALF_FLOAT_OES 0x8D61U
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30U
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40U
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5U
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS 0x8B82U
#endif
#ifndef GL_MAP_COHERENT_BIT
#define GL_MAP_COHERENT_BIT 0x0080U
#endif
#ifndef GL_MAP_INVALIDATE_BUFFER_BIT
#define GL_MAP_INVALIDATE_BUFFER_BIT 0x0008U
#endif
#ifndef GL_MAP_PERSISTENT_BIT
#define GL_MAP_PERSISTENT_BIT 0x0040U
#endif
#ifndef GL_MAP_READ_BIT
#define GL_MAP_READ_BIT 0x0001U
#endif
#ifndef GL_READ_ONLY
#define GL_READ_ONLY 0x88B8U
#endif
#ifndef GL_MAP_UNSYNCHRONIZED_BIT
#define GL_MAP_UNSYNCHRONIZED_BIT 0x0020U
#endif
#ifndef GL_MAP_WRITE_BIT
#define GL_MAP_WRITE_BIT 0x0002U
#endif
#ifndef GL_NEAREST
#define GL_NEAREST 0x2600U
#endif
#ifndef GL_NUM_EXTENSIONS
#define GL_NUM_EXTENSIONS 0x821DU
#endif
#ifndef GL_PACK_ALIGNMENT
#define GL_PACK_ALIGNMENT 0x0D05U
#endif
#ifndef GL_PIXEL_PACK_BUFFER
#define GL_PIXEL_PACK_BUFFER 0x88EBU
#endif
#ifndef GL_PIXEL_UNPACK_BUFFER
#define GL_PIXEL_UNPACK_BUFFER 0x88ECU
#endif
#ifndef GL_UNPACK_ALIGNMENT
#define GL_UNPACK_ALIGNMENT 0x0CF5U
#endif
#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH 0x0CF2U
#endif
#ifndef GL_VIEWPORT
#define GL_VIEWPORT 0x0BA2U
#endif
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8U
#endif
#ifndef GL_READ_FRAMEBUFFER_BINDING
#define GL_READ_FRAMEBUFFER_BINDING 0x8CAAU
#endif
#ifndef GL_RENDERER
#define GL_RENDERER 0x1F01U
#endif
#ifndef GL_RGBA
#define GL_RGBA 0x1908U
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058U
#endif
#ifndef GL_RGBA16F
#define GL_RGBA16F 0x881AU
#endif
#ifndef GL_RGB10_A2
#define GL_RGB10_A2 0x8059U
#endif
#ifndef GL_BGRA
#define GL_BGRA 0x80E1U
#endif
#ifndef GL_UNSIGNED_INT_2_10_10_10_REV
#define GL_UNSIGNED_INT_2_10_10_10_REV 0x8368U
#endif
#ifndef GL_TEXTURE_ENV
#define GL_TEXTURE_ENV 0x2300U
#endif
#ifndef GL_TEXTURE_ENV_MODE
#define GL_TEXTURE_ENV_MODE 0x2200U
#endif
#ifndef GL_REPLACE
#define GL_REPLACE 0x1E01U
#endif
#ifndef GL_STATIC_DRAW
#define GL_STATIC_DRAW 0x88E4U
#endif
#ifndef GL_STREAM_DRAW
#define GL_STREAM_DRAW 0x88E0U
#endif
#ifndef GL_STREAM_READ
#define GL_STREAM_READ 0x88E1U
#endif
#ifndef GL_DYNAMIC_READ
#define GL_DYNAMIC_READ 0x88E9U
#endif
#ifndef GL_SYNC_GPU_COMMANDS_COMPLETE
#define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117U
#endif
#ifndef GL_SYNC_FLUSH_COMMANDS_BIT
#define GL_SYNC_FLUSH_COMMANDS_BIT 0x00000001U
#endif
#ifndef GL_ALREADY_SIGNALED
#define GL_ALREADY_SIGNALED 0x911AU
#endif
#ifndef GL_TIMEOUT_EXPIRED
#define GL_TIMEOUT_EXPIRED 0x911BU
#endif
#ifndef GL_CONDITION_SATISFIED
#define GL_CONDITION_SATISFIED 0x911CU
#endif
#ifndef GL_WAIT_FAILED
#define GL_WAIT_FAILED 0x911DU
#endif
#ifndef GL_TIMEOUT_IGNORED
#define GL_TIMEOUT_IGNORED 0xFFFFFFFFFFFFFFFFULL
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0U
#endif
#ifndef GL_TEXTURE_2D
#define GL_TEXTURE_2D 0x0DE1U
#endif
#ifndef GL_TEXTURE_COORD_ARRAY
#define GL_TEXTURE_COORD_ARRAY 0x8078U
#endif
#ifndef GL_TEXTURE_MAG_FILTER
#define GL_TEXTURE_MAG_FILTER 0x2800U
#endif
#ifndef GL_TEXTURE_MIN_FILTER
#define GL_TEXTURE_MIN_FILTER 0x2801U
#endif
#ifndef GL_TEXTURE_WRAP_S
#define GL_TEXTURE_WRAP_S 0x2802U
#endif
#ifndef GL_TEXTURE_WRAP_T
#define GL_TEXTURE_WRAP_T 0x2803U
#endif
#ifndef GL_TRIANGLES
#define GL_TRIANGLES 0x0004U
#endif
#ifndef GL_TRIANGLE_STRIP
#define GL_TRIANGLE_STRIP 0x0005U
#endif
#ifndef GL_UNSIGNED_BYTE
#define GL_UNSIGNED_BYTE 0x1401U
#endif
#ifndef GL_VERSION
#define GL_VERSION 0x1F02U
#endif
#ifndef GL_VERTEX_ARRAY
#define GL_VERTEX_ARRAY 0x8074U
#endif
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31U
#endif
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892U
#endif
#ifndef GL_WRITE_ONLY_OES
#define GL_WRITE_ONLY_OES 0x88B9U
#endif
#ifndef GL_WRITE_ONLY
#define GL_WRITE_ONLY 0x88B9U
#endif

void db_gl_check_error_at(const char *file, int line, const char *func);
#define DB_GL_CHECK_ERROR() db_gl_check_error_at(__FILE__, __LINE__, __func__)

#endif
