if(NOT DEFINED SOURCE_ROOT OR "${SOURCE_ROOT}" STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()

set(DB_IR_HEADER "${SOURCE_ROOT}/src/core/db_render_ir.h")
set(DB_IR_STORAGE "${SOURCE_ROOT}/src/core/db_render_ir.c")
set(DB_IR_QUERY "${SOURCE_ROOT}/src/core/db_render_ir_query.c")
set(DB_IR_RANGES "${SOURCE_ROOT}/src/core/db_render_ir_ranges.c")
set(DB_IR_OPTIMIZER "${SOURCE_ROOT}/src/core/db_render_ir_optimizer.c")
set(DB_BENCHMARK_CORE "${SOURCE_ROOT}/src/benchmarks/db_benchmark_core.c")
set(DB_CHECKPOINT "${SOURCE_ROOT}/src/benchmarks/db_benchmark_checkpoint.c")
set(DB_CHECKPOINT_HEADER
    "${SOURCE_ROOT}/src/benchmarks/db_benchmark_checkpoint_internal.h")
set(DB_VK_PLANNER
    "${SOURCE_ROOT}/src/renderers/vulkan_1_2_multi_gpu/vk_piece_plan.c")
set(DB_VK_RUNTIME_METRICS
    "${SOURCE_ROOT}/src/renderers/vulkan_1_2_multi_gpu/vk_runtime_metrics.c")
set(DB_VK_RUNTIME_FRAME
    "${SOURCE_ROOT}/src/renderers/vulkan_1_2_multi_gpu/vk_runtime_frame.c")
set(DB_VK_DEVICE_GROUP
    "${SOURCE_ROOT}/src/renderers/vulkan_1_2_multi_gpu/vk_device_group.c")
set(DB_DAMAGE_TRACE "${SOURCE_ROOT}/src/renderers/damage_trace.c")
set(DB_GL1_REPLAY
    "${SOURCE_ROOT}/src/renderers/opengl_gl1_5_gles1_1/gl1_replay.c")
set(DB_GL3_RENDERER "${SOURCE_ROOT}/src/renderers/opengl_gl3_3/gl3_renderer.c")
set(DB_VK_STATE
    "${SOURCE_ROOT}/src/renderers/vulkan_1_2_multi_gpu/vk_state_internal.h")
set(DB_QUALIFICATION_SERVICE "${SOURCE_ROOT}/src/core/db_conformance_service.c")

foreach(
    DB_IR_FILE IN
    ITEMS "${DB_IR_HEADER}"
          "${DB_IR_STORAGE}"
          "${DB_IR_QUERY}"
          "${DB_IR_RANGES}"
          "${DB_IR_OPTIMIZER}"
          "${DB_BENCHMARK_CORE}"
          "${DB_CHECKPOINT}"
          "${DB_CHECKPOINT_HEADER}"
          "${DB_VK_PLANNER}"
          "${DB_VK_RUNTIME_METRICS}"
          "${DB_VK_RUNTIME_FRAME}"
          "${DB_VK_DEVICE_GROUP}"
          "${DB_DAMAGE_TRACE}"
          "${DB_GL1_REPLAY}"
          "${DB_GL3_RENDERER}"
          "${DB_VK_STATE}"
          "${DB_QUALIFICATION_SERVICE}")
    if(NOT EXISTS "${DB_IR_FILE}")
        message(FATAL_ERROR "Missing semantic IR policy input: ${DB_IR_FILE}")
    endif()
endforeach()

file(READ "${DB_IR_HEADER}" DB_IR_HEADER_SOURCE)
string(REGEX MATCH "typedef struct \\{[^}]*\\} db_render_ir_command_header_t;"
             DB_IR_COMMAND_HEADER "${DB_IR_HEADER_SOURCE}")
if(DB_IR_COMMAND_HEADER MATCHES "max_align_t")
    message(FATAL_ERROR "Per-command IR header retains max_align_t padding")
endif()

file(READ "${DB_IR_STORAGE}" DB_IR_STORAGE_SOURCE)
if(DB_IR_STORAGE_SOURCE MATCHES
   "for[ \t\r\n]*\\([^)]*store->command_count[^)]*\\)"
   OR DB_IR_STORAGE_SOURCE MATCHES
      "while[ \t\r\n]*\\([^)]*offset[^)]*store->command_size[^)]*\\)")
    message(
        FATAL_ERROR
            "Last-command mutation reintroduced a linear command-arena scan")
endif()

file(READ "${DB_IR_RANGES}" DB_IR_RANGES_SOURCE)
string(
    REGEX
        MATCHALL
        "int[ \t\r\n]+db_render_ir_commands_batch_compatible_validated[ \t\r\n]*\\("
        DB_IR_COMPATIBILITY_DEFINITIONS
        "${DB_IR_RANGES_SOURCE}")
list(LENGTH DB_IR_COMPATIBILITY_DEFINITIONS DB_IR_COMPATIBILITY_COUNT)
if(NOT DB_IR_COMPATIBILITY_COUNT EQUAL 1)
    message(
        FATAL_ERROR
            "Expected one canonical validated command compatibility implementation, found ${DB_IR_COMPATIBILITY_COUNT}"
    )
endif()
string(REGEX MATCHALL
             "int[ \t\r\n]+db_render_ir_commands_batch_compatible[ \t\r\n]*\\("
             DB_IR_COMPATIBILITY_WRAPPERS "${DB_IR_RANGES_SOURCE}")
list(LENGTH DB_IR_COMPATIBILITY_WRAPPERS DB_IR_COMPATIBILITY_WRAPPER_COUNT)
if(NOT DB_IR_COMPATIBILITY_WRAPPER_COUNT EQUAL 1)
    message(
        FATAL_ERROR
            "Expected one checked public command compatibility wrapper, found ${DB_IR_COMPATIBILITY_WRAPPER_COUNT}"
    )
endif()

file(READ "${DB_IR_QUERY}" DB_IR_QUERY_SOURCE)
if(DB_IR_QUERY_SOURCE MATCHES
   "static[ \t\r\n]+int[ \t\r\n]+[^;(]*(compatible|compatibility)")
    message(FATAL_ERROR "IR query reintroduced local compatibility policy")
endif()
if(DB_IR_QUERY_SOURCE
   MATCHES
   "db_fnv1a64(_tree)?[ \t\r\n]*\\([^;]*(view->commands|command_size|sizeof[ \t]*\\([^)]*command)"
)
    message(FATAL_ERROR "IR hash uses native command storage or layout")
endif()

file(READ "${DB_IR_OPTIMIZER}" DB_IR_OPTIMIZER_SOURCE)
if(DB_IR_OPTIMIZER_SOURCE MATCHES
   "clip_region[ \t\r\n]*=[ \t\r\n]*command->clip_region")
    message(FATAL_ERROR "Optimizer reuses a raw-store clip region ID")
endif()
if(DB_IR_OPTIMIZER_SOURCE MATCHES
   "for[ \t\r\n]*\\([^)]*occluder[^)]*visible_count[^)]*\\)")
    message(
        FATAL_ERROR
            "Optimizer reintroduced pairwise visible-rectangle overwrite scans")
endif()
if(DB_IR_OPTIMIZER_SOURCE MATCHES "db_render_ir_add_fill_region[ \t\r\n]*\\(")
    message(
        FATAL_ERROR
            "Optimizer bypasses bounded coverage workspace for fill-region construction"
    )
endif()

file(READ "${DB_BENCHMARK_CORE}" DB_BENCHMARK_CORE_SOURCE)
if(DB_BENCHMARK_CORE_SOURCE MATCHES "db_benchmark_emit_gradient[ \t\r\n]*\\(")
    message(
        FATAL_ERROR
            "Benchmark core reintroduced temporary gradient row-fill generation"
    )
endif()

file(READ "${DB_CHECKPOINT}" DB_CHECKPOINT_SOURCE)
file(READ "${DB_CHECKPOINT_HEADER}" DB_CHECKPOINT_HEADER_SOURCE)
if(DB_CHECKPOINT_SOURCE MATCHES "overlay_dirty_indices|db_sort_u32_ascending"
   OR DB_CHECKPOINT_HEADER_SOURCE MATCHES "overlay_dirty_indices")
    message(
        FATAL_ERROR
            "Checkpoint reintroduced dirty-pixel indexing or commit-time sorting"
    )
endif()

file(READ "${DB_VK_PLANNER}" DB_VK_PLANNER_SOURCE)
if(DB_VK_PLANNER_SOURCE MATCHES "\\[[ \t]*DB_VK_MAX_PIECES_PER_FRAME[ \t]*\\]")
    message(FATAL_ERROR "Vulkan planner retains a large local frame array")
endif()

file(READ "${DB_VK_RUNTIME_METRICS}" DB_VK_RUNTIME_METRICS_SOURCE)
if(DB_VK_RUNTIME_METRICS_SOURCE MATCHES
   "memcpy[ \t\r\n]*\\([ \t\r\n]*mapped[ \t\r\n]*,[ \t\r\n]*binding->pixels")
    message(FATAL_ERROR "Vulkan rebuild upload bypasses strided row packing")
endif()
if(NOT DB_VK_RUNTIME_METRICS_SOURCE MATCHES
   "db_copy_strided_rows_tight[ \t\r\n]*\\(")
    message(FATAL_ERROR "Vulkan rebuild upload must use checked row packing")
endif()

file(READ "${DB_VK_RUNTIME_FRAME}" DB_VK_RUNTIME_FRAME_SOURCE)
if(DB_VK_RUNTIME_FRAME_SOURCE MATCHES
   "external_bindings[.]bindings[^;]*\\[[ \t\r\n]*0[Uu]?[ \t\r\n]*\\]")
    message(
        FATAL_ERROR
            "Vulkan rebuild binding must resolve the upload resource instead of assuming binding zero"
    )
endif()
if(DB_VK_RUNTIME_FRAME_SOURCE MATCHES "db_vk_frame_rect_count[ \t\r\n]*\\(")
    message(
        FATAL_ERROR
            "Vulkan native frame execution reintroduced flattened row counting")
endif()
if(NOT DB_VK_RUNTIME_FRAME_SOURCE MATCHES
   "db_vk_route_accelerated_gradients_to_primary[ \t\r\n]*\\(")
    message(
        FATAL_ERROR
            "Independent Vulkan execution must not send accelerated gradients to its legacy rectangle worker"
    )
endif()

file(READ "${DB_VK_DEVICE_GROUP}" DB_VK_DEVICE_GROUP_SOURCE)
if(DB_VK_DEVICE_GROUP_SOURCE MATCHES "db_vk_frame_rect_count[ \t\r\n]*\\(")
    message(
        FATAL_ERROR
            "Vulkan device-group execution reintroduced flattened row counting")
endif()

file(READ "${DB_DAMAGE_TRACE}" DB_DAMAGE_TRACE_SOURCE)
if(DB_DAMAGE_TRACE_SOURCE MATCHES "coverage_map|coverage\\[[^]]*\\]")
    message(
        FATAL_ERROR "Damage trace reintroduced a target-sized coverage scan")
endif()
if(DB_VK_PLANNER_SOURCE MATCHES "for[ \t\r\n]*\\([^)]*prior[^)]*\\)")
    message(FATAL_ERROR "Vulkan planner reintroduced pairwise overlap scans")
endif()

file(READ "${DB_VK_STATE}" DB_VK_STATE_SOURCE)
string(REGEX MATCHALL "DeviceSelectionState[ \t]+selection"
             DB_VK_ENUMERATION_STORES "${DB_VK_STATE_SOURCE}")
list(LENGTH DB_VK_ENUMERATION_STORES DB_VK_ENUMERATION_STORE_COUNT)
if(NOT DB_VK_ENUMERATION_STORE_COUNT EQUAL 1)
    message(
        FATAL_ERROR
            "Vulkan enumeration state must exist only in initialization scratch"
    )
endif()

file(READ "${DB_QUALIFICATION_SERVICE}" DB_QUALIFICATION_SERVICE_SOURCE)
if(DB_QUALIFICATION_SERVICE_SOURCE
   MATCHES
   "(db_conformance_key_t|db_conformance_decision_t|size_t)[ \t]+[A-Za-z_][A-Za-z0-9_]*\\[[ \t]*DB_(QUALIFICATION|CONFORMANCE)_[A-Za-z0-9_]*MAX_[A-Za-z0-9_]*"
)
    message(
        FATAL_ERROR
            "Qualification resolver reintroduced oversized local descriptor scratch"
    )
endif()

file(READ "${DB_GL1_REPLAY}" DB_GL1_REPLAY_SOURCE)
string(FIND "${DB_GL1_REPLAY_SOURCE}"
            "void db_gl1_replay_publish_pending(void)" DB_GL1_PUBLISH_OFFSET)
if(DB_GL1_PUBLISH_OFFSET LESS 0)
    message(FATAL_ERROR "Missing GL1 replay publication implementation")
endif()
string(SUBSTRING "${DB_GL1_REPLAY_SOURCE}" ${DB_GL1_PUBLISH_OFFSET} -1
                 DB_GL1_PUBLISH_SOURCE)
if(DB_GL1_PUBLISH_SOURCE MATCHES
   "(malloc|calloc|realloc|snapshot_init|clone_replayable)[ \t\r\n]*\\(")
    message(
        FATAL_ERROR
            "GL1 replay publication performs fallible work after presentation")
endif()

file(READ "${DB_GL3_RENDERER}" DB_GL3_RENDERER_SOURCE)
if(DB_GL3_RENDERER_SOURCE MATCHES
   "db_init_vertices_for_execution_config[ \t\r\n]*\\(")
    message(
        FATAL_ERROR
            "GL3 reintroduced six-vertex-per-rectangle compatibility storage")
endif()

message(STATUS "Semantic IR bounded-execution policy passed")
