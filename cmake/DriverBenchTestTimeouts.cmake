# Test-runner deadlines are intentionally separate from runtime wait profiles.
set(DB_TEST_TIMEOUT_ALTERNATE_PROBE_SECONDS 10)
set(DB_TEST_TIMEOUT_DETERMINISM_SHORT_SECONDS 10)
set(DB_TEST_TIMEOUT_DETERMINISM_GPU_SECONDS 15)
set(DB_TEST_TIMEOUT_LINT_SECONDS 60)
set(DB_TEST_TIMEOUT_QEMU_HASH_SECONDS 30)
set(DB_TEST_TIMEOUT_PROBE_PROCESS_SECONDS 10)

function(db_test_timeout_seconds out_var profile)
    if(profile STREQUAL "alternate_probe")
        set(db_seconds "${DB_TEST_TIMEOUT_ALTERNATE_PROBE_SECONDS}")
    elseif(profile STREQUAL "determinism_short")
        set(db_seconds "${DB_TEST_TIMEOUT_DETERMINISM_SHORT_SECONDS}")
    elseif(profile STREQUAL "determinism_gpu")
        set(db_seconds "${DB_TEST_TIMEOUT_DETERMINISM_GPU_SECONDS}")
    elseif(profile STREQUAL "lint")
        set(db_seconds "${DB_TEST_TIMEOUT_LINT_SECONDS}")
    elseif(profile STREQUAL "qemu_hash")
        set(db_seconds "${DB_TEST_TIMEOUT_QEMU_HASH_SECONDS}")
    elseif(profile STREQUAL "probe_process")
        set(db_seconds "${DB_TEST_TIMEOUT_PROBE_PROCESS_SECONDS}")
    else()
        message(
            FATAL_ERROR "Unknown DriverBench test timeout profile: ${profile}")
    endif()
    set(${out_var}
        "${db_seconds}"
        PARENT_SCOPE)
endfunction()
