# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# Compiler and platform settings, adapted from Shortcircuit XT's cmake/compiler-options.cmake.
# CMAKE_OSX_DEPLOYMENT_TARGET (11.0) is set in the top-level CMakeLists.txt, because it has to
# be set before project().

# Calculate bitness
math(EXPR BITS "8*${CMAKE_SIZEOF_VOID_P}")
if (NOT ${BITS} EQUAL 64)
    message(WARNING "${PROJECT_NAME} has only been tested on 64 bits. This may not work")
endif ()

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU" AND UNIX AND NOT APPLE AND NOT GG_SKIP_PIE_CHANGE)
    message(STATUS "Setting -no-pie on EXE flags; use GG_SKIP_PIE_CHANGE=TRUE to avoid")
    string(APPEND CMAKE_EXE_LINKER_FLAGS " -no-pie")
endif ()

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    if (${GG_SANITIZE})
        message(STATUS "Sanitizer is ON")
    endif ()
    if (${GG_USE_RTSAN})
        message(STATUS "RealtimeSanitizer is ON")
    endif ()

    add_compile_options(
            $<$<BOOL:${GG_SANITIZE}>:-fsanitize=address>
            $<$<BOOL:${GG_SANITIZE}>:-fsanitize=undefined>
            # libstdc++ in gcc 13 deprecates fs::u8path(std::string) for C++20
            # (LWG 3840 may yet revert this). Keep the warning visible but
            # don't fail -Werror builds over it. gcc-only; libc++/Clang is fine.
            $<$<CXX_COMPILER_ID:GNU>:-Wno-error=deprecated-declarations>

            # not technically rtsan but llvm mac requires it
            $<$<BOOL:${GG_USE_RTSAN}>:-Wno-elaborated-enum-base>
            $<$<BOOL:${GG_USE_RTSAN}>:-fsanitize=realtime>
    )

    add_link_options(
            $<$<BOOL:${GG_SANITIZE}>:-fsanitize=address>
            $<$<BOOL:${GG_SANITIZE}>:-fsanitize=undefined>
            $<$<BOOL:${GG_USE_RTSAN}>:-fsanitize=realtime>
    )
endif ()

# Platform Specific Compile Settings
add_library(gg-compiler-options INTERFACE)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Never want shared if not specified")
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)

if (${BUILD_SHARED_LIBS})
    message(FATAL_ERROR "You have overridden BUILD_SHARED_LIBS to be ON. This is an unsupported configuration")
endif ()

if (APPLE)
    enable_language(OBJC)
    enable_language(OBJCXX)
    set(CMAKE_OBJC_VISIBILITY_PRESET hidden)
    set(CMAKE_OBJCXX_VISIBILITY_PRESET hidden)
    if (${CMAKE_CXX_COMPILER_VERSION} VERSION_GREATER_EQUAL "15.0.0" AND ${CMAKE_CXX_COMPILER_VERSION} VERSION_LESS "15.1")
        add_link_options(-Wl,-ld_classic)
        add_compile_definitions(JUCE_SILENCE_XCODE_15_LINKER_WARNING=1)
    endif ()
endif ()

if (APPLE)
    set(OS_COMPILE_OPTIONS
            -Werror
            -Wno-multichar
            -Wno-shorten-64-to-32
            )
    set(OS_COMPILE_DEFINITIONS MAC=1)
elseif (UNIX AND NOT APPLE)
    set(OS_COMPILE_OPTIONS
            -Werror
            -Wno-multichar
            -fPIC  # probably not needed with CMAKE_POSITION_INDEPENDENT_CODE_ON but hey
            $<IF:$<STREQUAL:${CMAKE_SYSTEM_PROCESSOR},aarch64>,-march=armv8-a,-march=nehalem>
            -Wno-psabi  # pair passing warning from gcc10 to now
            )
    set(OS_COMPILE_DEFINITIONS
            LINUX=1)
