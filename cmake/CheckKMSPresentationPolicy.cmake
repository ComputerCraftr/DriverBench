if(NOT DEFINED SOURCE_ROOT OR "${SOURCE_ROOT}" STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()

set(DB_KMS_CPU_FILE "${SOURCE_ROOT}/src/displays/linux_kms_atomic/kms_cpu.c")
set(DB_KMS_FILES
    "${SOURCE_ROOT}/src/displays/linux_kms_atomic/kms_core.c"
    "${SOURCE_ROOT}/src/displays/linux_kms_atomic/kms_gl_frame.c"
    "${DB_KMS_CPU_FILE}"
    "${SOURCE_ROOT}/src/displays/linux_kms_atomic/kms_display.c"
    "${SOURCE_ROOT}/src/displays/linux_kms_atomic/kms_egl.c"
    "${SOURCE_ROOT}/src/displays/linux_kms_atomic/kms_internal.h"
    "${SOURCE_ROOT}/src/displays/linux_kms_atomic/kms_runner.c"
    "${SOURCE_ROOT}/src/displays/linux_kms_atomic/kms_runner.h")

set(DB_KMS_SOURCE "")
foreach(DB_KMS_FILE IN LISTS DB_KMS_FILES)
    file(READ "${DB_KMS_FILE}" DB_KMS_FILE_SOURCE)
    string(APPEND DB_KMS_SOURCE "\n${DB_KMS_FILE_SOURCE}")
endforeach()

set(DB_KMS_FORBIDDEN
    "DB_GL_SHADOW_PRESENT|db_gl_shadow_present|PBO"
    "preserved_framebuffer_count[ \t\r\n]*=[ \t\r\n]*[1-9]"
    "EGL_(RED|GREEN|BLUE)_SIZE[ \t\r\n]*,[ \t\r\n]*8")
foreach(DB_KMS_PATTERN IN LISTS DB_KMS_FORBIDDEN)
    if(DB_KMS_SOURCE MATCHES "${DB_KMS_PATTERN}")
        message(
            FATAL_ERROR "KMS presentation policy violation: ${DB_KMS_PATTERN}")
    endif()
endforeach()

file(READ "${DB_KMS_CPU_FILE}" DB_KMS_CPU_SOURCE)
string(REGEX MATCHALL "gbm_bo_create[ \t\r\n]*\\(" DB_KMS_BO_CREATES
             "${DB_KMS_CPU_SOURCE}")
list(LENGTH DB_KMS_BO_CREATES DB_KMS_BO_CREATE_COUNT)
if(NOT DB_KMS_BO_CREATE_COUNT EQUAL 1)
    message(
        FATAL_ERROR
            "KMS CPU must have exactly one persistent scanout allocation site")
endif()

file(READ "${SOURCE_ROOT}/src/displays/display_gl_renderer_select_common.h"
     DB_GL_DISPATCH_SOURCE)
if(DB_GL_DISPATCH_SOURCE MATCHES
   "db_gl(1|3)_render_frame[ \t\r\n]*\\([^;]*,[ \t\r\n]*0[ \t\r\n]*,[ \t\r\n]*0"
)
    message(FATAL_ERROR "GL display dispatch must not use a zero KMS viewport")
endif()

file(READ "${SOURCE_ROOT}/src/renderers/gl_shadow_present.c"
     DB_GL1_PRESENT_SOURCE)
file(READ "${SOURCE_ROOT}/src/renderers/gl_shadow_present_frame.c"
     DB_GL1_PRESENT_FRAME_SOURCE)
string(APPEND DB_GL1_PRESENT_SOURCE "\n${DB_GL1_PRESENT_FRAME_SOURCE}")
file(READ "${SOURCE_ROOT}/src/renderers/gl_shadow_present_hdr.c"
     DB_GL1_PRESENT_HDR_SOURCE)
string(APPEND DB_GL1_PRESENT_SOURCE "\n${DB_GL1_PRESENT_HDR_SOURCE}")
if(DB_GL1_PRESENT_SOURCE MATCHES "hdr_present_(program|shader)"
   OR DB_GL1_PRESENT_SOURCE MATCHES "db_gl_create_(shader|program)")
    message(
        FATAL_ERROR "GL1 HDR presentation must remain shaderless fixed-function"
    )
endif()
if(NOT DB_GL1_PRESENT_SOURCE MATCHES
   "DB_GL_SHADOW_PRESENT_TEXTURE_BT2020_PQ_RGB10A2"
   OR NOT DB_GL1_PRESENT_SOURCE MATCHES
      "db_gl_shadow_present_upload_hdr_damage_blocks")
    message(
        FATAL_ERROR
            "GL1 HDR presentation must retain packed RGB10A2 damage uploads")
endif()
