if(NOT DEFINED SOURCE_ROOT)
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()

set(db_hash_root "${SOURCE_ROOT}/src/core")
file(GLOB db_hash_sources "${db_hash_root}/db_hash*.c"
     "${db_hash_root}/db_hash*.h")
foreach(db_source IN LISTS db_hash_sources)
    file(READ "${db_source}" db_text)
    foreach(
        db_forbidden IN
        ITEMS db_fnv_blockhash_u64 DB_FNV1A32_OFFSET DB_FNV1A32_PRIME
              vreinterpretq_u64_u32 db_x86_pack4_u32_to2_u64_sse41
              db_x86_pack8_u32_to4_u64_avx2)
        string(FIND "${db_text}" "${db_forbidden}" db_match)
        if(NOT db_match EQUAL -1)
            message(
                FATAL_ERROR
                    "Forbidden legacy/native-layout hash pattern '${db_forbidden}' in ${db_source}"
            )
        endif()
    endforeach()
endforeach()

file(READ "${db_hash_root}/db_hash_simd.c" db_tree_source)
foreach(
    db_required IN
    ITEMS DB_FNV_TREE_LEAF_TAG DB_FNV_TREE_PARENT_TAG DB_FNV_TREE_UNARY_TAG
          DB_FNV_TREE_ROOT_TAG db_fnv_tree_put_u32_le db_fnv_tree_put_u64_le)
    string(FIND "${db_tree_source}" "${db_required}" db_match)
    if(db_match EQUAL -1)
        message(
            FATAL_ERROR
                "Required typed tree-hash contract '${db_required}' is missing")
    endif()
endforeach()

file(READ "${db_hash_root}/db_hash_simd_x86.c" db_x86_source)
string(FIND "${db_x86_source}" "target(\"sse2\")" db_sse2_target)
string(FIND "${db_x86_source}" "__builtin_cpu_supports(\"avx2\")" db_avx2_guard)
if(db_sse2_target EQUAL -1 OR db_avx2_guard EQUAL -1)
    message(
        FATAL_ERROR "x86 tree hash must retain guarded SSE2 and AVX2 kernels")
endif()

foreach(db_simd_source IN ITEMS db_tree_source db_x86_source)
    string(FIND "${${db_simd_source}}" "* DB_FNV1A64_PRIME" db_prime_multiply)
    if(NOT db_prime_multiply EQUAL -1)
        message(
            FATAL_ERROR
                "SIMD tree kernels must implement FNV prime multiplication with shift/add"
        )
    endif()
endforeach()
