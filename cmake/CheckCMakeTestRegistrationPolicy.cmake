if(NOT DEFINED SOURCE_ROOT OR "${SOURCE_ROOT}" STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()

set(DB_TEST_REGISTRATION_POLICY_FILE "${CMAKE_CURRENT_LIST_FILE}")
set(DB_ALLOWED_TEST_REGISTRATION_FILES
    "${SOURCE_ROOT}/cmake/DriverBenchTests.cmake"
    "${SOURCE_ROOT}/cmake/DriverBenchRuntimeRegressionTests.cmake"
    "${SOURCE_ROOT}/cmake/DriverBenchSourcePolicyTests.cmake"
    "${SOURCE_ROOT}/cmake/DriverBenchTestHelpers.cmake"
    "${SOURCE_ROOT}/cmake/DriverBenchDeterminismSuites.cmake"
    "${SOURCE_ROOT}/cmake/DriverBenchUnitProbeTests.cmake"
    "${DB_TEST_REGISTRATION_POLICY_FILE}")

set(DB_CMAKE_FILES "${SOURCE_ROOT}/CMakeLists.txt")
file(GLOB DB_PROJECT_CMAKE_FILES "${SOURCE_ROOT}/cmake/*.cmake")
list(APPEND DB_CMAKE_FILES ${DB_PROJECT_CMAKE_FILES})

set(DB_FAILURES "")

foreach(DB_CMAKE_FILE IN LISTS DB_CMAKE_FILES)
    list(FIND DB_ALLOWED_TEST_REGISTRATION_FILES "${DB_CMAKE_FILE}"
         DB_ALLOWED_INDEX)
    if(NOT DB_ALLOWED_INDEX EQUAL -1)
        continue()
    endif()

    file(READ "${DB_CMAKE_FILE}" DB_FILE_CONTENT)
    if(DB_FILE_CONTENT MATCHES "(^|[^A-Za-z0-9_])add_test[ \t\r\n]*\\(")
        string(
            APPEND
            DB_FAILURES
            "${DB_CMAKE_FILE}: add_test(...) registrations must live in an approved DriverBench test-registration module\n"
        )
    endif()
endforeach()

set(DB_DRIVERBENCH_TESTS_CONTENT "")
foreach(DB_REGISTRATION_FILE IN LISTS DB_ALLOWED_TEST_REGISTRATION_FILES)
    if(DB_REGISTRATION_FILE STREQUAL "${DB_TEST_REGISTRATION_POLICY_FILE}")
        continue()
    endif()
    file(READ "${DB_REGISTRATION_FILE}" DB_REGISTRATION_CONTENT)
    string(APPEND DB_DRIVERBENCH_TESTS_CONTENT "\n${DB_REGISTRATION_CONTENT}")
endforeach()
string(REGEX MATCHALL "\"[^\"]*--display glfw_window[^\"]*\"" DB_GLFW_TEST_ARGS
             "${DB_DRIVERBENCH_TESTS_CONTENT}")
foreach(DB_GLFW_TEST_ARG IN LISTS DB_GLFW_TEST_ARGS)
    if(NOT DB_GLFW_TEST_ARG MATCHES "--api[ \t]+")
        continue()
    endif()
    if(NOT DB_GLFW_TEST_ARG MATCHES "--glfw-hidden-window[ \t]+1")
        string(
            APPEND
            DB_FAILURES
            "CTest registration: glfw_window args must include --glfw-hidden-window 1\n"
        )
    endif()
endforeach()

file(READ "${SOURCE_ROOT}/cmake/RunGlfwResizeContractTest.cmake"
     DB_GLFW_RESIZE_TEST_CONTENT)
if(NOT DB_GLFW_RESIZE_TEST_CONTENT MATCHES "--glfw-hidden-window[ 	]+1")
    string(APPEND DB_FAILURES
           "GLFW resize CTest must keep its native test surface hidden\n")
endif()

file(GLOB DB_TEST_RUNNERS "${SOURCE_ROOT}/cmake/Run*.cmake")
foreach(DB_TEST_RUNNER IN LISTS DB_TEST_RUNNERS)
    file(READ "${DB_TEST_RUNNER}" DB_TEST_RUNNER_CONTENT)
    if(DB_TEST_RUNNER_CONTENT MATCHES "message[ \t\r\n]*\\([^\\)]*[Ss]kipp")
        string(
            APPEND
            DB_FAILURES
            "${DB_TEST_RUNNER}: ad hoc skip messages are forbidden; use db_test_report_skip\n"
        )
    endif()
endforeach()

if(NOT DB_FAILURES STREQUAL "")
    message(FATAL_ERROR "${DB_FAILURES}")
endif()
