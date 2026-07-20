if(NOT DEFINED OUT_HEADER)
    message(FATAL_ERROR "OUT_HEADER is required")
endif()
if(NOT DEFINED GL3_IR_EXECUTE_VERT OR NOT DEFINED GL3_IR_EXECUTE_FRAG)
    message(FATAL_ERROR "GL3 IR execution shaders are required")
endif()
if(NOT DEFINED GL3_EXACT_LOOKUP_VERT
   OR NOT DEFINED GL3_EXACT_LOOKUP_RGBA8_FRAG
   OR NOT DEFINED GL3_EXACT_LOOKUP_RGBA16F_FRAG)
    message(FATAL_ERROR "GL3 exact lookup shaders are required")
endif()
if(NOT DEFINED GL3_PRESENTATION_VERT OR NOT DEFINED GL3_PRESENTATION_FRAG)
    message(FATAL_ERROR "GL3 presentation shaders are required")
endif()

function(db_escape_c_string INPUT_TEXT OUTPUT_VAR)
    set(_value "${INPUT_TEXT}")
    string(REPLACE "\\" "\\\\" _value "${_value}")
    string(REPLACE "\"" "\\\"" _value "${_value}")
    string(REPLACE "\r\n" "\n" _value "${_value}")
    string(REPLACE "\r" "\n" _value "${_value}")
    string(REPLACE "\n" "\\n\"\n\"" _value "${_value}")
    set(${OUTPUT_VAR}
        "${_value}"
        PARENT_SCOPE)
endfunction()

function(db_read_shader_with_includes INPUT_PATH OUTPUT_VAR)
    set(_stack "${ARGN}")
    get_filename_component(_input_real "${INPUT_PATH}" REALPATH)
    list(FIND _stack "${_input_real}" _stack_index)
    if(NOT _stack_index EQUAL -1)
        message(FATAL_ERROR "Cyclic shader include detected: ${_input_real}")
    endif()
    list(APPEND _stack "${_input_real}")

    file(READ "${INPUT_PATH}" _text)
    get_filename_component(_dir "${INPUT_PATH}" DIRECTORY)
    string(REGEX MATCH "#include[ \t]+\"([^\"]+)\"" _inc_match "${_text}")
    while(_inc_match)
        set(_inc_rel "${CMAKE_MATCH_1}")
        set(_inc_path "${_dir}/${_inc_rel}")
        if(NOT EXISTS "${_inc_path}")
            message(FATAL_ERROR "Shader include not found: ${_inc_path}")
        endif()
        db_read_shader_with_includes("${_inc_path}" _inc_text ${_stack})
        string(REPLACE "${_inc_match}" "${_inc_text}" _text "${_text}")
        string(REGEX MATCH "#include[ \t]+\"([^\"]+)\"" _inc_match "${_text}")
    endwhile()
    set(${OUTPUT_VAR}
        "${_text}"
        PARENT_SCOPE)
endfunction()

function(db_spv_hex_to_u32_words HEX_TEXT OUTPUT_WORDS OUTPUT_COUNT)
    string(LENGTH "${HEX_TEXT}" _hex_len)
    math(EXPR _rem "${_hex_len} % 8")
    if(NOT _rem EQUAL 0)
        message(FATAL_ERROR "SPIR-V binary must be a multiple of 4 bytes")
    endif()

    set(_words "")
    math(EXPR _count "${_hex_len} / 8")
    if(_count GREATER 0)
        math(EXPR _last "${_count} - 1")
        foreach(_i RANGE 0 ${_last})
            math(EXPR _offset "${_i} * 8")
            string(SUBSTRING "${HEX_TEXT}" ${_offset} 8 _chunk)
            string(SUBSTRING "${_chunk}" 0 2 _b0)
            string(SUBSTRING "${_chunk}" 2 2 _b1)
            string(SUBSTRING "${_chunk}" 4 2 _b2)
            string(SUBSTRING "${_chunk}" 6 2 _b3)
            list(APPEND _words "0x${_b3}${_b2}${_b1}${_b0}U")
        endforeach()
    endif()

    string(JOIN ", " _words_joined ${_words})
    set(${OUTPUT_WORDS}
        "${_words_joined}"
        PARENT_SCOPE)
    set(${OUTPUT_COUNT}
        "${_count}"
        PARENT_SCOPE)
endfunction()

db_read_shader_with_includes("${GL3_IR_EXECUTE_VERT}" _gl3_ir_execute_vert_text)
db_read_shader_with_includes("${GL3_IR_EXECUTE_FRAG}" _gl3_ir_execute_frag_text)
db_escape_c_string("${_gl3_ir_execute_vert_text}" _gl3_ir_execute_vert_escaped)
db_escape_c_string("${_gl3_ir_execute_frag_text}" _gl3_ir_execute_frag_escaped)
db_read_shader_with_includes("${GL3_EXACT_LOOKUP_VERT}"
                             _gl3_exact_lookup_vert_text)