else ()

    if (CMAKE_CXX_COMPILER_FRONTEND_VARIANT MATCHES "MSVC")
        set(OS_COMPILE_OPTIONS
                /bigobj
                /wd4244   # convert float from double
                /wd4305   # truncate from double to float
                /wd4018   # signed unsigned compare
                /wd4996   # don't warn on stricmp vs _stricmp and other posix names
                /wd4267   # converting size_t <-> int
                /wd4005   # macro redefinition for nominmax
                /utf-8
                )
        message(STATUS "MSVC flags are ${OS_COMPILE_OPTIONS}")
    endif ()

    if (WIN32)
        # Win32 clang marks strncpy as deprecated and wants strncpy_s; maybe one day
        # fix that but not today
        add_compile_definitions(_CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
    endif ()

    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        set(OS_COMPILE_OPTIONS
                -Wno-multichar
                -Wno-macro-redefined
                -march=nehalem

                # Targeting Windows with GCC/Clang is experimental
                # $<$<NOT:$<BOOL:${WIN32}>>:-Werror>

                # PE/COFF doesn't support visibility
                $<$<NOT:$<BOOL:${WIN32}>>:-fvisibility=hidden>

                # Inlines visibility is only relevant with C++
                $<$<AND:$<NOT:$<BOOL:${WIN32}>>,$<COMPILE_LANGUAGE:CXX>>:-fvisibility-inlines-hidden>)
    endif ()

    # WIN32_LEAN_AND_MEAN keeps <windows.h> from pulling in <rpc.h>/<rpcndr.h>.
    # NOMINMAX stops the min/max macros from colliding with std::min/std::max.
    set(OS_COMPILE_DEFINITIONS _CRT_SECURE_NO_WARNINGS=1 WINDOWS=1 _USE_MATH_DEFINES=1
            WIN32_LEAN_AND_MEAN=1 NOMINMAX=1)
endif ()

# Determinism (INTENT §14): never contract a*b+c into an FMA, so every platform rounds the
# same way. ClangCL takes the clang spelling through /clang:. Never enable fast-math.
set(GG_FP_CONTRACT_OFF_OPTIONS)
if (CMAKE_CXX_COMPILER_FRONTEND_VARIANT MATCHES "MSVC")
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set(GG_FP_CONTRACT_OFF_OPTIONS /clang:-ffp-contract=off)
    endif ()
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set(GG_FP_CONTRACT_OFF_OPTIONS -ffp-contract=off)
endif ()
list(APPEND OS_COMPILE_OPTIONS ${GG_FP_CONTRACT_OFF_OPTIONS})

# Warnings (carry-over [94]): -Wall -Wextra on every first-party target (the ones that link this
# interface target; third-party code never does, so it keeps its own warnings and our flags never
# reach it). -Werror comes with the OS options on macOS and Linux, and as /WX on ClangCL. Under
# clang-cl, /Wall is clang's -Weverything, so the GNU spellings go through /clang:. Plain MSVC
# (cl.exe) is not a supported compiler.
if (CMAKE_CXX_COMPILER_FRONTEND_VARIANT MATCHES "MSVC")
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        list(APPEND OS_COMPILE_OPTIONS /clang:-Wall /clang:-Wextra /WX)
    endif ()
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    list(APPEND OS_COMPILE_OPTIONS -Wall -Wextra)
    # GCC 12 only: -Wall's -Wrestrict is a false positive in libstdc++'s std::string code
    # (std::string::assign/operator= from a string_view, e.g. sst-basic-blocks' ParamMetaData;
    # GCC bug 105329 and its family), fixed in GCC 13. Narrowly off for 12.x; every other
    # compiler keeps it.
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 12
            AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13)
        list(APPEND OS_COMPILE_OPTIONS -Wno-restrict)
    endif ()
endif ()

# That flag alone, for third-party code compiled from source (libs/CMakeLists.txt): it computes
# with the project's rounding but keeps its own warnings, out of our -Werror.
add_library(gg-fp-contract-off INTERFACE)
target_compile_options(gg-fp-contract-off INTERFACE ${GG_FP_CONTRACT_OFF_OPTIONS})
add_library(gg::fp-contract-off ALIAS gg-fp-contract-off)

target_compile_options(gg-compiler-options INTERFACE ${OS_COMPILE_OPTIONS})
target_compile_definitions(gg-compiler-options INTERFACE ${OS_COMPILE_DEFINITIONS})
target_compile_definitions(gg-compiler-options INTERFACE $<IF:$<CONFIG:DEBUG>,BUILD_IS_DEBUG,BUILD_IS_RELEASE>=1)
# So code can compile out debug-only work that allocates on the realtime path under rtsan.
target_compile_definitions(gg-compiler-options INTERFACE $<$<BOOL:${GG_USE_RTSAN}>:BUILD_IS_RTSAN=1>)
add_library(gg::compiler-options ALIAS gg-compiler-options)
