// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_METRICS_WAVIO_H
#define GG_METRICS_WAVIO_H

#include <optional>
#include <string>

#include "gg/metrics/AudioData.h"

namespace gg::metrics
{
/// Decodes a WAV (or anything else dr_wav reads: RF64, W64, AIFF), FLAC or MP3 file to float
/// samples, chosen by content rather than extension. Returns nullopt if the file can't be opened
/// or decoded, or reports no channels or a zero sample rate. Both functions take a UTF-8 path;
/// on Windows they open it with dr_libs' wide-character functions.
std::optional<AudioData> readAudioFile(const std::string &path);

/// Writes `audio` as a 32-bit IEEE-float RIFF WAV, so readAudioFile gives back the same bits.
/// Returns false, possibly leaving a partial file, if the audio has no channels, channels of
/// different lengths, more than 65535 channels, a sample rate that isn't a positive integer that
/// fits in 32 bits, more data than RIFF's 4 GiB limit, or if any write fails.
bool writeWavFloat32(const std::string &path, const AudioData &audio);
} // namespace gg::metrics

#endif // GG_METRICS_WAVIO_H
