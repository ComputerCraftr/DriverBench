include("${CMAKE_CURRENT_LIST_DIR}/TestRunnerCommon.cmake")

db_test_require_defined(TEST_BIN)
db_test_require_defined(TEST_ARGS_A)
db_test_require_defined(TEST_ARGS_B)

db_test_run_command(output_a skip_a status_a "${TEST_ARGS_A}" "Trace A failed")
db_test_run_command(output_b skip_b status_b "${TEST_ARGS_B}" "Trace B failed")

if(NOT "${skip_a}" STREQUAL "")
    message(STATUS "Skipped A: ${skip_a}")
    return()
endif()
if(NOT "${skip_b}" STREQUAL "")
    message(STATUS "Skipped B: ${skip_b}")
    return()
endif()

function(db_damage_trace_field line key out_value)
    if("${line}" MATCHES "(^| )${key}=([^ ]+)")
        set(${out_value}
            "${CMAKE_MATCH_2}"
            PARENT_SCOPE)
    else()
        message(
            FATAL_ERROR "Malformed damage event: missing '${key}' in:\n${line}")
    endif()
endfunction()

function(db_extract_logical_damage output out_records)
    string(REPLACE "\r" "" output "${output}")
    string(REPLACE "\n" ";" lines "${output}")
    set(records "")
    foreach(line IN LISTS lines)
        if(NOT line MATCHES "damage_summary ")
            continue()
        endif()
        db_damage_trace_field("${line}" "stage" stage)
        if(NOT "${stage}" STREQUAL "logical")
            continue()
        endif()
        foreach(
            field
            frame
            width
            height
            pixels
            duplicate_pixels
            union_hash
            rejected
            truncated
            result)
            db_damage_trace_field("${line}" "${field}" "${field}_value")
        endforeach()
        set(record
            "frame=${frame_value} width=${width_value} height=${height_value} pixels=${pixels_value} duplicate_pixels=${duplicate_pixels_value} union_hash=${union_hash_value} rejected=${rejected_value} truncated=${truncated_value} result=${result_value}"
        )
        list(APPEND records "${record}")
    endforeach()
    if("${records}" STREQUAL "")
        message(
            FATAL_ERROR
                "No canonical logical damage events found in output:\n${output}"
        )
    endif()
    set(${out_records}
        "${records}"
        PARENT_SCOPE)
endfunction()

db_extract_logical_damage("${output_a}" records_a)
db_extract_logical_damage("${output_b}" records_b)

list(LENGTH records_a count_a)
list(LENGTH records_b count_b)
if(NOT count_a EQUAL count_b)
    message(
        FATAL_ERROR
            "Damage trace event-count divergence: A=${count_a}, B=${count_b}\n"
            "A=${records_a}\nB=${records_b}")
endif()

if(count_a GREATER 0)
    math(EXPR last_index "${count_a} - 1")
    foreach(index RANGE 0 ${last_index})
        list(GET records_a ${index} record_a)
        list(GET records_b ${index} record_b)
        if(NOT "${record_a}" STREQUAL "${record_b}")
            message(
                FATAL_ERROR
                    "First logical damage divergence at event ${index}:\n"
                    "A: ${record_a}\nB: ${record_b}")
        endif()
    endforeach()
endif()

message(STATUS "Canonical logical damage traces match (${count_a} events)")
