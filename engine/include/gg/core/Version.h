// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_VERSION_H
#define GG_CORE_VERSION_H

#include <string_view>

namespace gg
{
/// The project version ("MAJOR.MINOR.PATCH[-prerelease]", as cmake/version.cmake composes it),
/// taken from CMake's GG_VERSION_STRING through the compile definition of that name. Defined in
/// the gg-engine library.
extern const std::string_view kVersion;
} // namespace gg

#endif // GG_CORE_VERSION_H
