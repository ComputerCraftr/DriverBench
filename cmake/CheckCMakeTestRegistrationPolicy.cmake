if(NOT DEFINED SOURCE_ROOT OR "${SOURCE_ROOT}" STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()

set(DB_ALLOWED_TEST_REGISTRATION_FILES
    "${SOURCE_ROOT}/cmake/DriverBenchTests.cmake"
    "${SOURCE_ROOT}/cmake/DriverBenchTestHelpers.cmake"
    "${SOURCE_ROOT}/cmake/DriverBenchDeterminismSuites.cmake"
    "${SOURCE_ROOT}/cmake/CheckCMakeTestRegistrationPolicy.cmake")

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
            "${DB_CMAKE_FILE}: add_test(...) registrations must live in cmake/DriverBenchTests.cmake\n"
        )
    endif()
endforeach()

set(DB_DRIVERBENCH_TESTS_CONTENT "")
foreach(DB_REGISTRATION_FILE IN LISTS DB_ALLOWED_TEST_REGISTRATION_FILES)
    if(DB_REGISTRATION_FILE STREQUAL
       "${SOURCE_ROOT}/cmake/CheckCMakeTestRegistrationPolicy.cmake")
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

if(NOT DB_FAILURES STREQUAL "")
    message(FATAL_ERROR "${DB_FAILURES}")
endif()
