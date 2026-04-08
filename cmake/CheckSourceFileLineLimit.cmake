if(NOT DEFINED SOURCE_ROOT)
  message(FATAL_ERROR "SOURCE_ROOT is required")
endif()
if(NOT DEFINED LINE_LIMIT)
  set(LINE_LIMIT 1000)
endif()

set(DB_SCOPED_FILES
  "${SOURCE_ROOT}/CMakeLists.txt"
)

file(GLOB DB_CMAKE_FILES
  "${SOURCE_ROOT}/cmake/*.cmake")
file(GLOB_RECURSE DB_SOURCE_FILES
  "${SOURCE_ROOT}/src/*.c"
  "${SOURCE_ROOT}/src/*.h")

list(APPEND DB_SCOPED_FILES
  ${DB_CMAKE_FILES}
  ${DB_SOURCE_FILES})

set(DB_VIOLATIONS "")
foreach(DB_FILE IN LISTS DB_SCOPED_FILES)
  file(STRINGS "${DB_FILE}" DB_LINES)
  list(LENGTH DB_LINES DB_LINE_COUNT)
  if(DB_LINE_COUNT GREATER LINE_LIMIT)
    file(RELATIVE_PATH DB_REL_FILE "${SOURCE_ROOT}" "${DB_FILE}")
    string(APPEND DB_VIOLATIONS "${DB_REL_FILE}: ${DB_LINE_COUNT}\n")
  endif()
endforeach()

if(NOT DB_VIOLATIONS STREQUAL "")
  message(FATAL_ERROR
    "Source file line limit exceeded (limit=${LINE_LIMIT}):\n${DB_VIOLATIONS}")
endif()

message(STATUS "All scoped source files are within ${LINE_LIMIT} lines.")
