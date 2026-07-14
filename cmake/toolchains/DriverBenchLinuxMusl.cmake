set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES DB_TARGET_LINUX_MUSL_TRIPLE
     DB_TARGET_LINUX_ROOT)

if(NOT DEFINED DB_TARGET_LINUX_MUSL_TRIPLE OR DB_TARGET_LINUX_MUSL_TRIPLE
                                              STREQUAL "")
    message(
        FATAL_ERROR "The musl toolchain requires DB_TARGET_LINUX_MUSL_TRIPLE.")
endif()

if(DB_TARGET_LINUX_MUSL_TRIPLE MATCHES "^x86_64-")
    set(CMAKE_SYSTEM_PROCESSOR x86_64)
elseif(DB_TARGET_LINUX_MUSL_TRIPLE MATCHES "^i[3-6]86-")
    set(CMAKE_SYSTEM_PROCESSOR i686)
endif()

find_program(DB_MUSL_TARGET_GCC NAMES "${DB_TARGET_LINUX_MUSL_TRIPLE}-gcc")
if(DB_MUSL_TARGET_GCC)
    set(CMAKE_C_COMPILER
        "${DB_MUSL_TARGET_GCC}"
        CACHE FILEPATH "Resolved musl target C compiler" FORCE)
else()
    find_program(DB_MUSL_CLANG NAMES clang REQUIRED)
    find_program(
        DB_MUSL_LLVM_AR
        NAMES llvm-ar ar REQUIRED
        NO_CMAKE_FIND_ROOT_PATH)
    find_program(
        DB_MUSL_LLVM_RANLIB
        NAMES llvm-ranlib ranlib REQUIRED
        NO_CMAKE_FIND_ROOT_PATH)
    set(CMAKE_C_COMPILER
        "${DB_MUSL_CLANG}"
        CACHE FILEPATH "Clang used for the musl target" FORCE)
    set(CMAKE_C_COMPILER_TARGET
        "${DB_TARGET_LINUX_MUSL_TRIPLE}"
        CACHE STRING "Clang musl target triple" FORCE)
    set(CMAKE_AR
        "${DB_MUSL_LLVM_AR}"
        CACHE FILEPATH "Archiver used for the Clang musl target" FORCE)
    set(CMAKE_RANLIB
        "${DB_MUSL_LLVM_RANLIB}"
        CACHE FILEPATH "Ranlib used for the Clang musl target" FORCE)
endif()

if(DEFINED DB_TARGET_LINUX_ROOT AND NOT DB_TARGET_LINUX_ROOT STREQUAL "")
    set(DB_MUSL_ROOT "${DB_TARGET_LINUX_ROOT}")
elseif(NOT "$ENV{DB_TARGET_LINUX_ROOT}" STREQUAL "")
    set(DB_MUSL_ROOT "$ENV{DB_TARGET_LINUX_ROOT}")
elseif(EXISTS "/usr/${DB_TARGET_LINUX_MUSL_TRIPLE}")
    set(DB_MUSL_ROOT "/usr/${DB_TARGET_LINUX_MUSL_TRIPLE}")
endif()

if(DEFINED DB_MUSL_ROOT)
    set(DB_TARGET_LINUX_ROOT
        "${DB_MUSL_ROOT}"
        CACHE PATH "Resolved musl target root" FORCE)
    set(CMAKE_SYSROOT
        "${DB_MUSL_ROOT}"
        CACHE PATH "Resolved musl sysroot" FORCE)
endif()

if(DB_TARGET_LINUX_MUSL_TRIPLE MATCHES "^i[3-6]86-")
    find_program(DB_MUSL_QEMU_I386 NAMES qemu-i386)
    if(DB_MUSL_QEMU_I386)
        set(DB_MUSL_EMULATOR "${DB_MUSL_QEMU_I386}")
        if(DEFINED DB_MUSL_ROOT)
            list(APPEND DB_MUSL_EMULATOR -L "${DB_MUSL_ROOT}")
        endif()
        set(CMAKE_CROSSCOMPILING_EMULATOR
            "${DB_MUSL_EMULATOR}"
            CACHE STRING "Resolved i686 musl emulator" FORCE)
    endif()
endif()
