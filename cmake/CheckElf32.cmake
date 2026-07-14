if(NOT DEFINED TEST_BIN OR NOT DEFINED READELF_BIN)
    message(FATAL_ERROR "TEST_BIN and READELF_BIN are required")
endif()

execute_process(
    COMMAND "${READELF_BIN}" -h "${TEST_BIN}"
    RESULT_VARIABLE readelf_status
    OUTPUT_VARIABLE readelf_output
    ERROR_VARIABLE readelf_error)
if(NOT readelf_status EQUAL 0)
    message(FATAL_ERROR "readelf failed: ${readelf_error}")
endif()
if(NOT readelf_output MATCHES "Class:[ \t]+ELF32")
    message(
        FATAL_ERROR
            "sanitizer activation binary is not ELF32:\n${readelf_output}")
endif()
