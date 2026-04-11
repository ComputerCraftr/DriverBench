include("${CMAKE_CURRENT_LIST_DIR}/TestRunnerCommon.cmake")

foreach(required TEST_BIN TEST_ARGS TEST_BACKEND TEST_TARGET
                 TEST_PRESENT_METHOD)
    db_test_require_defined(${required})
endforeach()
db_test_run_command(output skip_reason status "${TEST_ARGS}"
                    "Persistent-target trace command failed")
if(NOT "${skip_reason}" STREQUAL "")
    message(STATUS "Persistent-target trace skipped: ${skip_reason}")
    return()
endif()

function(db_find_event event frame out_line)
    string(REPLACE "\r" "" normalized "${output}")
    string(REPLACE "\n" ";" lines "${normalized}")
    foreach(line IN LISTS lines)
        if(line MATCHES "(^| )event=${event}( |$)"
           AND line MATCHES "(^| )backend=${TEST_BACKEND}( |$)"
           AND line MATCHES "(^| )frame=${frame}( |$)")
            set(${out_line}
                "${line}"
                PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${out_line}
        ""
        PARENT_SCOPE)
endfunction()

db_find_event("frame_plan" 0 frame_zero)
db_find_event("frame_plan" 1 frame_one)
if(frame_zero STREQUAL "" OR frame_one STREQUAL "")
    message(FATAL_ERROR "Missing frame_plan events:\n${output}")
endif()
foreach(line frame_zero frame_one)
    db_test_assert_field_equals("${${line}}" "target" "${TEST_TARGET}"
                                "${line}")
endforeach()
db_test_assert_field_equals("${frame_zero}" "geometry_operation" "rebuild"
                            "frame zero plan")
db_test_assert_field_equals("${frame_one}" "geometry_operation" "incremental"
                            "frame one plan")
db_test_assert_field_equals("${frame_zero}" "rebuild_required" "true"
                            "frame zero plan")
db_test_assert_field_equals("${frame_one}" "rebuild_required" "false"
                            "frame one plan")

if(frame_zero MATCHES "(^| )target_generation=([^ ]+)")
    set(generation_zero "${CMAKE_MATCH_2}")
else()
    message(FATAL_ERROR "Frame zero has no target generation: ${frame_zero}")
endif()
db_test_assert_field_equals("${frame_one}" "target_generation"
                            "${generation_zero}" "stable target generation")

if(NOT "${TEST_PRESENT_METHOD}" STREQUAL "none")
    string(REPLACE "\r" "" normalized "${output}")
    string(REPLACE "\n" ";" lines "${normalized}")
    set(found_present FALSE)
    foreach(line IN LISTS lines)
        if(line MATCHES "(^| )event=damage_summary( |$)"
           AND line MATCHES "(^| )backend=${TEST_BACKEND}( |$)"
           AND line MATCHES "(^| )stage=present( |$)"
           AND line MATCHES "(^| )present_method=${TEST_PRESENT_METHOD}( |$)")
            set(found_present TRUE)
        endif()
        if(line MATCHES "(^| )src=vk_image( |$)"
           AND line MATCHES "(^| )dst=vk_image( |$)"
           AND line MATCHES "(^| )op=copy( |$)")
            message(
                FATAL_ERROR "Redundant Vulkan backing copy observed:\n${line}")
        endif()
    endforeach()
    if(NOT found_present)
        message(
            FATAL_ERROR
                "Missing present_method=${TEST_PRESENT_METHOD}:\n${output}")
    endif()
endif()

message(STATUS "Persistent target contract passed: backend=${TEST_BACKEND} "
               "generation=${generation_zero} method=${TEST_PRESENT_METHOD}")
