// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_SPECTRAL_SPECTRALCONSTANTS_H
#define GG_SPECTRAL_SPECTRALCONSTANTS_H

/// The Spectral Smear's constants (the extremes spec §6.2), apart from SpectralSource and pffft,
/// for code that needs only them (the Cloud scheduler's Spray bound). SpectralSource repeats each
/// as its own static member, so its callers keep their names.
namespace gg::spectral
{
/// The output ring: Spray's 2 s, Size's 2 s, the 256-frame margins and a block (spec §6.2).
inline constexpr double kRingSeconds = 5.0;
/// The STFT's window, in seconds; SpectralSource::fftSizeFor() rounds it to a power of two.
inline constexpr double kWindowSeconds = 0.085;
} // namespace gg::spectral

#endif // GG_SPECTRAL_SPECTRALCONSTANTS_H
