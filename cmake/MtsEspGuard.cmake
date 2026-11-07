# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# The MTS-ESP vendoring guard. libs/mts-esp-client/ holds five files of ODDSound's MTS-ESP
# repository (https://github.com/ODDSound/MTS-ESP, commit f214739b8832e7f297cb9970d0c0efbf783f1462)
# byte for byte: the client and master sources and headers, and the 0BSD LICENSE. The repository
# also holds ODDSound's closed libMTS binaries, which never enter this AGPL tree, so MTS-ESP is
# vendored rather than a submodule. gg_check_mts_esp() computes each file's SHA-256 at configure
# and fails, naming the file, the expected and the actual hash, when one differs or is missing.
#
#   gg_check_mts_esp([DIR <dir>])
#
# DIR is the vendored folder (default ${CMAKE_SOURCE_DIR}/libs/mts-esp-client); the scratch cases
# in tests/cmake/mts-esp-guard use it on copies.

set(GG_MTS_ESP_HASHES
        "LICENSE|481d4c320dc5c1a7f5b4604c8e9a08ac7710e73291b177c468ab69e0fe158837"
        "Client/libMTSClient.h|5c89f524b41f732b955e092c35522eb007dcb3a23db7631e9f9f75f4ffcf958f"
        "Client/libMTSClient.cpp|b854e7c2b657a660431839fb1c413522fa2877296404cee2a36db55d8813c4b3"
        "Master/libMTSMaster.h|124caf7eb967c68c221cd0632b8ead09d143456ec3f72be304007daf5ebfb186"
        "Master/libMTSMaster.cpp|5bad1805125c86b8e710113ff367823fe3615774734b82709cb88cff69293db2")

function (gg_check_mts_esp)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "DIR" "")
    if (NOT arg_DIR)
        set(arg_DIR ${CMAKE_SOURCE_DIR}/libs/mts-esp-client)
    endif ()
    foreach (entry IN LISTS GG_MTS_ESP_HASHES)
        string(REPLACE "|" ";" parts "${entry}")
        list(GET parts 0 file)
        list(GET parts 1 expected)
        if (NOT EXISTS ${arg_DIR}/${file})
            message(FATAL_ERROR "the vendored MTS-ESP file ${file} is missing from ${arg_DIR}")
        endif ()
        file(SHA256 ${arg_DIR}/${file} actual)
        if (NOT actual STREQUAL expected)
            message(FATAL_ERROR "the vendored MTS-ESP file ${file} was modified: expected SHA-256 "
                    "${expected}, got ${actual}")
        endif ()
    endforeach ()
endfunction ()
