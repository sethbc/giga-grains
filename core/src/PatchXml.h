// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_PATCHXML_H
#define GG_CORE_PATCHXML_H

// Internal to gg-core, and free of tinyxml and PatchBase, so the tests can reach it: the patch
// XML of one product's Patch.

#include <cstdint>
#include <string>

#include "gg/core/ParamIds.h"

namespace gg::core
{
/// The state of these values and this seed, written by a Patch with `mode`'s metadata.
/// toStateXml writes Mode::Fx's, whatever the product, which is right only while both products
/// write the same bytes: a test holds the two to that (carry-over "toStateXml builds a
/// Patch(Mode::Fx)"), so the day their XML differs, toStateXml must take the mode.
std::string patchXml(const ParamValues &values, uint64_t seed, Mode mode);
} // namespace gg::core

#endif // GG_CORE_PATCHXML_H
