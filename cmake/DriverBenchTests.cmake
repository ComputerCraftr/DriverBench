include("${CMAKE_CURRENT_LIST_DIR}/TestRunnerCommon.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/DriverBenchTestTimeouts.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/DriverBenchTestCapabilities.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/DriverBenchAlternateSuites.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/DriverBenchTestHelpers.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/DriverBenchDeterminismSuites.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/DriverBenchRuntimeRegressionTests.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/DriverBenchSourcePolicyTests.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/DriverBenchUnitProbeTests.cmake")

function(db_register_driverbench_tests)
    if(NOT DB_IS_RELEASE_LIKE)
        message(
            STATUS
                "Non-release build detected: full ctest may be slow; Release is the "
                "default validation target for renderer performance regressions."
        )
    endif()

    db_register_source_policy_tests()
    db_register_unit_and_probe_tests()

    set(DB_DETERMINISM_CPU_OFFSCREEN_PREFIX "--api cpu --display offscreen")
    set(DB_DETERMINISM_GL1_OFFSCREEN_PREFIX
        "--api opengl --renderer gl1_5_gles1_1 --display offscreen --vsync 0")
    set(DB_DETERMINISM_GL3_OFFSCREEN_PREFIX
        "--api opengl --renderer gl3_3 --display offscreen --vsync 0")
    set(DB_DETERMINISM_COMMON_ARGS
        "--random-seed 123456 --fps-cap 0 --working-format rgba16f --output-format sdr"
    )
    db_configure_test_runtime_capabilities()
    db_discover_alternate_release_suites()

    set(DB_TEST_SUITE_IDS "")
    set(db_native_glfw_enabled OFF)
    if(NOT DB_TEST_HEADLESS_ONLY
       AND DB_BUILD_GLFW_WINDOW_DISPLAY
       AND NOT (DB_GLFW_LINK_LIB STREQUAL ""))
        set(db_native_glfw_enabled ON)
    endif()
    if(DB_TEST_HEADLESS_ONLY)
        message(
            STATUS
                "Headless CTest registration enabled: native-window and graphics-runtime tests are omitted"
        )
    endif()
    find_program(DB_GOLDEN_UPDATE_PYTHON NAMES python3)
    if(DB_GOLDEN_UPDATE_PYTHON)
        set(db_native_vulkan_enabled OFF)
        if(db_native_glfw_enabled
           AND DB_BUILD_VULKAN
           AND DB_VULKAN_LIB)
            set(db_native_vulkan_enabled ON)
        endif()
        add_custom_target(
            update_canonical_goldens
            COMMAND
                ${DB_GOLDEN_UPDATE_PYTHON}
                ${CMAKE_SOURCE_DIR}/scripts/update_canonical_goldens.py --binary
                $<TARGET_FILE:${DB_UNIFIED_TARGET}> --manifest
                ${CMAKE_SOURCE_DIR}/cmake/DriverBenchCanonicalGoldens.cmake
                --runner
                ${CMAKE_SOURCE_DIR}/cmake/RunCanonicalDeterminismMatrix.cmake
                --cmake ${CMAKE_COMMAND} --glfw-enabled
                ${db_native_glfw_enabled} --vulkan-enabled
                ${db_native_vulkan_enabled}
            DEPENDS ${DB_UNIFIED_TARGET}
            USES_TERMINAL)
    endif()
    db_define_release_suite(
        native "" "$<TARGET_FILE:${DB_UNIFIED_TARGET}>"
        "$<TARGET_FILE:driverbench_unit_tests>" "" "${db_native_glfw_enabled}")

    foreach(db_alt_lane_id IN LISTS DB_TEST_ALTERNATE_LANE_IDS)
        if(DB_TEST_ALT_${db_alt_lane_id}_ENABLED)
            set(db_alt_glfw_enabled
                "${DB_TEST_ALT_${db_alt_lane_id}_GLFW_AVAILABLE}")
            if(DB_TEST_HEADLESS_ONLY)
                set(db_alt_glfw_enabled OFF)
            endif()
            db_define_release_suite(
                "${db_alt_lane_id}"
                "alt_${db_alt_lane_id}_"
                "${DB_TEST_ALT_${db_alt_lane_id}_BINARY}"
                "${DB_TEST_ALT_${db_alt_lane_id}_UNIT_BINARY}"
                "alternate;${db_alt_lane_id}"
                "${db_alt_glfw_enabled}")
        endif()
    endforeach()

    foreach(db_suite_id IN LISTS DB_TEST_SUITE_IDS)
        db_register_release_suite("${db_suite_id}")
    endforeach()
    db_test_finalize_skip_contract()
endfunction()
