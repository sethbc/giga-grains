# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# Compiles installer/windows/giga-grains.iss with Inno Setup and checks the installer it makes
# (spec §6.5). Run as a CTest, on Windows with Inno Setup 6 (cmake/products.cmake's GG_ISCC):
#
#   cmake -DISCC=<ISCC.exe> -DISS=<giga-grains.iss> -DPRODUCTS=<gg-products>
#         -DLICENSES=<gg-products/licenses> -DOUT=<scratch dir> -DNUMERIC=<X.Y.Z>
#         -DFULL=<full version> -P <this file>
#
# It deletes OUT, compiles the script into it, as gg-installer does, and requires ISCC to exit 0
# and OUT to hold giga-grains-<FULL>-windows-x64.exe, larger than 1 MB (the products are tens of
# megabytes; a script whose [Files] matched nothing would make a few hundred kilobytes).

cmake_minimum_required(VERSION 3.22)

foreach (var IN ITEMS ISCC ISS PRODUCTS LICENSES OUT NUMERIC FULL)
    if (NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
        message(FATAL_ERROR "check-windows-inno: ${var} is not set")
    endif ()
endforeach ()

file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}")
foreach (path IN ITEMS PRODUCTS LICENSES OUT ISS)
    file(TO_NATIVE_PATH "${${path}}" native_${path})
endforeach ()

execute_process(
        COMMAND "${ISCC}" "/DStagedAssets=${native_PRODUCTS}" "/DLicenses=${native_LICENSES}"
        "/DVersion=${NUMERIC}" "/DFullVersion=${FULL}" "/DOutDir=${native_OUT}" "${native_ISS}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
set(transcript "exit status ${result}\n--- stdout\n${stdout}--- stderr\n${stderr}")
if (NOT result EQUAL 0)
    message(FATAL_ERROR "FAIL: ${ISCC} failed on ${ISS}\n${transcript}")
endif ()

set(installer "${OUT}/giga-grains-${FULL}-windows-x64.exe")
if (NOT EXISTS "${installer}")
    file(GLOB written RELATIVE "${OUT}" "${OUT}/*")
    message(FATAL_ERROR "FAIL: ISCC wrote no ${installer} (${OUT} holds '${written}')\n"
            "${transcript}")
endif ()
file(SIZE "${installer}" size)
if (size LESS_EQUAL 1048576)
    message(FATAL_ERROR "FAIL: ${installer} is ${size} bytes, not more than 1 MB\n${transcript}")
endif ()
message(STATUS "PASS: ISCC wrote ${installer} (${size} bytes)")
