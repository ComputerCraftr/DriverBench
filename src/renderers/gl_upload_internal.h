#ifndef DRIVERBENCH_GL_UPLOAD_INTERNAL_H
#define DRIVERBENCH_GL_UPLOAD_INTERNAL_H

#include "gl_api.h"
#include "gl_common.h"

static inline GLenum db_gl_upload_target_gl_enum(db_gl_upload_target_t target) {
    if (target == DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER) {
        return GL_PIXEL_UNPACK_BUFFER;
    }
    if (target == DB_GL_UPLOAD_TARGET_PBO_PACK_BUFFER) {
        return GL_PIXEL_PACK_BUFFER;
    }
    return GL_ARRAY_BUFFER;
}

#endif
