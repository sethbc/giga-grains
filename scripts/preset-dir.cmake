# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# Prints a configure preset's build tree, its binaryDir, as check.sh and worktree-setup.sh need it,
# without configuring anything:
#
#   cmake -D PRESET=<name> -P scripts/preset-dir.cmake
#
# It prints one line to stdout: "dir <absolute path>", or "error <message>" when the presets
# don't name a usable, non-hidden configure preset <name> or its binaryDir can't be worked out.
# CMake's own exit status is 0 either way; anything else on stdout means the script itself broke.
#
# It reads CMakePresets.json and, if it exists, CMakeUserPresets.json, from the directory above
# this script. binaryDir comes from the preset or, failing that, from the presets it inherits,
# earlier ones first, as CMake resolves it. The macros ${sourceDir}, ${sourceParentDir},
# ${sourceDirName}, ${presetName}, ${hostSystemName}, $env{NAME} and $penv{NAME} are expanded,
# the last two from this process's environment (not the preset's "environment"); any other macro
# is an error. A relative result is taken from the source directory. "include" (presets version
# 4) isn't followed.

cmake_minimum_required(VERSION 3.22)

get_filename_component(source_dir "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

function(result kind text)
    execute_process(COMMAND ${CMAKE_COMMAND} -E echo "${kind} ${text}")
endfunction()

if (NOT DEFINED PRESET OR PRESET STREQUAL "")
    result(error "no preset given (-D PRESET=<name>)")
    return()
endif ()

# Every configure preset of both files: preset_json_<name> holds its JSON object.
set(names "")
foreach (file IN ITEMS CMakePresets.json CMakeUserPresets.json)
    if (NOT EXISTS "${source_dir}/${file}")
        continue()
    endif ()
    file(READ "${source_dir}/${file}" presets_json)
    string(JSON count ERROR_VARIABLE json_error LENGTH "${presets_json}" configurePresets)
    if (json_error)
        if (json_error MATCHES "not found")
            continue()
        endif ()
        result(error "can't read ${file}: ${json_error}")
        return()
    endif ()
    if (count EQUAL 0)
        continue()
    endif ()
    math(EXPR last "${count} - 1")
    foreach (i RANGE ${last})
        string(JSON preset GET "${presets_json}" configurePresets ${i})
        string(JSON name GET "${preset}" name)
        set(preset_json_${name} "${preset}")
        list(APPEND names "${name}")
    endforeach ()
endforeach ()

if (NOT "${PRESET}" IN_LIST names)
    result(error
            "no configure preset named ${PRESET} in CMakePresets.json or CMakeUserPresets.json")
    return()
endif ()
string(JSON hidden ERROR_VARIABLE json_error GET "${preset_json_${PRESET}}" hidden)
if (NOT json_error AND hidden)
    result(error "${PRESET} is a hidden preset, which can't be configured")
    return()
endif ()

# Sets out_var to the binaryDir of `name` or of the first preset it inherits that has one, depth
# first, or to "" if none has. `depth` stops an inheritance cycle.
function(find_binary_dir name depth out_var)
    set(${out_var} "" PARENT_SCOPE)
    if (depth GREATER 32 OR NOT DEFINED preset_json_${name})
        return()
    endif ()
    set(json "${preset_json_${name}}")
    string(JSON dir ERROR_VARIABLE json_error GET "${json}" binaryDir)
    if (NOT json_error)
        set(${out_var} "${dir}" PARENT_SCOPE)
        return()
    endif ()
    string(JSON type ERROR_VARIABLE json_error TYPE "${json}" inherits)
    if (json_error)
        return()
    endif ()
    set(parents "")
    if (type STREQUAL "STRING")
        string(JSON parents GET "${json}" inherits)
    elseif (type STREQUAL "ARRAY")
        string(JSON count LENGTH "${json}" inherits)
        if (count GREATER 0)
            math(EXPR last "${count} - 1")
            foreach (i RANGE ${last})
                string(JSON parent GET "${json}" inherits ${i})
                list(APPEND parents "${parent}")
            endforeach ()
        endif ()
    endif ()
    math(EXPR next "${depth} + 1")
    foreach (parent IN LISTS parents)
        find_binary_dir("${parent}" ${next} dir)
        if (NOT dir STREQUAL "")
            set(${out_var} "${dir}" PARENT_SCOPE)
            return()
        endif ()
    endforeach ()
endfunction()

find_binary_dir("${PRESET}" 0 dir)
if (dir STREQUAL "")
    result(error "${PRESET} has no binaryDir, nor does any preset it inherits")
    return()
endif ()

get_filename_component(source_parent "${source_dir}" DIRECTORY)
get_filename_component(source_name "${source_dir}" NAME)
string(REPLACE "\${sourceDir}" "${source_dir}" dir "${dir}")
string(REPLACE "\${sourceParentDir}" "${source_parent}" dir "${dir}")
string(REPLACE "\${sourceDirName}" "${source_name}" dir "${dir}")
string(REPLACE "\${presetName}" "${PRESET}" dir "${dir}")
string(REPLACE "\${hostSystemName}" "${CMAKE_HOST_SYSTEM_NAME}" dir "${dir}")
# Each pass replaces one variable's every use; the bound stops a value that names itself.
foreach (pass RANGE 16)
    if (NOT dir MATCHES "\\$p?env{([^}]*)}")
        break()
    endif ()
    string(REPLACE "${CMAKE_MATCH_0}" "$ENV{${CMAKE_MATCH_1}}" dir "${dir}")
endforeach ()
if (dir MATCHES "\\$[a-zA-Z]*{")
    result(error "${PRESET}'s binaryDir ${dir} has a macro this script doesn't expand")
    return()
endif ()
get_filename_component(dir "${dir}" ABSOLUTE BASE_DIR "${source_dir}")
result(dir "${dir}")
