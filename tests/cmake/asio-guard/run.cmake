# SPDX-License-Identifier: AGPL-3.0-or-later
# Configures the scratch project for one case (see CMakeLists.txt beside this file):
#   cmake -DGG_GUARD_CASE=<case> -DGG_REPO=<root> -DGG_OUT=<build dir> -P run.cmake
# "allowed" must configure; any other case must fail with the guard's message.

# A -P script sets no policies of its own: the project's minimum, so CMake 3.x reads it as 4.x
# does (CMP0012's TRUE in a condition among them).
cmake_minimum_required(VERSION 3.22)

file(REMOVE_RECURSE ${GG_OUT})
execute_process(
        COMMAND ${CMAKE_COMMAND} -S ${CMAKE_CURRENT_LIST_DIR} -B ${GG_OUT}
        -DGG_GUARD_CASE=${GG_GUARD_CASE} -DGG_REPO=${GG_REPO}
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if (GG_GUARD_CASE STREQUAL "allowed")
    if (NOT status EQUAL 0)
        message(FATAL_ERROR "the allowed use was refused:\n${out}\n${err}")
    endif ()
else ()
    if (status EQUAL 0)
        message(FATAL_ERROR "case ${GG_GUARD_CASE} configured: the guard let a misuse through")
    endif ()
    if (NOT err MATCHES "is used outside that")
        message(FATAL_ERROR "case ${GG_GUARD_CASE} failed, but not in the guard:\n${out}\n${err}")
    endif ()
endif ()
