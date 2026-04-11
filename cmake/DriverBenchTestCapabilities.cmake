function(db_configure_test_runtime_capabilities)
    if(APPLE)
        set(DB_TEST_RUNTIME_GL1_DIRTY_TRACE_ARGS
            "${DB_DETERMINISM_GL1_OFFSCREEN_PREFIX} --benchmark-mode snake_grid ${DB_DETERMINISM_COMMON_ARGS} --bench-speed 1024 --frame-limit 3 --backbuffer-draw-mode dirty --trace-gl-errors 1 --trace-damage 1 --trace-shadow-upload 1"
            PARENT_SCOPE)
        set(DB_TEST_RUNTIME_GL1_FULL_TRACE_ARGS
            "${DB_DETERMINISM_GL1_OFFSCREEN_PREFIX} --benchmark-mode snake_grid ${DB_DETERMINISM_COMMON_ARGS} --bench-speed 1024 --frame-limit 5 --backbuffer-draw-mode full --trace-gl-errors 1 --trace-damage 1 --trace-shadow-upload 1"
            PARENT_SCOPE)
        set(DB_TEST_RUNTIME_GL1_DIRTY_SNAKE_SHAPES_CONTRACT_ARGS
            "${DB_DETERMINISM_GL1_OFFSCREEN_PREFIX} --benchmark-mode snake_shapes ${DB_DETERMINISM_COMMON_ARGS} --bench-speed 1024 --frame-limit 4 --backbuffer-draw-mode dirty"
            PARENT_SCOPE)
    else()
        set(DB_TEST_RUNTIME_GL1_DIRTY_TRACE_ARGS
            ""
            PARENT_SCOPE)
        set(DB_TEST_RUNTIME_GL1_FULL_TRACE_ARGS
            ""
            PARENT_SCOPE)
        set(DB_TEST_RUNTIME_GL1_DIRTY_SNAKE_SHAPES_CONTRACT_ARGS
            ""
            PARENT_SCOPE)
    endif()
endfunction()
