set(DB_PLATFORM_IS_APPLE OFF)
if(APPLE)
  set(DB_PLATFORM_IS_APPLE ON)
endif()

set(DB_PLATFORM_IS_NON_APPLE_UNIX OFF)
if(UNIX AND NOT APPLE)
  set(DB_PLATFORM_IS_NON_APPLE_UNIX ON)
endif()

function(db_configure_platform_linker_preference)
  set(DB_USE_LLD OFF PARENT_SCOPE)
  if(NOT DB_EFFECTIVE_LTO OR NOT DB_COMPILER_IS_CLANG)
    return()
  endif()

  if(DB_PLATFORM_IS_APPLE)
    message(STATUS
      "Apple build with LTO: keeping the platform linker; skipping ld.lld "
      "to avoid known ld64.lld crashes on macOS arm64."
    )
    return()
  endif()

  find_program(DB_LLD_PROGRAM NAMES ld.lld lld)
  if(DB_LLD_PROGRAM)
    set(DB_USE_LLD ON PARENT_SCOPE)
  endif()
endfunction()

function(db_apply_platform_target_options target_name)
  if(NOT TARGET ${target_name})
    return()
  endif()

  if(DB_PLATFORM_IS_NON_APPLE_UNIX)
    target_compile_definitions(${target_name} PRIVATE _POSIX_C_SOURCE=200809L)
  endif()

  if(DB_ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(DB_COMPILER_IS_GNU_LIKE)
      set(DB_SANITIZER_FLAGS -fsanitize=address,undefined)
      if(NOT DB_PLATFORM_IS_APPLE)
        list(APPEND DB_SANITIZER_FLAGS -fsanitize=leak)
      endif()
      target_compile_options(${target_name} PRIVATE
        -fno-omit-frame-pointer
        ${DB_SANITIZER_FLAGS}
      )
      target_link_options(${target_name} PRIVATE ${DB_SANITIZER_FLAGS})
    endif()
  endif()

endfunction()

function(db_apply_platform_executable_postbuild_options target_name)
  if(NOT TARGET ${target_name})
    return()
  endif()

  get_target_property(DB_TARGET_TYPE ${target_name} TYPE)
  if(NOT DB_TARGET_TYPE STREQUAL "EXECUTABLE")
    return()
  endif()

  if(DB_PLATFORM_IS_APPLE AND DB_ENABLE_DSYM AND
     CMAKE_BUILD_TYPE MATCHES "^(Debug|RelWithDebInfo)$")
    find_program(DB_DSYMUTIL_PROGRAM NAMES dsymutil xcrun)
    if(DB_DSYMUTIL_PROGRAM)
      if(DB_DSYMUTIL_PROGRAM MATCHES ".*/xcrun$")
        add_custom_command(TARGET ${target_name} POST_BUILD
          COMMAND ${DB_DSYMUTIL_PROGRAM} dsymutil
                  $<TARGET_FILE:${target_name}>
                  -o $<TARGET_FILE:${target_name}>.dSYM
          COMMENT "Generating dSYM for ${target_name}"
          VERBATIM
        )
      else()
        add_custom_command(TARGET ${target_name} POST_BUILD
          COMMAND ${DB_DSYMUTIL_PROGRAM}
                  $<TARGET_FILE:${target_name}>
                  -o $<TARGET_FILE:${target_name}>.dSYM
          COMMENT "Generating dSYM for ${target_name}"
          VERBATIM
        )
      endif()
    endif()
  endif()
endfunction()
