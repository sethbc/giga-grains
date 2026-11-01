// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_MEDIA_WAVBYTES_H
#define GG_MEDIA_WAVBYTES_H

#include <string>

#include "gg/source/SampleBuffer.h"

/// Writing audio as the bytes of a float32 WAV file (spec §5, §6.6): the codec of the session's
/// embedded payloads (AUDI, gg/core/SessionState.h) and of the captures folder's files, so an
/// embedded payload and its filed twin are the same bytes, with one fingerprint. Exact: every
/// float comes back with its bits, -0 and values beyond ±1 included, through decodeAudioFile
/// (gg/media/Decode.h). Main thread or a loader thread: it allocates.
namespace gg::media
{
/// `audio` as a complete WAV file, every integer little-endian:
/// - mono: the classic 44-byte header, a 16-byte `fmt ` chunk of format 3 (IEEE float);
/// - stereo: the 68-byte header of WAVE_FORMAT_EXTENSIBLE (format 0xFFFE), a 40-byte `fmt `
///   chunk with 32 valid bits, channel mask 3 (front left and right) and the IEEE float
///   sub-format GUID;
/// then the `data` chunk, its size exact: the frames interleaved, 32-bit floats as their bits.
/// No `fact` or other chunk is written. The rate is `audio.sampleRate()` rounded to the nearest
/// integer (the header holds one; the manifest's `rate` keeps the double). The same audio gives
/// the same bytes, byte for byte.
/// `audio` has 1 or 2 channels (asserted); throws std::length_error if its data wouldn't fit a
/// RIFF chunk's 32-bit size.
std::string encodeFloat32Wav(const SampleBuffer &audio);
} // namespace gg::media

#endif // GG_MEDIA_WAVBYTES_H
