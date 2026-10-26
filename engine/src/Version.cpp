// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/core/Version.h"

#ifndef GG_VERSION_STRING
#error "GG_VERSION_STRING must be defined by the build (see engine/CMakeLists.txt)"
#endif

namespace gg
{
const std::string_view kVersion{GG_VERSION_STRING};
} // namespace gg
