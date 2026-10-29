# SPDX-License-Identifier: AGPL-3.0-or-later
# A target in a subdirectory, which the guard's walk must reach.
add_library(sub_lib STATIC ${CMAKE_CURRENT_BINARY_DIR}/../plain.cpp)
