include("${CMAKE_CURRENT_LIST_DIR}/TestRunnerCommon.cmake")

db_test_require_defined(TEST_BIN)
set(TEST_ARGS
    "--random-seed 123456 --bench-speed 1024 --benchmark-mode snake_grid --display glfw_window --glfw-hidden-window 1 --api opengl --renderer gl1_5_gles1_1 --resize-at-frame 2:1279x719 --frame-limit 4 --fps-cap 0 --trace-damage 1"
)
db_test_run_command(output skip_reason status "${TEST_ARGS}"
                    "GLFW resize contract command failed")
if(NOT "${skip_reason}" STREQUAL "")
    return()
endif()

foreach(
    required IN
    ITEMS "event=window_resize_request schema=2 frame=2"
          "event=presentation_resize schema=2 code=resize_observed"
          "event=frame_plan schema=2 frame=2"
          "event=frame_plan schema=2 frame=3")
    if(NOT output MATCHES "${required}")
        message(
            FATAL_ERROR
                "Missing resize contract output '${required}':\n${output}")
    endif()
endforeach()
foreach(frame 2 3)
    string(REGEX MATCH "event=frame_plan[^\n]*frame=${frame}[^\n]*" frame_event
                 "${output}")
    foreach(field_value IN ITEMS "simulation_ticks;1024" "backend;gl1"
                                 "target;gl1_backing" "target_generation;1")
        list(GET field_value 0 field)
        list(GET field_value 1 value)
        db_test_assert_field_equals("${frame_event}" "${field}" "${value}"
                                    "resize frame ${frame}")
    endforeach()
endforeach()
string(REGEX MATCH "event=presentation_resize[^\n]*" resize_event "${output}")
string(REGEX MATCHALL
             "event=presentation_contract[^\n]*destination_width=[^\n]*"
             presentation_events "${output}")
list(GET presentation_events -1 presentation_event)
db_test_assert_field_equals("${resize_event}" "new_window_width" "1279"
                            "presentation resize event")
db_test_assert_field_equals("${resize_event}" "new_window_height" "719"
                            "presentation resize event")
foreach(field new_framebuffer_width new_framebuffer_height content_scale_x
              content_scale_y)
    db_test_extract_field_or_empty("${resize_event}" "${field}" ${field})
    if("${${field}}" STREQUAL "" OR ${field} LESS_EQUAL 0)
        message(FATAL_ERROR "Invalid ${field} in resize event: ${resize_event}")
    endif()
endforeach()
math(EXPR expected_framebuffer_width "1279 * ${content_scale_x}")
math(EXPR expected_framebuffer_height "719 * ${content_scale_y}")
if(NOT new_framebuffer_width EQUAL expected_framebuffer_width
   OR NOT new_framebuffer_height EQUAL expected_framebuffer_height)
    message(
        FATAL_ERROR
            "Framebuffer extent does not match content scale: ${resize_event}")
endif()
db_test_assert_field_equals(
    "${presentation_event}" "destination_width" "${new_framebuffer_width}"
    "presentation contract after resize")
db_test_assert_field_equals(
    "${presentation_event}" "destination_height" "${new_framebuffer_height}"
    "presentation contract after resize")
if(output MATCHES "frame=(2|3)[^\n]*geometry_operation=rebuild")
    message(FATAL_ERROR "Resize invalidated logical backing:\n${output}")
endif()
if(output MATCHES "event=(info_message|error_message|fatal_error)")
    message(FATAL_ERROR "Resize path used compatibility logging:\n${output}")
endif()
