// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_METRICS_SPECTROGRAM_H
#define GG_METRICS_SPECTROGRAM_H

#include <string>

#include "gg/metrics/AudioData.h"

namespace gg::metrics
{
/// Writes a grayscale PNG spectrogram of the mono mix, for people and Claude to look at.
///
/// - STFT of 2048 samples, periodic Hann window, hop 512. Frames start at multiples of the hop;
///   the last frame is the first that reaches the end of the audio, zero padded.
/// - One column per frame (left to right). Empty audio gives one black column.
/// - Linear frequency, 1024 rows: the top row is Nyquist (bin 1024), the bottom row is bin 1.
/// - Each pixel is clamp((dB + 120) / 120, 0, 1), with dB scaled so a sine of amplitude A reads
///   20·log10(A): 0 dB (white) is a full-scale sine, -120 dB and below is black.
///
/// Non-finite samples count as silence. `path` is UTF-8, as WavIO's are: on Windows a non-ASCII
/// name opens through the wide API. Returns false if the file can't be written.
bool writeSpectrogramPng(const std::string &path, const AudioData &audio);
} // namespace gg::metrics

#endif // GG_METRICS_SPECTROGRAM_H
