# SPDX-License-Identifier: AGPL-3.0-or-later
# One case of the MTS-ESP vendoring guard (cmake/MtsEspGuard.cmake):
#   cmake -DGG_GUARD_CASE=<pass|modified|missing> -DGG_REPO=<root> -DGG_OUT=<scratch dir> -P run.cmake
# "pass" checks the real folder; the others check a copy with one file changed or removed, and
# require the check to fail naming that file.
cmake_minimum_required(VERSION 3.22)

file(REMOVE_RECURSE ${GG_OUT})
set(dir ${GG_OUT}/mts-esp-client)
if (GG_GUARD_CASE STREQUAL "pass")
    set(dir ${GG_REPO}/libs/mts-esp-client)
else ()
    file(COPY ${GG_REPO}/libs/mts-esp-client DESTINATION ${GG_OUT})
endif ()
if (GG_GUARD_CASE STREQUAL "pass")
elseif (GG_GUARD_CASE STREQUAL "modified")
    file(APPEND ${dir}/Client/libMTSClient.cpp "\n")
elseif (GG_GUARD_CASE STREQUAL "missing")
    file(REMOVE ${dir}/Master/libMTSMaster.h)
else ()
    message(FATAL_ERROR "unknown case ${GG_GUARD_CASE}")
endif ()

# The check runs in a child cmake so its FATAL_ERROR is an exit status and a message.
file(WRITE ${GG_OUT}/check.cmake
        "cmake_minimum_required(VERSION 3.22)\ninclude(${GG_REPO}/cmake/MtsEspGuard.cmake)\n"
        "gg_check_mts_esp(DIR ${dir})\n")
execute_process(COMMAND ${CMAKE_COMMAND} -P ${GG_OUT}/check.cmake
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if (GG_GUARD_CASE STREQUAL "pass")
    if (NOT status EQUAL 0)
        message(FATAL_ERROR "the real files were refused:\n${out}\n${err}")
    endif ()
else ()
    if (status EQUAL 0)
        message(FATAL_ERROR "case ${GG_GUARD_CASE}: the guard let it through")
    endif ()
    if (GG_GUARD_CASE STREQUAL "modified")
        set(want "Client/libMTSClient\\.cpp was modified")
    else ()
        set(want "Master/libMTSMaster\\.h is missing")
    endif ()
    if (NOT err MATCHES "${want}")
        message(FATAL_ERROR "case ${GG_GUARD_CASE} failed, but not as expected:\n${err}")
    endif ()
endif ()
