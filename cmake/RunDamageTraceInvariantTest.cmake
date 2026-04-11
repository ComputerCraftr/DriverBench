include("${CMAKE_CURRENT_LIST_DIR}/TestRunnerCommon.cmake")

db_test_require_defined(TEST_BIN)
db_test_require_defined(TEST_ARGS)
db_test_run_command(output skip_reason status "${TEST_ARGS}"
                    "Damage trace command failed")
if(NOT "${skip_reason}" STREQUAL "")
    message(STATUS "Skipped: ${skip_reason}")
    return()
endif()

function(db_damage_field line key out_value)
    if("${line}" MATCHES "(^| )${key}=([^ ]+)")
        set(${out_value}
            "${CMAKE_MATCH_2}"
            PARENT_SCOPE)
    else()
        message(
            FATAL_ERROR "Malformed damage event: missing '${key}' in:\n${line}")
    endif()
endfunction()

string(REPLACE "\r" "" output "${output}")
string(REPLACE "\n" ";" lines "${output}")
set(upload_count 0)
set(render_hashes "")
foreach(line IN LISTS lines)
    if(NOT line MATCHES "damage_summary ")
        continue()
    endif()
    foreach(
        field
        frame
        backend
        stage
        result
        rejected
        truncated
        src_hash
        dst_hash)
        db_damage_field("${line}" "${field}" "${field}_value")
    endforeach()
    if(NOT "${rejected_value}" STREQUAL "0"
       OR NOT "${truncated_value}" STREQUAL "false"
       OR "${result_value}" STREQUAL "failed")
        message(FATAL_ERROR "Invalid damage event:\n${line}")
    endif()
    if("${backend_value}" STREQUAL "gl1" AND "${stage_value}" STREQUAL
                                             "staging_write")
        if("${src_hash_value}" STREQUAL "0x0000000000000000"
           OR NOT "${src_hash_value}" STREQUAL "${dst_hash_value}")
            message(
                FATAL_ERROR
                    "GL1 staging contents differ from authoritative shadow on frame "
                    "${frame_value}:\n${line}")
        endif()
        set("staging_hash_${frame_value}" "${dst_hash_value}")
    elseif("${backend_value}" STREQUAL "gl1" AND "${stage_value}" STREQUAL
                                                 "upload")
        set(staging_var "staging_hash_${frame_value}")
        if("${src_hash_value}" STREQUAL "0x0000000000000000")
            message(
                FATAL_ERROR
                    "GL1 executed upload has no observable source hash on frame ${frame_value}:\n${line}"
            )
        endif()
        if(DEFINED ${staging_var} AND NOT "${src_hash_value}" STREQUAL
                                      "${${staging_var}}")
            message(
                FATAL_ERROR
                    "GL1 upload source differs from staging contents on frame "
                    "${frame_value}:\n${line}")
        endif()
        math(EXPR upload_count "${upload_count} + 1")
    elseif("${backend_value}" STREQUAL "display" AND "${stage_value}" STREQUAL
                                                     "render_target")
        list(APPEND render_hashes "${dst_hash_value}")
    endif()
endforeach()

if(upload_count LESS 3)
    message(
        FATAL_ERROR
            "Expected at least three GL1 upload events, found ${upload_count}")
endif()
list(REMOVE_DUPLICATES render_hashes)
list(LENGTH render_hashes render_hash_count)
if(render_hash_count LESS 2)
    message(
        FATAL_ERROR
            "GL1 render target made no forward progress: ${render_hashes}")
endif()

message(STATUS "Damage trace invariants passed: uploads=${upload_count}, "
               "render_target_hashes=${render_hash_count}")