db_read_shader_with_includes("${GL3_EXACT_LOOKUP_RGBA8_FRAG}"
                             _gl3_exact_lookup_rgba8_frag_text)
db_read_shader_with_includes("${GL3_EXACT_LOOKUP_RGBA16F_FRAG}"
                             _gl3_exact_lookup_rgba16f_frag_text)
db_escape_c_string("${_gl3_exact_lookup_vert_text}"
                   _gl3_exact_lookup_vert_escaped)
db_escape_c_string("${_gl3_exact_lookup_rgba8_frag_text}"
                   _gl3_exact_lookup_rgba8_frag_escaped)
db_escape_c_string("${_gl3_exact_lookup_rgba16f_frag_text}"
                   _gl3_exact_lookup_rgba16f_frag_escaped)
db_read_shader_with_includes("${GL3_PRESENTATION_VERT}"
                             _gl3_presentation_vert_text)
db_read_shader_with_includes("${GL3_PRESENTATION_FRAG}"
                             _gl3_presentation_frag_text)
db_escape_c_string("${_gl3_presentation_vert_text}"
                   _gl3_presentation_vert_escaped)
db_escape_c_string("${_gl3_presentation_frag_text}"
                   _gl3_presentation_frag_escaped)

set(_vk_available 0)
set(_vk_ir_execute_vert_words "")
set(_vk_ir_execute_frag_words "")
set(_vk_ir_execute_vert_count 0)
set(_vk_ir_execute_frag_count 0)
set(_vk_presentation_vert_words "")
set(_vk_presentation_frag_words "")
set(_vk_presentation_vert_count 0)
set(_vk_presentation_frag_count 0)
set(_vk_transport_pack_comp_words "")
set(_vk_transport_unpack_frag_words "")
set(_vk_transport_pack_comp_count 0)
set(_vk_transport_unpack_frag_count 0)
if(DEFINED VK_IR_EXECUTE_VERT_SPV AND DEFINED VK_IR_EXECUTE_FRAG_SPV)
    file(READ "${VK_IR_EXECUTE_VERT_SPV}" _vk_ir_execute_vert_hex HEX)
    file(READ "${VK_IR_EXECUTE_FRAG_SPV}" _vk_ir_execute_frag_hex HEX)
    string(TOUPPER "${_vk_ir_execute_vert_hex}" _vk_ir_execute_vert_hex)
    string(TOUPPER "${_vk_ir_execute_frag_hex}" _vk_ir_execute_frag_hex)
    db_spv_hex_to_u32_words("${_vk_ir_execute_vert_hex}"
                            _vk_ir_execute_vert_words _vk_ir_execute_vert_count)
    db_spv_hex_to_u32_words("${_vk_ir_execute_frag_hex}"
                            _vk_ir_execute_frag_words _vk_ir_execute_frag_count)
    file(READ "${VK_PRESENTATION_VERT_SPV}" _vk_presentation_vert_hex HEX)
    file(READ "${VK_PRESENTATION_FRAG_SPV}" _vk_presentation_frag_hex HEX)
    string(TOUPPER "${_vk_presentation_vert_hex}" _vk_presentation_vert_hex)
    string(TOUPPER "${_vk_presentation_frag_hex}" _vk_presentation_frag_hex)
    db_spv_hex_to_u32_words(
        "${_vk_presentation_vert_hex}" _vk_presentation_vert_words
        _vk_presentation_vert_count)
    db_spv_hex_to_u32_words(
        "${_vk_presentation_frag_hex}" _vk_presentation_frag_words
        _vk_presentation_frag_count)
    file(READ "${VK_TRANSPORT_PACK_COMP_SPV}" _vk_transport_pack_comp_hex HEX)
    file(READ "${VK_TRANSPORT_UNPACK_FRAG_SPV}" _vk_transport_unpack_frag_hex
         HEX)
    string(TOUPPER "${_vk_transport_pack_comp_hex}" _vk_transport_pack_comp_hex)
    string(TOUPPER "${_vk_transport_unpack_frag_hex}"
                   _vk_transport_unpack_frag_hex)
    db_spv_hex_to_u32_words(
        "${_vk_transport_pack_comp_hex}" _vk_transport_pack_comp_words
        _vk_transport_pack_comp_count)
    db_spv_hex_to_u32_words(
        "${_vk_transport_unpack_frag_hex}" _vk_transport_unpack_frag_words
        _vk_transport_unpack_frag_count)
    set(_vk_available 1)
endif()

file(WRITE "${OUT_HEADER}"
     "/* Auto-generated by GenerateEmbeddedShadersHeader.cmake */\n")
