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
    /// The Spectral Smear's ring (the extremes spec §6.2): grains read it as Live grains read the
    /// live ring, Spray seconds behind its head, while Position addresses the smear's material in
    /// the material's units (a fraction of the sample, or seconds behind the live ring's head).
    Spectral,
};
} // namespace gg

#endif // GG_SOURCE_SOURCEMODE_H
