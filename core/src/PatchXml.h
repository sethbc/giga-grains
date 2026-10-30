// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_PATCHXML_H
#define GG_CORE_PATCHXML_H

// Internal to gg-core, and free of tinyxml and PatchBase, so the tests can reach it: the patch
// XML of one product's Patch, and a patch's preset metadata alone.

#include <cstdint>
#include <string>
#include <string_view>

#include "gg/core/ModMatrix.h"
#include "gg/core/ParamIds.h"
#include "gg/core/PresetInfo.h"

namespace gg::core
{
/// The state of these values, this seed and these routes, written by a Patch with `mode`'s
/// metadata.
/// toStateXml writes Mode::Fx's, whatever the product, which is right only while both products
/// write the same bytes: a test holds the two to that (carry-over "toStateXml builds a
/// Patch(Mode::Fx)"), so the day their XML differs, toStateXml must take the mode.
std::string patchXml(const ParamValues &values, uint64_t seed, const ModRoutes &routes, Mode mode,
                     std::string_view name = "Init", const PresetInfo *preset = nullptr);

/// The preset metadata of a patch's XML, its root's `name` and its <preset> element, into `info`,
/// without building its values (presetInfoOf in gg/core/Preset.h): false, and `info` untouched,
/// unless `xml` passes the nesting pre-scan, parses without error and has this format's <patch>
/// root holding a <preset> element whose `product` is known. A missing name reads "Init".
bool presetInfoOfXml(std::string_view xml, PresetInfo &info);
} // namespace gg::core

#endif // GG_CORE_PATCHXML_H
