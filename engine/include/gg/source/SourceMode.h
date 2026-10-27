// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_SOURCE_SOURCEMODE_H
#define GG_SOURCE_SOURCEMODE_H

namespace gg
{
/// Where grains read.
enum class SourceMode
{
    Sample, ///< A loaded SampleBuffer (the instrument; M0's source). Position is a fraction of it.
    Live,   ///< The live ring the FX writes its input into. Position is seconds behind the head.
};
} // namespace gg

#endif // GG_SOURCE_SOURCEMODE_H
