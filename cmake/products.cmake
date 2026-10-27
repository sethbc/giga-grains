# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# gg-products: gathers every built plugin format into one directory, ${CMAKE_BINARY_DIR}/gg-products
# (build/<preset>/gg-products/), for validation and packaging:
#   Giga Grains.clap, Giga Grains.vst3, Giga Grains.component (macOS),
#   and the standalones Giga Grains / Giga Grains FX (.app on macOS, .exe on Windows).
# Adapted from Shortcircuit XT's cmake/basic-installer.cmake.

set(GG_PRODUCT_DIR ${CMAKE_BINARY_DIR}/gg-products)

add_custom_target(gg-products)

# gg_add_product(<format target postfix> <file suffix>)
function(gg_add_product format suffix)
    set(target gg_clapfirst_${format})
    if (NOT TARGET ${target})
        return()
    endif ()

    get_target_property(output_name ${target} LIBRARY_OUTPUT_NAME)
    if (NOT output_name)
        get_target_property(output_name ${target} OUTPUT_NAME)
    endif ()

    add_dependencies(gg-products ${target})

    # On macOS every format is a bundle; elsewhere only the VST3 is (a folder, as
    # WINDOWS_FOLDER_VST3 asks). A bundle's binary sits three levels below its root
    # (<name><suffix>/Contents/<arch or MacOS>/<binary>).
    set(is_bundle FALSE)
    if (APPLE OR "${suffix}" STREQUAL ".vst3")
        set(is_bundle TRUE)
    endif ()

    message(STATUS "gg-products: ${target} -> '${output_name}${suffix}'")
    set(dest "${GG_PRODUCT_DIR}/${output_name}${suffix}")
    if (is_bundle)
        add_custom_command(TARGET gg-products POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E echo "gg-products: ${output_name}${suffix}"
                COMMAND ${CMAKE_COMMAND} -E rm -rf "${dest}"
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                "$<TARGET_FILE_DIR:${target}>/../../../${output_name}${suffix}" "${dest}"
                VERBATIM
                )
    else ()
        add_custom_command(TARGET gg-products POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E echo "gg-products: $<TARGET_FILE_NAME:${target}>"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${GG_PRODUCT_DIR}"
                COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${target}>" "${GG_PRODUCT_DIR}/"
                VERBATIM
                )
    endif ()
endfunction()

gg_add_product(clap ".clap")
gg_add_product(vst3 ".vst3")
gg_add_product(auv2 ".component")
if (APPLE)
    gg_add_product(standalone ".app")
    gg_add_product(standalone_fx ".app")
elseif (WIN32)
    gg_add_product(standalone ".exe")
    gg_add_product(standalone_fx ".exe")
else ()
    gg_add_product(standalone "")
    gg_add_product(standalone_fx "")
endif ()