file(APPEND "${OUT_HEADER}" "#ifndef DRIVERBENCH_EMBEDDED_SHADERS_H\n")
file(APPEND "${OUT_HEADER}" "#define DRIVERBENCH_EMBEDDED_SHADERS_H\n\n")
file(APPEND "${OUT_HEADER}" "#include <stddef.h>\n")
file(APPEND "${OUT_HEADER}" "#include <stdint.h>\n\n")
file(
    APPEND "${OUT_HEADER}"
    "[[maybe_unused]] static const char db_gl3_ir_execute_vert_source[] =\n\"${_gl3_ir_execute_vert_escaped}\";\n\n"
)
file(
    APPEND "${OUT_HEADER}"
    "[[maybe_unused]] static const char db_gl3_ir_execute_frag_source[] =\n\"${_gl3_ir_execute_frag_escaped}\";\n\n"
)
file(
    APPEND "${OUT_HEADER}"
    "[[maybe_unused]] static const char db_gl3_exact_lookup_vert_source[] =\n\"${_gl3_exact_lookup_vert_escaped}\";\n\n"
)
file(
    APPEND "${OUT_HEADER}"
    "[[maybe_unused]] static const char db_gl3_exact_lookup_rgba8_frag_source[] =\n\"${_gl3_exact_lookup_rgba8_frag_escaped}\";\n\n"
)
file(
    APPEND "${OUT_HEADER}"
    "[[maybe_unused]] static const char db_gl3_exact_lookup_rgba16f_frag_source[] =\n\"${_gl3_exact_lookup_rgba16f_frag_escaped}\";\n\n"
)
file(
    APPEND "${OUT_HEADER}"
    "[[maybe_unused]] static const char db_gl3_presentation_vert_source[] =\n\"${_gl3_presentation_vert_escaped}\";\n\n"
)
file(
    APPEND "${OUT_HEADER}"
    "[[maybe_unused]] static const char db_gl3_presentation_frag_source[] =\n\"${_gl3_presentation_frag_escaped}\";\n\n"
)
file(APPEND "${OUT_HEADER}"
     "#define DB_EMBEDDED_VULKAN_SPV_AVAILABLE ${_vk_available}\n\n")
if(_vk_available EQUAL 1)
    file(
        APPEND "${OUT_HEADER}"
        "[[maybe_unused]] static const uint32_t db_vk_ir_execute_vert_spv[] = {\n    ${_vk_ir_execute_vert_words}\n};\n"
    )
    file(
        APPEND "${OUT_HEADER}"
        "[[maybe_unused]] static const size_t db_vk_ir_execute_vert_spv_word_count = ${_vk_ir_execute_vert_count}U;\n\n"
    )
    file(
        APPEND "${OUT_HEADER}"
        "[[maybe_unused]] static const uint32_t db_vk_ir_execute_frag_spv[] = {\n    ${_vk_ir_execute_frag_words}\n};\n"
    )
    file(
        APPEND "${OUT_HEADER}"
        "[[maybe_unused]] static const size_t db_vk_ir_execute_frag_spv_word_count = ${_vk_ir_execute_frag_count}U;\n\n"
    )
    file(
        APPEND "${OUT_HEADER}"
        "[[maybe_unused]] static const uint32_t db_vk_presentation_vert_spv[] = {\n    ${_vk_presentation_vert_words}\n};\n"
    )
    file(
        APPEND "${OUT_HEADER}"
        "[[maybe_unused]] static const size_t db_vk_presentation_vert_spv_word_count = ${_vk_presentation_vert_count}U;\n\n"
    )
    file(
        APPEND "${OUT_HEADER}"
        "[[maybe_unused]] static const uint32_t db_vk_presentation_frag_spv[] = {\n    ${_vk_presentation_frag_words}\n};\n"
    )
    file(
        APPEND "${OUT_HEADER}"
        "[[maybe_unused]] static const size_t db_vk_presentation_frag_spv_word_count = ${_vk_presentation_frag_count}U;\n\n"
    )
    file(
        APPEND "${OUT_HEADER}"
        "[[maybe_unused]] static const uint32_t db_vk_transport_pack_comp_spv[] = {\n    ${_vk_transport_pack_comp_words}\n};\n"
    )
    file(
        APPEND "${OUT_HEADER}"
        "[[maybe_unused]] static const size_t db_vk_transport_pack_comp_spv_word_count = ${_vk_transport_pack_comp_count}U;\n\n"
    )
    file(
        APPEND "${OUT_HEADER}"
        "[[maybe_unused]] static const uint32_t db_vk_transport_unpack_frag_spv[] = {\n    ${_vk_transport_unpack_frag_words}\n};\n"
    )
    file(
        APPEND "${OUT_HEADER}"
        "[[maybe_unused]] static const size_t db_vk_transport_unpack_frag_spv_word_count = ${_vk_transport_unpack_frag_count}U;\n\n"
    )
endif()
file(APPEND "${OUT_HEADER}" "#endif\n")
