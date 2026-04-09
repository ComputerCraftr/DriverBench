if(NOT DEFINED SOURCE_ROOT OR "${SOURCE_ROOT}" STREQUAL "")
  message(FATAL_ERROR "SOURCE_ROOT is required")
endif()

set(DB_ALLOWED_TEST_REGISTRATION_FILES
  "${SOURCE_ROOT}/cmake/DriverBenchTests.cmake"
  "${SOURCE_ROOT}/cmake/CheckCMakeTestRegistrationPolicy.cmake"
)

set(DB_CMAKE_FILES
  "${SOURCE_ROOT}/CMakeLists.txt"
)
file(GLOB DB_PROJECT_CMAKE_FILES "${SOURCE_ROOT}/cmake/*.cmake")
list(APPEND DB_CMAKE_FILES ${DB_PROJECT_CMAKE_FILES})

set(DB_FAILURES "")

foreach(DB_CMAKE_FILE IN LISTS DB_CMAKE_FILES)
  list(FIND DB_ALLOWED_TEST_REGISTRATION_FILES "${DB_CMAKE_FILE}" DB_ALLOWED_INDEX)
  if(NOT DB_ALLOWED_INDEX EQUAL -1)
    continue()
  endif()

  file(READ "${DB_CMAKE_FILE}" DB_FILE_CONTENT)
  if(DB_FILE_CONTENT MATCHES "(^|[^A-Za-z0-9_])add_test[ \t\r\n]*\\(")
    string(APPEND DB_FAILURES
      "${DB_CMAKE_FILE}: add_test(...) registrations must live in cmake/DriverBenchTests.cmake\n")
  endif()
endforeach()

if(NOT DB_FAILURES STREQUAL "")
  message(FATAL_ERROR "${DB_FAILURES}")
endif()
